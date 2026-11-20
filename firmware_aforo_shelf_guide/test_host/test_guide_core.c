/* Pruebas de host para la lógica pura de la guía LED (sin ESP-IDF).
 * Compilar y ejecutar: test_host/run_host_tests.sh */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "guide_core.h"
#include "guide_protocol.h"

static guide_core_t core;

static const char *BOOT = "1f3ac9d2";
static const char *ID_A = "cmd-00000001";
static const char *ID_B = "cmd-00000002";
static const char *SES_A = "11111111-1111-4111-8111-111111111111";
static const char *SES_B = "22222222-2222-4222-8222-222222222222";

static void setup(void) {
  guide_core_init(&core, BOOT, 8);
}

static guide_cmd_t start_cmd(const char *id, const char *session, int ch,
                             uint32_t accept_ms, uint32_t end_ms) {
  guide_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.action = GUIDE_ACTION_START;
  cmd.version = GUIDE_PROTOCOL_VERSION;
  cmd.channel = ch;
  snprintf(cmd.id, sizeof(cmd.id), "%s", id);
  snprintf(cmd.session, sizeof(cmd.session), "%s", session);
  snprintf(cmd.boot, sizeof(cmd.boot), "%s", BOOT);
  cmd.accept_ms = accept_ms;
  cmd.end_ms = end_ms;
  cmd.has_session = true;
  cmd.has_boot = true;
  cmd.has_accept = true;
  cmd.has_end = true;
  return cmd;
}

static guide_cmd_t stop_cmd(const char *id, const char *session, int ch) {
  guide_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.action = GUIDE_ACTION_STOP;
  cmd.version = GUIDE_PROTOCOL_VERSION;
  cmd.channel = ch;
  snprintf(cmd.id, sizeof(cmd.id), "%s", id);
  snprintf(cmd.session, sizeof(cmd.session), "%s", session);
  snprintf(cmd.boot, sizeof(cmd.boot), "%s", BOOT);
  cmd.has_session = true;
  cmd.has_boot = true;
  return cmd;
}

static void test_start_turns_on(void) {
  guide_cmd_t cmd;
  guide_outcome_t out;
  setup();
  cmd = start_cmd(ID_A, SES_A, 3, 103000, 163000);
  out = guide_core_handle(&core, &cmd, 100000);
  assert(out.applied == GUIDE_APPLIED_ON);
  assert(strcmp(out.reason, GUIDE_REASON_OK) == 0);
  assert(out.channel == 3);
  assert(guide_core_session(&core, 3)->active);
  assert(guide_core_session(&core, 3)->end_ms == 163000);
}

static void test_duplicate_does_not_extend_ttl(void) {
  guide_cmd_t cmd;
  guide_outcome_t out;
  uint32_t original_end;
  setup();
  cmd = start_cmd(ID_A, SES_A, 2, 103000, 163000);
  (void)guide_core_handle(&core, &cmd, 100000);
  original_end = guide_core_session(&core, 2)->end_ms;

  /* Llega 40 s después: si el TTL se reprogramara, avanzaría. */
  out = guide_core_handle(&core, &cmd, 140000);
  assert(out.applied == GUIDE_APPLIED_ON);
  assert(out.duplicate);
  assert(guide_core_session(&core, 2)->end_ms == original_end);
}

static void test_stop_own_session(void) {
  guide_cmd_t cmd;
  guide_outcome_t out;
  setup();
  cmd = start_cmd(ID_A, SES_A, 1, 103000, 163000);
  (void)guide_core_handle(&core, &cmd, 100000);
  cmd = stop_cmd(ID_B, SES_A, 1);
  out = guide_core_handle(&core, &cmd, 110000);
  assert(out.applied == GUIDE_APPLIED_OFF);
  assert(!guide_core_session(&core, 1)->active);
}

static void test_stop_foreign_session_rejected(void) {
  guide_cmd_t cmd;
  guide_outcome_t out;
  setup();
  cmd = start_cmd(ID_A, SES_A, 1, 103000, 163000);
  (void)guide_core_handle(&core, &cmd, 100000);
  cmd = stop_cmd(ID_B, SES_B, 1);
  out = guide_core_handle(&core, &cmd, 110000);
  assert(out.applied == GUIDE_APPLIED_REJECTED);
  assert(strcmp(out.reason, GUIDE_REASON_STALE) == 0);
  assert(guide_core_session(&core, 1)->active);
}

