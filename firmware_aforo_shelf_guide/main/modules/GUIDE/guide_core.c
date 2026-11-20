/* Lógica pura de la guía LED: sesiones, vigencia y deduplicación.
 * No incluye ESP-IDF ni GPIO para poder probarse en host. */

#include "guide_core.h"

#include <string.h>

static bool due(uint32_t now_ms, uint32_t deadline_ms) {
  /* Aritmética con signo: tolera el desbordamiento de uint32 en ~49 días. */
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

static bool id_valid(const char *id) {
  size_t len;
  if (!id)
    return false;
  len = strlen(id);
  if (len < 8U || len > GUIDE_ID_MAX)
    return false;
  for (size_t i = 0; i < len; ++i) {
    char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-'))
      return false;
  }
  return true;
}

static bool boot_valid(const char *value) {
  size_t len;
  if (!value)
    return false;
  len = strlen(value);
  if (len != GUIDE_BOOT_TEXT)
    return false;
  for (size_t i = 0; i < len; ++i) {
    char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F')))
      return false;
  }
  return true;
}

static bool uuid_valid(const char *value) {
  size_t len;
  if (!value)
    return false;
  len = strlen(value);
  if (len != GUIDE_SESSION_MAX)
    return false;
  for (size_t i = 0; i < len; ++i) {
    char c = value[i];
    if (i == 8U || i == 13U || i == 18U || i == 23U) {
      if (c != '-')
        return false;
    } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

static void copy_text(char *dst, size_t size, const char *src) {
  if (!size)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t len = strlen(src);
  if (len >= size)
    len = size - 1U;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

static void record(guide_core_t *core, guide_outcome_t outcome) {
  core->last_valid = true;
  core->last_channel = outcome.channel;
  core->last_applied = outcome.applied;
  core->last_reason = outcome.reason;
}

static guide_cache_entry_t *cache_find(guide_core_t *core, const char *id,
                                       uint32_t now_ms) {
  for (uint16_t i = 0; i < GUIDE_CACHE_COUNT; ++i) {
    guide_cache_entry_t *entry = &core->cache[i];
    if (!entry->used)
      continue;
    if ((uint32_t)(now_ms - entry->stored_ms) > GUIDE_CACHE_TTL_MS) {
      entry->used = false;
      continue;
    }
    if (!strcmp(entry->id, id))
      return entry;
  }
  return NULL;
}

static void cache_store(guide_core_t *core, const char *id,
                        guide_applied_t applied, const char *reason,
                        uint8_t channel, uint32_t now_ms) {
  guide_cache_entry_t *entry = NULL;
  if (!id || !*id)
    return;
  for (uint16_t i = 0; i < GUIDE_CACHE_COUNT; ++i) {
    if (core->cache[i].used && !strcmp(core->cache[i].id, id)) {
      entry = &core->cache[i];
      break;
    }
  }
  if (!entry) {
    entry = &core->cache[core->cache_next % GUIDE_CACHE_COUNT];
    core->cache_next = (uint16_t)((core->cache_next + 1U) % GUIDE_CACHE_COUNT);
  }
  entry->used = true;
  copy_text(entry->id, sizeof(entry->id), id);
  entry->applied = applied;
  entry->reason = reason;
  entry->channel = channel;
  entry->stored_ms = now_ms;
}

void guide_core_init(guide_core_t *core, const char *boot, uint8_t capacity) {
  if (!core)
    return;
  memset(core, 0, sizeof(*core));
  copy_text(core->boot, sizeof(core->boot), boot);
  core->capacity = capacity > GUIDE_MAX_CHANNELS ? GUIDE_MAX_CHANNELS : capacity;
}

void guide_core_set_boot(guide_core_t *core, const char *boot) {
  if (!core)
    return;
  copy_text(core->boot, sizeof(core->boot), boot);
}

void guide_core_clear(guide_core_t *core) {
  if (!core)
    return;
  memset(core->sessions, 0, sizeof(core->sessions));
  memset(core->cache, 0, sizeof(core->cache));
  core->cache_next = 0;
  core->last_valid = false;
  core->last_channel = 0;
  core->last_applied = GUIDE_APPLIED_REJECTED;
  core->last_reason = GUIDE_REASON_MALFORMED;
}

guide_outcome_t guide_core_handle(guide_core_t *core, const guide_cmd_t *cmd,
                                  uint32_t now_ms) {
  guide_outcome_t out = {.applied = GUIDE_APPLIED_REJECTED,
                         .reason = GUIDE_REASON_MALFORMED,
                         .channel = 0,
                         .duplicate = false};
  guide_cache_entry_t *hit;
  guide_session_t *session;
  bool channel_ok;

  if (!core || !cmd)
    return out;

  channel_ok = cmd->channel >= 0 && cmd->channel < (int)core->capacity;
  if (channel_ok)
    out.channel = (uint8_t)cmd->channel;

  if (!id_valid(cmd->id))
    return out;

  /* 1. Duplicado: mismo resultado, sin reiniciar ni prolongar el TTL. */
  hit = cache_find(core, cmd->id, now_ms);
  if (hit) {
    out.duplicate = true;
    out.channel = hit->channel;
    if (hit->applied == GUIDE_APPLIED_ON) {
      session = &core->sessions[hit->channel];
      if (session->active && !strcmp(session->id, hit->id)) {
        out.applied = GUIDE_APPLIED_ON;
        out.reason = GUIDE_REASON_OK;
      } else {
        /* La sesión ya venció: la orden no revive ni vuelve a encender. */
        out.applied = GUIDE_APPLIED_REJECTED;
        out.reason = GUIDE_REASON_STALE;
      }
    } else {
      out.applied = hit->applied;
      out.reason = hit->reason;
    }
    record(core, out);
    return out;
  }

  /* 2. Validación del contrato (§7.2). */
  if (cmd->version != GUIDE_PROTOCOL_VERSION) {
    cache_store(core, cmd->id, GUIDE_APPLIED_REJECTED, GUIDE_REASON_MALFORMED,
                out.channel, now_ms);
    record(core, out);
    return out;
  }
  if (cmd->action != GUIDE_ACTION_START && cmd->action != GUIDE_ACTION_STOP) {
    cache_store(core, cmd->id, GUIDE_APPLIED_REJECTED, GUIDE_REASON_MALFORMED,
                out.channel, now_ms);
    record(core, out);
    return out;
  }
  if (!channel_ok) {
    out.reason = GUIDE_REASON_BAD_CHANNEL;
    cache_store(core, cmd->id, GUIDE_APPLIED_REJECTED, out.reason, out.channel,
                now_ms);
    record(core, out);
    return out;
  }
  if (!cmd->has_boot || !boot_valid(cmd->boot)) {
    cache_store(core, cmd->id, GUIDE_APPLIED_REJECTED, GUIDE_REASON_MALFORMED,
                out.channel, now_ms);
    record(core, out);
    return out;
  }
  if (strcmp(cmd->boot, core->boot) != 0) {
    out.reason = GUIDE_REASON_WRONG_BOOT;
    cache_store(core, cmd->id, GUIDE_APPLIED_REJECTED, out.reason, out.channel,
                now_ms);
    record(core, out);
    return out;
  }

  if (cmd->action == GUIDE_ACTION_START) {
    if (!cmd->has_session || !uuid_valid(cmd->session) || !cmd->has_accept ||
        !cmd->has_end) {
      cache_store(core, cmd->id, GUIDE_APPLIED_REJECTED, GUIDE_REASON_MALFORMED,
                  out.channel, now_ms);
      record(core, out);
      return out;
    }
    if ((int32_t)(cmd->end_ms - cmd->accept_ms) <= 0 ||
        (uint32_t)(cmd->end_ms - cmd->accept_ms) > GUIDE_MAX_TTL_MS) {
      cache_store(core, cmd->id, GUIDE_APPLIED_REJECTED, GUIDE_REASON_MALFORMED,
                  out.channel, now_ms);
      record(core, out);
      return out;
    }
    if (due(now_ms, cmd->accept_ms) || due(now_ms, cmd->end_ms)) {
      /* Llegó tarde: no se inicia un TTL nuevo. */
      out.reason = GUIDE_REASON_STALE;
      cache_store(core, cmd->id, GUIDE_APPLIED_REJECTED, out.reason, out.channel,
                  now_ms);
      record(core, out);
      return out;
    }
    if ((int32_t)(cmd->end_ms - now_ms) > (int32_t)GUIDE_MAX_TOTAL_MS) {
      cache_store(core, cmd->id, GUIDE_APPLIED_REJECTED, GUIDE_REASON_MALFORMED,
                  out.channel, now_ms);
      record(core, out);
      return out;
    }

    session = &core->sessions[out.channel];
    if (session->active) {
      if (strcmp(session->session, cmd->session) != 0) {
        /* Otro dueño del canal: no se toca su sesión. */
        out.reason = GUIDE_REASON_BUSY;
        record(core, out);
        return out;
      }
      /* Misma sesión, nuevo id: se responde el estado sin prolongar. */
      out.applied = GUIDE_APPLIED_ON;
      out.reason = GUIDE_REASON_OK;
      out.duplicate = true;
      record(core, out);
      return out;
    }

    session->active = true;
    copy_text(session->id, sizeof(session->id), cmd->id);
    copy_text(session->session, sizeof(session->session), cmd->session);
    session->end_ms = cmd->end_ms;
    out.applied = GUIDE_APPLIED_ON;
    out.reason = GUIDE_REASON_OK;
    cache_store(core, cmd->id, out.applied, out.reason, out.channel, now_ms);
    record(core, out);
    return out;
  }

  /* GUIDE_ACTION_STOP */
  if (!cmd->has_session || !uuid_valid(cmd->session)) {
    cache_store(core, cmd->id, GUIDE_APPLIED_REJECTED, GUIDE_REASON_MALFORMED,
                out.channel, now_ms);
    record(core, out);
    return out;
  }
  session = &core->sessions[out.channel];
  if (session->active && strcmp(session->session, cmd->session) != 0) {
    /* Un off solo apaga su propia sesión. */
    out.reason = GUIDE_REASON_STALE;
    cache_store(core, cmd->id, GUIDE_APPLIED_REJECTED, out.reason, out.channel,
                now_ms);
    record(core, out);
    return out;
  }
  session->active = false;
  out.applied = GUIDE_APPLIED_OFF;
  out.reason = GUIDE_REASON_OK;
  cache_store(core, cmd->id, out.applied, out.reason, out.channel, now_ms);
  record(core, out);
  return out;
}

uint8_t guide_core_expire(guide_core_t *core, uint32_t now_ms,
                          guide_expiry_t *out, uint8_t max) {
  uint8_t count = 0;
  if (!core || !out || !max)
    return 0;
  for (uint8_t channel = 0; channel < core->capacity && count < max; ++channel) {
    guide_session_t *session = &core->sessions[channel];
    if (!session->active || !due(now_ms, session->end_ms))
      continue;
    out[count].channel = channel;
    copy_text(out[count].id, sizeof(out[count].id), session->id);
    copy_text(out[count].session, sizeof(out[count].session), session->session);
    session->active = false;
    cache_store(core, out[count].id, GUIDE_APPLIED_OFF, GUIDE_REASON_TTL, channel,
                now_ms);
    core->last_valid = true;
    core->last_channel = channel;
    core->last_applied = GUIDE_APPLIED_OFF;
    core->last_reason = GUIDE_REASON_TTL;
    ++count;
  }
  return count;
}

void guide_core_fail(guide_core_t *core, uint8_t channel, const char *id,
                     uint32_t now_ms) {
  if (!core || channel >= core->capacity)
    return;
  core->sessions[channel].active = false;
  core->last_valid = true;
  core->last_channel = channel;
  core->last_applied = GUIDE_APPLIED_ERROR;
  core->last_reason = GUIDE_REASON_GPIO_ERROR;
  if (id && *id)
    cache_store(core, id, GUIDE_APPLIED_ERROR, GUIDE_REASON_GPIO_ERROR, channel,
                now_ms);
}

const guide_session_t *guide_core_session(const guide_core_t *core,
                                          uint8_t channel) {
  if (!core || channel >= core->capacity)
    return NULL;
  return &core->sessions[channel];
}


