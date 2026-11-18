#ifndef SELFIQ_GUIDE_CORE_H
#define SELFIQ_GUIDE_CORE_H

/* Lógica pura de sesiones y vigencia de la guía LED.
 * Sin ESP-IDF y sin GPIO: se compila y prueba en host (test_host/). */

#include "guide_protocol.h"

typedef struct {
  bool active;
  char id[GUIDE_ID_MAX + 1U];
  char session[GUIDE_SESSION_MAX + 1U];
  uint32_t end_ms; /* uptime del nodo en que debe apagarse */
} guide_session_t;

typedef struct {
  bool used;
  char id[GUIDE_ID_MAX + 1U];
  guide_applied_t applied;
  const char *reason;
  uint8_t channel;
  uint32_t stored_ms;
} guide_cache_entry_t;

typedef struct {
  char boot[GUIDE_BOOT_SIZE];
  uint8_t capacity;
  guide_session_t sessions[GUIDE_MAX_CHANNELS];
  guide_cache_entry_t cache[GUIDE_CACHE_COUNT];
  uint16_t cache_next;
  bool last_valid;
  uint8_t last_channel;
  guide_applied_t last_applied;
  const char *last_reason;
} guide_core_t;

typedef struct {
  guide_applied_t applied;
  const char *reason;
  uint8_t channel;
  bool duplicate;
} guide_outcome_t;

typedef struct {
  uint8_t channel;
  char id[GUIDE_ID_MAX + 1U];
  char session[GUIDE_SESSION_MAX + 1U];
} guide_expiry_t;

void guide_core_init(guide_core_t *core, const char *boot, uint8_t capacity);
void guide_core_set_boot(guide_core_t *core, const char *boot);
/* Apaga lógicamente todo (reset de identidad o pérdida de capacidad). */
void guide_core_clear(guide_core_t *core);

/* Decide qué hacer con un comando ya parseado. No toca hardware. */
guide_outcome_t guide_core_handle(guide_core_t *core, const guide_cmd_t *cmd,
                                  uint32_t now_ms);

/* Devuelve las sesiones que vencieron por TTL y las cierra. */
uint8_t guide_core_expire(guide_core_t *core, uint32_t now_ms,
                          guide_expiry_t *out, uint8_t max);

/* Marca el canal como no encendido tras un fallo de GPIO y recuerda el
 * resultado para que un duplicado QoS 1 no reintente la salida. */
void guide_core_fail(guide_core_t *core, uint8_t channel, const char *id,
                     uint32_t now_ms);

const guide_session_t *guide_core_session(const guide_core_t *core, uint8_t channel);

#endif /* SELFIQ_GUIDE_CORE_H */