static void test_second_owner_is_busy(void) {
  guide_cmd_t cmd;
  guide_outcome_t out;
  setup();
  cmd = start_cmd(ID_A, SES_A, 4, 103000, 163000);
  (void)guide_core_handle(&core, &cmd, 100000);
  cmd = start_cmd(ID_B, SES_B, 4, 103000, 163000);
  out = guide_core_handle(&core, &cmd, 101000);
  assert(out.applied == GUIDE_APPLIED_REJECTED);
  assert(strcmp(out.reason, GUIDE_REASON_BUSY) == 0);
  assert(strcmp(guide_core_session(&core, 4)->session, SES_A) == 0);
}

static void test_late_command_is_stale(void) {
  guide_cmd_t cmd;
  guide_outcome_t out;
  setup();
  cmd = start_cmd(ID_A, SES_A, 0, 103000, 163000);
  out = guide_core_handle(&core, &cmd, 104000);
  assert(out.applied == GUIDE_APPLIED_REJECTED);
  assert(strcmp(out.reason, GUIDE_REASON_STALE) == 0);
  assert(!guide_core_session(&core, 0)->active);
}

static void test_wrong_boot(void) {
  guide_cmd_t cmd;
  guide_outcome_t out;
  setup();
  cmd = start_cmd(ID_A, SES_A, 0, 103000, 163000);
  snprintf(cmd.boot, sizeof(cmd.boot), "deadbeef");
  out = guide_core_handle(&core, &cmd, 100000);
  assert(out.applied == GUIDE_APPLIED_REJECTED);
  assert(strcmp(out.reason, GUIDE_REASON_WRONG_BOOT) == 0);
}

static void test_channel_limits(void) {
  guide_cmd_t cmd;
  guide_outcome_t out;
  setup();
  cmd = start_cmd(ID_A, SES_A, 8, 103000, 163000);
  out = guide_core_handle(&core, &cmd, 100000);
  assert(out.applied == GUIDE_APPLIED_REJECTED);
  assert(strcmp(out.reason, GUIDE_REASON_BAD_CHANNEL) == 0);

  cmd = start_cmd(ID_B, SES_A, -1, 103000, 163000);
  out = guide_core_handle(&core, &cmd, 100000);
  assert(out.applied == GUIDE_APPLIED_REJECTED);
  assert(strcmp(out.reason, GUIDE_REASON_BAD_CHANNEL) == 0);
}

static void test_malformed_variants(void) {
  guide_cmd_t cmd;
  guide_outcome_t out;

  setup();
  cmd = start_cmd("short", SES_A, 0, 103000, 163000);
  out = guide_core_handle(&core, &cmd, 100000);
  assert(strcmp(out.reason, GUIDE_REASON_MALFORMED) == 0);

  setup();
  cmd = start_cmd(ID_A, SES_A, 0, 103000, 163000);
  cmd.version = 2;
  out = guide_core_handle(&core, &cmd, 100000);
  assert(strcmp(out.reason, GUIDE_REASON_MALFORMED) == 0);

  setup();
  cmd = start_cmd(ID_A, "no-es-uuid", 0, 103000, 163000);
  out = guide_core_handle(&core, &cmd, 100000);
  assert(strcmp(out.reason, GUIDE_REASON_MALFORMED) == 0);

  setup();
  cmd = start_cmd(ID_A, SES_A, 0, 103000, 103000);
  out = guide_core_handle(&core, &cmd, 100000);
  assert(strcmp(out.reason, GUIDE_REASON_MALFORMED) == 0);

  setup();
  cmd = start_cmd(ID_A, SES_A, 0, 100000, 200000); /* 100 s > 60 s */
  out = guide_core_handle(&core, &cmd, 100000);
  assert(strcmp(out.reason, GUIDE_REASON_MALFORMED) == 0);
}

