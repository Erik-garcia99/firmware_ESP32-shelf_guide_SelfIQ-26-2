/* Parser estricto de la orden de guía (§7.2). Sin ESP-IDF ni cJSON para poder
 * probarse en host: si el mensaje no cumple el contrato, se rechaza completo.
 */

#include "guide_protocol.h"

#include <string.h>

typedef struct {
  const char *p;
  const char *end;
} cursor_t;

/* Orden de claves del contrato: el índice define el bit de presencia. */
enum {
  KEY_V = 0,
  KEY_T,
  KEY_ID,
  KEY_SESSION,
  KEY_CH,
  KEY_BOOT,
  KEY_ACCEPT,
  KEY_END,
  KEY_COUNT
};

static const char *const KEY_NAMES[KEY_COUNT] = {
    "v", "t", "id", "session", "ch", "boot", "accept_ms", "end_ms"};

#define BIT(key) (1U << (key))

static void copy_text(char *dst, size_t size, const char *src) {
  size_t len;
  if (!size)
    return;
  len = strlen(src);
  if (len >= size)
    len = size - 1U;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

static void skip_ws(cursor_t *c) {
  while (c->p < c->end &&
         (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r'))
    ++c->p;
}

static bool expect(cursor_t *c, char ch) {
  skip_ws(c);
  if (c->p >= c->end || *c->p != ch)
    return false;
  ++c->p;
  return true;
}

static bool parse_string(cursor_t *c, char *out, size_t size) {
  size_t len = 0;
  skip_ws(c);
  if (c->p >= c->end || *c->p != '"')
    return false;
  ++c->p;
  while (c->p < c->end) {
    unsigned char ch = (unsigned char)*c->p++;
    if (ch == '"') {
      out[len] = '\0';
      return true;
    }
    if (ch < 0x20U)
      return false;
    if (ch == '\\') {
      if (c->p >= c->end)
        return false;
      switch (*c->p++) {
      case '"':
        ch = '"';
        break;
      case '\\':
        ch = '\\';
        break;
      case '/':
        ch = '/';
        break;
      case 'b':
        ch = '\b';
        break;
      case 'f':
        ch = '\f';
        break;
      case 'n':
        ch = '\n';
        break;
      case 'r':
        ch = '\r';
        break;
      case 't':
        ch = '\t';
        break;
      default:
        return false; /* \uXXXX no aparece en los campos del contrato */
      }
    }
    if (len + 1U >= size)
      return false;
    out[len++] = (char)ch;
  }
  return false;
}

static bool parse_u32(cursor_t *c, uint32_t *out) {
  uint32_t value = 0;
  bool digits = false;
  skip_ws(c);
  if (c->p >= c->end)
    return false;
  if (*c->p == '-') /* ningún campo del contrato admite negativos */
    return false;
  while (c->p < c->end && *c->p >= '0' && *c->p <= '9') {
    uint32_t digit = (uint32_t)(*c->p - '0');
    if (value > (UINT32_MAX - digit) / 10U)
      return false;
    value = value * 10U + digit;
    digits = true;
    ++c->p;
  }
  if (!digits)
    return false;
  if (c->p < c->end && (*c->p == '.' || *c->p == 'e' || *c->p == 'E'))
    return false; /* decimal o exponente: tipo incorrecto */
  *out = value;
  return true;
}

static int key_index(const char *key) {
  for (int i = 0; i < KEY_COUNT; ++i) {
    if (!strcmp(key, KEY_NAMES[i]))
      return i;
  }
  return -1;
}

static unsigned required_mask(guide_action_t action) {
  switch (action) {
  case GUIDE_ACTION_START:
    return BIT(KEY_V) | BIT(KEY_T) | BIT(KEY_ID) | BIT(KEY_SESSION) | BIT(KEY_CH) |
           BIT(KEY_BOOT) | BIT(KEY_ACCEPT) | BIT(KEY_END);
  case GUIDE_ACTION_STOP:
    return BIT(KEY_V) | BIT(KEY_T) | BIT(KEY_ID) | BIT(KEY_SESSION) | BIT(KEY_CH) |
           BIT(KEY_BOOT);
  case GUIDE_ACTION_STATUS:
    return BIT(KEY_V) | BIT(KEY_T) | BIT(KEY_ID);
  case GUIDE_ACTION_NONE:
  default:
    return 0;
  }
}

guide_parse_status_t guide_parse_command(const char *json, size_t len,
                                         guide_cmd_t *cmd) {
  cursor_t c;
  unsigned seen = 0;

  if (!json || !cmd)
    return GUIDE_PARSE_MALFORMED;
  memset(cmd, 0, sizeof(*cmd));
  cmd->channel = -1;
  if (len > GUIDE_JSON_MAX)
    return GUIDE_PARSE_TOO_BIG;

  c.p = json;
  c.end = json + len;
  if (!expect(&c, '{'))
    return GUIDE_PARSE_MALFORMED;

  skip_ws(&c);
  if (c.p < c.end && *c.p == '}') {
    ++c.p;
  } else {
    for (;;) {
      char key[12];
      char text[GUIDE_SESSION_MAX + 1U];
      uint32_t number = 0;
      int index;

      if (!parse_string(&c, key, sizeof(key)))
        return GUIDE_PARSE_MALFORMED;
      index = key_index(key);
      if (index < 0 || (seen & BIT(index)))
        return GUIDE_PARSE_MALFORMED;
      seen |= BIT(index);

      if (!expect(&c, ':'))
        return GUIDE_PARSE_MALFORMED;

      skip_ws(&c);
      if (c.p < c.end && *c.p == '"') {
        if (!parse_string(&c, text, sizeof(text)))
          return GUIDE_PARSE_MALFORMED;
        switch (index) {
        case KEY_T:
          cmd->action = guide_action_from_text(text);
          if (cmd->action == GUIDE_ACTION_NONE)
            return GUIDE_PARSE_MALFORMED;
          break;
        case KEY_ID:
          copy_text(cmd->id, sizeof(cmd->id), text);
          break;
        case KEY_SESSION:
          copy_text(cmd->session, sizeof(cmd->session), text);
          cmd->has_session = true;
          break;
        case KEY_BOOT:
          copy_text(cmd->boot, sizeof(cmd->boot), text);
          cmd->has_boot = true;
          break;
        default:
          return GUIDE_PARSE_MALFORMED; /* campo numérico con cadena */
        }
      } else {
        if (!parse_u32(&c, &number))
          return GUIDE_PARSE_MALFORMED;
        switch (index) {
        case KEY_V:
          if (number > 255U)
            return GUIDE_PARSE_MALFORMED;
          cmd->version = (uint8_t)number;
          break;
        case KEY_CH:
          if (number > 1000U) /* muy por encima del techo lógico 10 */
            return GUIDE_PARSE_MALFORMED;
          cmd->channel = (int)number;
          break;
        case KEY_ACCEPT:
          cmd->accept_ms = number;
          cmd->has_accept = true;
          break;
        case KEY_END:
          cmd->end_ms = number;
          cmd->has_end = true;
          break;
        default:
          return GUIDE_PARSE_MALFORMED; /* campo de texto con número */
        }
      }

      skip_ws(&c);
      if (c.p < c.end && *c.p == ',') {
        ++c.p;
        continue;
      }
      if (c.p < c.end && *c.p == '}') {
        ++c.p;
        break;
      }
      return GUIDE_PARSE_MALFORMED;
    }
  }

  skip_ws(&c);
  if (c.p != c.end)
    return GUIDE_PARSE_MALFORMED;
  if (cmd->action == GUIDE_ACTION_NONE || seen != required_mask(cmd->action))
    return GUIDE_PARSE_MALFORMED;
  return GUIDE_PARSE_OK;
}

/* --- Prueba de luz del panel staff ------------------------------------- */

enum {
  TEST_KEY_TYPE = 0,
  TEST_KEY_CHANNEL,
  TEST_KEY_DURATION,
  TEST_KEY_COUNT
};

static const char *const TEST_KEY_NAMES[TEST_KEY_COUNT] = {"type", "channel",
                                                           "duration_ms"};

bool guide_parse_light_test(const char *json, size_t len,
                            guide_light_test_t *test) {
  cursor_t c;
  unsigned seen = 0;
  uint32_t channel = 0;
  uint32_t duration = 0;

  if (!json || !test)
    return false;
  memset(test, 0, sizeof(*test));
  if (len > GUIDE_JSON_MAX)
    return false;

  c.p = json;
  c.end = json + len;
  if (!expect(&c, '{'))
    return false;

  skip_ws(&c);
  if (c.p < c.end && *c.p == '}') {
    ++c.p;
  } else {
    for (;;) {
      char key[12];
      int index = -1;

      if (!parse_string(&c, key, sizeof(key)) || !expect(&c, ':'))
        return false;
      for (int i = 0; i < TEST_KEY_COUNT; ++i) {
        if (!strcmp(key, TEST_KEY_NAMES[i]))
          index = i;
      }
      if (index < 0 || (seen & (1U << index)))
        return false;
      seen |= (1U << index);

      if (index == TEST_KEY_TYPE) {
        char text[16];
        if (!parse_string(&c, text, sizeof(text)) || strcmp(text, "light.test"))
          return false;
      } else {
        uint32_t number = 0;
        if (!parse_u32(&c, &number))
          return false;
        if (index == TEST_KEY_CHANNEL)
          channel = number;
        else
          duration = number;
      }

      skip_ws(&c);
      if (c.p < c.end && *c.p == ',') {
        ++c.p;
        continue;
      }
      if (c.p < c.end && *c.p == '}') {
        ++c.p;
        break;
      }
      return false;
    }
  }

  skip_ws(&c);
  if (c.p != c.end || seen != ((1U << TEST_KEY_COUNT) - 1U))
    return false;
  if (channel >= 1000U)
    return false;
  if (duration < GUIDE_TEST_MIN_MS || duration > GUIDE_TEST_MAX_MS)
    return false;
  test->channel = (uint8_t)channel;
  test->duration_ms = duration;
  return true;
}
