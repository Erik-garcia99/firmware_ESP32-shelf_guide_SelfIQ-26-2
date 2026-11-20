#ifndef SELFIQ_GUIDE_PROTOCOL_H
#define SELFIQ_GUIDE_PROTOCOL_H

/* Contrato guía LED v1 (docs_compartido/contracts/guia_led_v1.md).
 * Este archivo no depende de ESP-IDF: describe lo que viaja por MQTT y lo que
 * el nodo acepta o rechaza. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GUIDE_PROTOCOL_VERSION 1U
#define GUIDE_MAX_CHANNELS 8U       /* capacidad máxima del piloto */
#define GUIDE_LOGICAL_LIMIT 10U     /* techo de BD/UI; el nodo nunca lo supera */
#define GUIDE_ID_MAX 64U
#define GUIDE_SESSION_MAX 36U
#define GUIDE_BOOT_TEXT 8U
#define GUIDE_BOOT_SIZE (GUIDE_BOOT_TEXT + 1U)
#define GUIDE_MAX_TTL_MS 60000U
#define GUIDE_MAX_ACCEPT_MS 3000U
#define GUIDE_MAX_TOTAL_MS (GUIDE_MAX_TTL_MS + 2000U + GUIDE_MAX_ACCEPT_MS)
#define GUIDE_JSON_MAX 512U
#define GUIDE_CACHE_COUNT 16U
#define GUIDE_CACHE_TTL_MS 70000U
#define GUIDE_FW_VERSION "1.0.0"

/* Motivos del contrato (§7.2 y §7.3). */
#define GUIDE_REASON_OK "ok"
#define GUIDE_REASON_TTL "ttl"
#define GUIDE_REASON_STALE "stale"
#define GUIDE_REASON_WRONG_BOOT "wrong_boot"
#define GUIDE_REASON_BAD_CHANNEL "bad_channel"
#define GUIDE_REASON_MALFORMED "malformed"
#define GUIDE_REASON_DUPLICATE "duplicate"
#define GUIDE_REASON_GPIO_ERROR "gpio_error"
#define GUIDE_REASON_BUSY "busy"

typedef enum {
  GUIDE_ACTION_NONE = 0,
  GUIDE_ACTION_START,
  GUIDE_ACTION_STOP,
  GUIDE_ACTION_STATUS,
} guide_action_t;

typedef enum {
  GUIDE_APPLIED_ON = 0,
  GUIDE_APPLIED_OFF,
  GUIDE_APPLIED_REJECTED,
  GUIDE_APPLIED_ERROR,
} guide_applied_t;

typedef struct {
  guide_action_t action;
  uint8_t version;
  char id[GUIDE_ID_MAX + 1U];
  char session[GUIDE_SESSION_MAX + 1U];
  int channel; /* -1 cuando falta o no es entero */
  char boot[GUIDE_BOOT_SIZE];
  uint32_t accept_ms; /* uptime del nodo */
  uint32_t end_ms;    /* uptime del nodo */
  bool has_session;
  bool has_boot;
  bool has_accept;
  bool has_end;
} guide_cmd_t;

typedef enum {
  GUIDE_PARSE_OK = 0,
  GUIDE_PARSE_MALFORMED,
  GUIDE_PARSE_TOO_BIG,
} guide_parse_status_t;

/* Prueba de luz del panel staff (`light.test`), el mensaje que ya publica la
 * Raspberry: {"type":"light.test","channel":5,"duration_ms":5000}. */
#define GUIDE_TEST_MIN_MS 100U
#define GUIDE_TEST_MAX_MS 30000U

typedef struct {
  uint8_t channel;
  uint32_t duration_ms;
} guide_light_test_t;

/* Parser estricto del objeto JSON de una orden (§7.2). Sin dependencias:
 * acepta solo objeto plano con cadenas y enteros, exige la lista exacta de
 * campos de la acción y rechaza claves duplicadas, tipos incorrectos,
 * decimales, escapes \u y bytes de control. */
guide_parse_status_t guide_parse_command(const char *json, size_t len,
                                         guide_cmd_t *cmd);

/* Parser de la prueba staff: exige `type`, `channel` y `duration_ms` exactos,
 * sin campos extra ni duplicados, y duración entre 100 ms y 30 s. */
bool guide_parse_light_test(const char *json, size_t len,
                            guide_light_test_t *test);

guide_action_t guide_action_from_text(const char *text);
const char *guide_applied_text(guide_applied_t applied);
const char *guide_action_text(guide_action_t action);

#endif /* SELFIQ_GUIDE_PROTOCOL_H */