static void test_expire_and_no_replay(void) {
  guide_cmd_t cmd;
  guide_outcome_t out;
  guide_expiry_t expired[GUIDE_MAX_CHANNELS];
  uint8_t count;
  setup();
  cmd = start_cmd(ID_A, SES_A, 6, 103000, 163000);
  (void)guide_core_handle(&core, &cmd, 100000);

  count = guide_core_expire(&core, 162999, expired, GUIDE_MAX_CHANNELS);
  assert(count == 0);
  assert(guide_core_session(&core, 6)->active);

  count = guide_core_expire(&core, 163000, expired, GUIDE_MAX_CHANNELS);
  assert(count == 1);
  assert(expired[0].channel == 6);
  assert(strcmp(expired[0].session, SES_A) == 0);
  assert(!guide_core_session(&core, 6)->active);

  count = guide_core_expire(&core, 170000, expired, GUIDE_MAX_CHANNELS);
  assert(count == 0);

  /* Repetir la orden ya vencida no vuelve a encender: la caché devuelve el
   * apagado por TTL, no un "on" nuevo. */
  out = guide_core_handle(&core, &cmd, 164000);
  assert(out.applied == GUIDE_APPLIED_OFF);
  assert(strcmp(out.reason, GUIDE_REASON_TTL) == 0);
  assert(!guide_core_session(&core, 6)->active);

  /* Fuera de la caché de deduplicación tampoco revive: accept_ms ya pasó. */
  out = guide_core_handle(&core, &cmd, 240000);
  assert(out.applied == GUIDE_APPLIED_REJECTED);
  assert(strcmp(out.reason, GUIDE_REASON_STALE) == 0);
  assert(!guide_core_session(&core, 6)->active);
}

static void test_capacity_is_clamped(void) {
  guide_core_init(&core, BOOT, 200);
  assert(core.capacity == GUIDE_MAX_CHANNELS);
  guide_core_init(&core, BOOT, 0);
  assert(core.capacity == 0);
}

static void test_clear_and_fail(void) {
  guide_cmd_t cmd;
  guide_outcome_t out;
  setup();
  cmd = start_cmd(ID_A, SES_A, 5, 103000, 163000);
  (void)guide_core_handle(&core, &cmd, 100000);
  guide_core_fail(&core, 5, ID_A, 101000);
  assert(!guide_core_session(&core, 5)->active);
  assert(core.last_applied == GUIDE_APPLIED_ERROR);
  assert(strcmp(core.last_reason, GUIDE_REASON_GPIO_ERROR) == 0);

  /* Un duplicado tras el fallo no reintenta la salida: devuelve el error. */
  out = guide_core_handle(&core, &cmd, 102000);
  assert(out.applied == GUIDE_APPLIED_ERROR);
  assert(strcmp(out.reason, GUIDE_REASON_GPIO_ERROR) == 0);
  assert(!guide_core_session(&core, 5)->active);

  cmd = start_cmd(ID_B, SES_A, 5, 110000, 170000);
  (void)guide_core_handle(&core, &cmd, 105000);
  assert(guide_core_session(&core, 5)->active);
  guide_core_clear(&core);
  assert(!guide_core_session(&core, 5)->active);
  assert(!core.last_valid);
}

static void test_uint32_wrap(void) {
  guide_cmd_t cmd;
  guide_outcome_t out;
  /* Uptime cerca del desbordamiento: la aritmética con signo debe seguir bien. */
  setup();
  cmd = start_cmd(ID_A, SES_A, 7, 0x00000010U, 0x0000EA70U);
  out = guide_core_handle(&core, &cmd, 0xFFFFFFF0U);
  assert(out.applied == GUIDE_APPLIED_ON);
  assert(core.sessions[7].end_ms == 0x0000EA70U);
}

static void expect_parse(const char *json, guide_parse_status_t expected) {
  guide_cmd_t cmd;
  guide_parse_status_t got = guide_parse_command(json, strlen(json), &cmd);
  if (got != expected) {
    fprintf(stderr, "parse: esperado %d, obtenido %d en %s\n", (int)expected,
            (int)got, json);
    assert(0);
  }
}

static void test_parse_contract_examples(void) {
  guide_cmd_t cmd;
  const char *start =
      "{\"v\":1,\"t\":\"guide.start\",\"id\":\"c1f4a9e2-3b70-4d15-8a63-0e2c9f7b1d40\","
      "\"session\":\"d2b6f0a1-1111-4222-8333-444455556666\",\"ch\":2,"
      "\"boot\":\"1f3ac9d2\",\"accept_ms\":172340,\"end_ms\":232340}";
  assert(guide_parse_command(start, strlen(start), &cmd) == GUIDE_PARSE_OK);
  assert(cmd.action == GUIDE_ACTION_START);
  assert(cmd.version == 1);
  assert(cmd.channel == 2);
  assert(cmd.has_session && cmd.has_boot && cmd.has_accept && cmd.has_end);
  assert(cmd.accept_ms == 172340 && cmd.end_ms == 232340);
  assert(!strcmp(cmd.boot, "1f3ac9d2"));

  /* Espacios y orden distinto de claves siguen siendo JSON válido. */
  const char *spaced =
      "{ \"t\" : \"guide.stop\" , \"boot\":\"1f3ac9d2\", \"ch\": 0, "
      "\"id\":\"cmd-00000009\", \"session\":\"d2b6f0a1-1111-4222-8333-444455556666\","
      "\"v\":1 }";
  assert(guide_parse_command(spaced, strlen(spaced), &cmd) == GUIDE_PARSE_OK);
  assert(cmd.action == GUIDE_ACTION_STOP);
  assert(cmd.channel == 0);

  const char *status =
      "{\"v\":1,\"t\":\"guide.status\",\"id\":\"cmd-0000000a\"}";
  assert(guide_parse_command(status, strlen(status), &cmd) == GUIDE_PARSE_OK);
  assert(cmd.action == GUIDE_ACTION_STATUS);
}

static void test_parse_rejects_bad_shapes(void) {
  /* Campo extra, falta de campo obligatorio, claves duplicadas. */
  expect_parse("{\"v\":1,\"t\":\"guide.status\",\"id\":\"cmd-0000000a\",\"gpio\":10}",
               GUIDE_PARSE_MALFORMED);
  expect_parse("{\"v\":1,\"t\":\"guide.start\",\"id\":\"cmd-0000000a\","
               "\"session\":\"d2b6f0a1-1111-4222-8333-444455556666\",\"ch\":2,"
               "\"boot\":\"1f3ac9d2\",\"accept_ms\":172340}",
               GUIDE_PARSE_MALFORMED);
  expect_parse("{\"v\":1,\"v\":1,\"t\":\"guide.status\",\"id\":\"cmd-0000000a\"}",
               GUIDE_PARSE_MALFORMED);
  expect_parse("{\"v\":1,\"t\":\"guide.start\",\"id\":\"cmd-0000000a\","
               "\"session\":\"d2b6f0a1-1111-4222-8333-444455556666\",\"ch\":2,"
               "\"boot\":\"1f3ac9d2\",\"accept_ms\":172340,\"end_ms\":232340,"
               "\"extra\":1}",
               GUIDE_PARSE_MALFORMED);

  /* Tipos incorrectos: cadena donde va entero, decimal, booleano, negativo. */
  expect_parse("{\"v\":1,\"t\":\"guide.start\",\"id\":\"cmd-0000000a\","
               "\"session\":\"d2b6f0a1-1111-4222-8333-444455556666\",\"ch\":\"2\","
               "\"boot\":\"1f3ac9d2\",\"accept_ms\":172340,\"end_ms\":232340}",
               GUIDE_PARSE_MALFORMED);
  expect_parse("{\"v\":1,\"t\":\"guide.start\",\"id\":\"cmd-0000000a\","
               "\"session\":\"d2b6f0a1-1111-4222-8333-444455556666\",\"ch\":2.5,"
               "\"boot\":\"1f3ac9d2\",\"accept_ms\":172340,\"end_ms\":232340}",
               GUIDE_PARSE_MALFORMED);
  expect_parse("{\"v\":1,\"t\":\"guide.start\",\"id\":\"cmd-0000000a\","
               "\"session\":\"d2b6f0a1-1111-4222-8333-444455556666\",\"ch\":true,"
               "\"boot\":\"1f3ac9d2\",\"accept_ms\":172340,\"end_ms\":232340}",
               GUIDE_PARSE_MALFORMED);
  expect_parse("{\"v\":1,\"t\":\"guide.start\",\"id\":\"cmd-0000000a\","
               "\"session\":\"d2b6f0a1-1111-4222-8333-444455556666\",\"ch\":2,"
               "\"boot\":\"1f3ac9d2\",\"accept_ms\":-1,\"end_ms\":232340}",
               GUIDE_PARSE_MALFORMED);
  expect_parse("{\"v\":1,\"t\":\"guide.start\",\"id\":123,\"session\":\"d2b6f0a1-1111-4222-8333-444455556666\","
               "\"ch\":2,\"boot\":\"1f3ac9d2\",\"accept_ms\":172340,\"end_ms\":232340}",
               GUIDE_PARSE_MALFORMED);

  /* Acción desconocida, JSON truncado, basura final y objeto anidado. */
  expect_parse("{\"v\":1,\"t\":\"guide.explode\",\"id\":\"cmd-0000000a\"}",
               GUIDE_PARSE_MALFORMED);
  expect_parse("{\"v\":1,\"t\":\"guide.status\"", GUIDE_PARSE_MALFORMED);
  expect_parse("{\"v\":1,\"t\":\"guide.status\",\"id\":\"cmd-0000000a\"} x",
               GUIDE_PARSE_MALFORMED);
  expect_parse("{\"v\":1,\"t\":\"guide.status\",\"id\":{\"a\":1}}",
               GUIDE_PARSE_MALFORMED);
  expect_parse("", GUIDE_PARSE_MALFORMED);
  expect_parse("[]", GUIDE_PARSE_MALFORMED);
}

static void test_parse_size_limit(void) {
  guide_cmd_t cmd;
  char big[GUIDE_JSON_MAX + 2];
  memset(big, ' ', sizeof(big));
  big[0] = '{';
  big[1] = '}';
  assert(guide_parse_command(big, GUIDE_JSON_MAX, &cmd) == GUIDE_PARSE_MALFORMED ||
         guide_parse_command(big, GUIDE_JSON_MAX, &cmd) == GUIDE_PARSE_OK);
  assert(guide_parse_command(big, GUIDE_JSON_MAX + 1U, &cmd) == GUIDE_PARSE_TOO_BIG);
}

static void expect_light_test(const char *json, bool expected) {
  guide_light_test_t test;
  bool got = guide_parse_light_test(json, strlen(json), &test);
  if (got != expected) {
    fprintf(stderr, "light.test: esperado %d, obtenido %d en %s\n", (int)expected,
            (int)got, json);
    assert(0);
  }
}

static void test_parse_light_test(void) {
  /* Mensaje exacto que publica la Raspberry (mqtt_bridge.test_light). */
  const char *rpi =
      "{\"type\": \"light.test\", \"channel\": 5, \"duration_ms\": 5000}";
  guide_light_test_t test;
  assert(guide_parse_light_test(rpi, strlen(rpi), &test));
  assert(test.channel == 5 && test.duration_ms == 5000);

  const char *swapped =
      "{\"duration_ms\":100,\"channel\":0,\"type\":\"light.test\"}";
  assert(guide_parse_light_test(swapped, strlen(swapped), &test));
  assert(test.channel == 0 && test.duration_ms == 100);

  expect_light_test("{\"type\":\"light.test\",\"channel\":5}", false);
  expect_light_test("{\"type\":\"light.test\",\"channel\":5,\"duration_ms\":5000,"
                    "\"id\":\"x\"}", false);
  expect_light_test("{\"type\":\"light.test\",\"channel\":5,\"duration_ms\":99}",
                    false);
  expect_light_test("{\"type\":\"light.test\",\"channel\":5,\"duration_ms\":30001}",
                    false);
  expect_light_test("{\"type\":\"light.test\",\"channel\":\"5\",\"duration_ms\":5000}",
                    false);
  expect_light_test("{\"type\":\"light.test\",\"type\":\"light.test\",\"channel\":5,"
                    "\"duration_ms\":5000}", false);
  expect_light_test("{\"type\":\"guide.start\",\"channel\":5,\"duration_ms\":5000}",
                    false);
  expect_light_test("{\"type\":\"light.test\",\"channel\":5,\"duration_ms\":5000} x",
                    false);
  expect_light_test("", false);
}

static void test_light_test_session(void) {
  /* Misma construcción que run_light_test: aceptación inmediata (+1 ms) y TTL
   * igual a la duración pedida desde ese instante. */
  guide_cmd_t cmd;
  guide_outcome_t out;
  guide_expiry_t exp[1];
  setup();
  cmd = start_cmd("lt-000186a0-0001", SES_A, 5, 100001, 105001);
  out = guide_core_handle(&core, &cmd, 100000);
  assert(out.applied == GUIDE_APPLIED_ON);
  assert(guide_core_session(&core, 5)->active);

  /* Antes del plazo no se apaga; al cumplirlo sí, y queda el motivo. */
  assert(guide_core_expire(&core, 105000, exp, 1) == 0);
  assert(guide_core_expire(&core, 105001, exp, 1) == 1);
  assert(exp[0].channel == 5);
  assert(!guide_core_session(&core, 5)->active);
}

int main(void) {
  test_start_turns_on();
  test_duplicate_does_not_extend_ttl();
  test_stop_own_session();
  test_stop_foreign_session_rejected();
  test_second_owner_is_busy();
  test_late_command_is_stale();
  test_wrong_boot();
  test_channel_limits();
  test_malformed_variants();
  test_expire_and_no_replay();
  test_capacity_is_clamped();
  test_clear_and_fail();
  test_uint32_wrap();
  test_parse_contract_examples();
  test_parse_rejects_bad_shapes();
  test_parse_size_limit();
  test_parse_light_test();
  test_light_test_session();
  printf("guide: 18/18 pruebas de host conformes (lógica + parser)\n");
  return 0;
}
