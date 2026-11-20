/* Control de la guía LED: validación, timer monotónico y resultados.
 * El temporizador es independiente de la reconexión MQTT: el LED se apaga por
 * TTL aunque el bucle de red esté esperando 60 s. */

#include "guide_control.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "guide_core.h"
#include "guide_driver.h"

/* Montaje del piloto. Cambiar a 1 para el respaldo con 74HC595. */
#define GUIDE_USE_HC595 0

#if GUIDE_USE_HC595
static const guide_driver_t *const s_driver = &guide_driver_hc595;
#else
static const guide_driver_t *const s_driver = &guide_driver_direct;
#endif

#define GUIDE_TICK_US 50000LL

static const char *TAG = "guide_control";

static guide_core_t s_core;
static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_timer;
static guide_publish_fn s_publish;
static char s_boot[GUIDE_BOOT_SIZE];

static uint32_t now_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static void new_boot_id(char *out, size_t size) {
  uint32_t value = 0;
  esp_fill_random(&value, sizeof(value));
  snprintf(out, size, "%08lx", (unsigned long)value);
}

static void copy_field(char *dst, size_t size, const char *src) {
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

static void emit_result(const char *id, const char *session, uint8_t channel,
                        guide_applied_t applied, const char *reason) {
  guide_publish_fn publish = s_publish;
  char json[GUIDE_JSON_MAX];
  char boot[GUIDE_BOOT_SIZE];
  int len;

  if (!publish || !id || !*id)
    return;
  if (s_lock) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    copy_field(boot, sizeof(boot), s_boot);
    xSemaphoreGive(s_lock);
  } else {
    copy_field(boot, sizeof(boot), s_boot);
  }
  len = snprintf(json, sizeof(json),
                 "{\"v\":1,\"t\":\"guide.result\",\"id\":\"%s\",\"session\":\"%s\","
                 "\"ch\":%u,\"boot\":\"%s\",\"applied\":\"%s\",\"reason\":\"%s\","
                 "\"uptime_ms\":%lu}",
                 id, session ? session : "", (unsigned)channel, boot,
                 guide_applied_text(applied), reason,
                 (unsigned long)now_ms());
  if (len <= 0 || (size_t)len >= sizeof(json)) {
    ESP_LOGE(TAG, "Resultado no cabe en %u bytes; no se publica",
             (unsigned)GUIDE_JSON_MAX);
    return;
  }
  if (publish(json, len) != ESP_OK)
    ESP_LOGW(TAG, "Resultado no publicado (sin red); el estado sigue consultable");
}

static void timer_cb(void *arg) {
  guide_expiry_t expired[GUIDE_MAX_CHANNELS];
  uint8_t count;

  (void)arg;
  if (!s_lock)
    return;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  count = guide_core_expire(&s_core, now_ms(), expired, GUIDE_MAX_CHANNELS);
  xSemaphoreGive(s_lock);

  for (uint8_t i = 0; i < count; ++i) {
    esp_err_t err = s_driver->set(expired[i].channel, false);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "TTL del canal %u no pudo apagar la salida: %s",
               (unsigned)expired[i].channel, esp_err_to_name(err));
      emit_result(expired[i].id, expired[i].session, expired[i].channel,
                  GUIDE_APPLIED_ERROR, GUIDE_REASON_GPIO_ERROR);
      continue;
    }
    ESP_LOGI(TAG, "TTL cumplido: canal %u apagado (sesión %.8s…)",
             (unsigned)expired[i].channel, expired[i].session);
    emit_result(expired[i].id, expired[i].session, expired[i].channel,
                GUIDE_APPLIED_OFF, GUIDE_REASON_TTL);
  }
}

esp_err_t guide_control_init(void) {
  esp_err_t err;

  if (s_lock)
    return ESP_OK;
  s_lock = xSemaphoreCreateMutex();
  if (!s_lock)
    return ESP_ERR_NO_MEM;

  new_boot_id(s_boot, sizeof(s_boot));

  err = s_driver->init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Driver %s no disponible: %s; guías deshabilitadas",
             s_driver->name, esp_err_to_name(err));
  }
  guide_core_init(&s_core, s_boot, err == ESP_OK ? s_driver->capacity : 0U);

  const esp_timer_create_args_t args = {
      .callback = timer_cb,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "guide_ttl",
  };
  err = esp_timer_create(&args, &s_timer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "No se pudo crear el temporizador de TTL: %s", esp_err_to_name(err));
    /* Sin temporizador no hay apagado garantizado: se rechaza toda guía. */
    guide_core_init(&s_core, s_boot, 0U);
    return err;
  }
  err = esp_timer_start_periodic(s_timer, GUIDE_TICK_US);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "No se pudo iniciar el temporizador de TTL: %s", esp_err_to_name(err));
    guide_core_init(&s_core, s_boot, 0U);
    return err;
  }

  ESP_LOGI(TAG, "Guía lista: driver=%s canales=%u boot=%s", s_driver->name,
           (unsigned)s_core.capacity, s_boot);
  return ESP_OK;
}

void guide_control_set_publisher(guide_publish_fn publish) {
  s_publish = publish;
}

static void run_command(guide_cmd_t *cmd) {
  guide_outcome_t outcome;

  xSemaphoreTake(s_lock, portMAX_DELAY);
  outcome = guide_core_handle(&s_core, cmd, now_ms());
  xSemaphoreGive(s_lock);

  /* La salida se aplica antes de emitir: un error de GPIO nunca es éxito. */
  if (outcome.applied == GUIDE_APPLIED_ON || outcome.applied == GUIDE_APPLIED_OFF) {
    esp_err_t err =
        s_driver->set(outcome.channel, outcome.applied == GUIDE_APPLIED_ON);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Canal %u no aplicado: %s", (unsigned)outcome.channel,
               esp_err_to_name(err));
      xSemaphoreTake(s_lock, portMAX_DELAY);
      guide_core_fail(&s_core, outcome.channel, cmd->id, now_ms());
      xSemaphoreGive(s_lock);
      emit_result(cmd->id, cmd->session, outcome.channel, GUIDE_APPLIED_ERROR,
                  GUIDE_REASON_GPIO_ERROR);
      return;
    }
  }

  if (outcome.applied == GUIDE_APPLIED_REJECTED) {
    ESP_LOGW(TAG, "Orden %s rechazada: %s (canal %d)", guide_action_text(cmd->action),
             outcome.reason, cmd->channel);
  }
  emit_result(cmd->id, cmd->session, outcome.channel, outcome.applied,
              outcome.reason);
}

/* Prueba staff: mismo encendido y mismo TTL, pero con id/sesión sintéticos
 * porque `light.test` no trae ninguno. */
static void run_light_test(const guide_light_test_t *test) {
  static unsigned sequence;
  guide_cmd_t cmd;
  uint32_t now = now_ms();
  unsigned seq = sequence++ & 0xFFFFU;

  memset(&cmd, 0, sizeof(cmd));
  cmd.action = GUIDE_ACTION_START;
  cmd.version = GUIDE_PROTOCOL_VERSION;
  cmd.channel = (int)test->channel;
  /* `accept_ms` es el fin de la ventana de aceptación: se acepta en el acto
   * (+1 ms) y el TTL de la prueba cuenta desde ahí. */
  cmd.accept_ms = now + 1U;
  cmd.end_ms = cmd.accept_ms + test->duration_ms;
  cmd.has_accept = true;
  cmd.has_end = true;
  cmd.has_boot = true;
  cmd.has_session = true;

  xSemaphoreTake(s_lock, portMAX_DELAY);
  copy_field(cmd.boot, sizeof(cmd.boot), s_boot);
  xSemaphoreGive(s_lock);

  snprintf(cmd.id, sizeof(cmd.id), "lt-%08lx-%04x", (unsigned long)now, seq);
  snprintf(cmd.session, sizeof(cmd.session),
           "00000000-0000-4000-8000-%08lx%04x", (unsigned long)now, seq);

  ESP_LOGI(TAG, "Prueba de luz: canal %u durante %lu ms",
           (unsigned)test->channel, (unsigned long)test->duration_ms);
  run_command(&cmd);
}

void guide_control_handle_payload(const char *data, int data_len, int total_len,
                                  int offset, bool retain) {
  guide_cmd_t cmd;
  guide_parse_status_t parsed;

  if (!data || data_len <= 0)
    return;
  if (!s_lock || s_core.capacity == 0U) {
    ESP_LOGW(TAG, "Guía no disponible; orden descartada");
    return;
  }
  if (retain) {
    ESP_LOGW(TAG, "Orden retained descartada");
    return;
  }
  if (offset != 0 || total_len != data_len) {
    ESP_LOGW(TAG, "Orden fragmentada descartada (%d de %d bytes)", data_len,
             total_len);
    return;
  }
  if (data_len > (int)GUIDE_JSON_MAX) {
    ESP_LOGW(TAG, "Orden de %d bytes supera el máximo del contrato", data_len);
    return;
  }

  /* Sin `id` utilizable no hay forma de emitir un resultado conforme: la orden
   * se descarta y el gateway la cierra por plazo máximo. */
  parsed = guide_parse_command(data, (size_t)data_len, &cmd);
  if (parsed != GUIDE_PARSE_OK) {
    guide_light_test_t test;
    if (guide_parse_light_test(data, (size_t)data_len, &test)) {
      run_light_test(&test);
      return;
    }
    ESP_LOGW(TAG, "Orden rechazada sin aplicar: %s",
             parsed == GUIDE_PARSE_TOO_BIG ? "demasiado grande" : "mal formada");
    return;
  }

  if (cmd.action == GUIDE_ACTION_STATUS) {
    guide_control_publish_state(cmd.id);
    return;
  }

  run_command(&cmd);
}

static void build_channels_map(char *out, size_t size, uint8_t capacity) {
  size_t pos = 0;
  out[0] = '\0';
  for (uint8_t channel = 0; channel < capacity && pos + 4U < size; ++channel) {
    uint8_t physical = s_driver->map ? s_driver->map[channel] : channel;
    pos += (size_t)snprintf(out + pos, size - pos, "%s%u", channel ? "," : "",
                            (unsigned)physical);
  }
}

void guide_control_publish_capabilities(void) {
  guide_publish_fn publish = s_publish;
  char map[64];
  char json[GUIDE_JSON_MAX];
  char boot[GUIDE_BOOT_SIZE];
  int len;

  if (!publish)
    return;
  if (s_core.capacity == 0) {
    ESP_LOGW(TAG, "Sin canales utilizables: no se publican capacidades");
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  copy_field(boot, sizeof(boot), s_boot);
  xSemaphoreGive(s_lock);
  build_channels_map(map, sizeof(map), s_core.capacity);
  len = snprintf(json, sizeof(json),
                 "{\"v\":1,\"t\":\"guide.capabilities\",\"boot\":\"%s\","
                 "\"driver\":\"%s\",\"channels\":%u,\"channels_map\":[%s],"
                 "\"fw\":\"%s\"}",
                 boot, s_driver->name, (unsigned)s_core.capacity, map,
                 GUIDE_FW_VERSION);
  if (len <= 0 || (size_t)len >= sizeof(json)) {
    ESP_LOGE(TAG, "Capacidades no caben en %u bytes", (unsigned)GUIDE_JSON_MAX);
    return;
  }
  if (publish(json, len) != ESP_OK)
    ESP_LOGW(TAG, "Capacidades no publicadas: sin conexión MQTT");
}

void guide_control_publish_state(const char *command_id) {
  guide_publish_fn publish = s_publish;
  char json[GUIDE_JSON_MAX];
  char boot[GUIDE_BOOT_SIZE];
  char on[GUIDE_MAX_CHANNELS * 4U];
  size_t pos = 0;
  uint8_t last_channel = 0;
  const char *last_reason = GUIDE_REASON_OK;

  if (!publish || !command_id || !*command_id)
    return;

  on[0] = '\0';
  xSemaphoreTake(s_lock, portMAX_DELAY);
  copy_field(boot, sizeof(boot), s_boot);
  last_channel = s_core.last_channel;
  last_reason = s_core.last_valid ? s_core.last_reason : GUIDE_REASON_OK;
  for (uint8_t channel = 0; channel < s_core.capacity && pos + 5U < sizeof(on);
       ++channel) {
    if (!s_core.sessions[channel].active)
      continue;
    pos += (size_t)snprintf(on + pos, sizeof(on) - pos, "%s%u",
                            pos ? "," : "", (unsigned)channel);
  }
  xSemaphoreGive(s_lock);

  int len = snprintf(json, sizeof(json),
                     "{\"v\":1,\"t\":\"guide.state\",\"id\":\"%s\",\"boot\":\"%s\","
                     "\"uptime_ms\":%lu,\"on\":[%s],\"last_ch\":%u,\"last\":\"%s\"}",
                     command_id, boot, (unsigned long)now_ms(), on,
                     (unsigned)last_channel, last_reason ? last_reason : GUIDE_REASON_OK);
  if (len <= 0 || (size_t)len >= sizeof(json)) {
    ESP_LOGE(TAG, "Estado no cabe en %u bytes", (unsigned)GUIDE_JSON_MAX);
    return;
  }
  if (publish(json, len) != ESP_OK)
    ESP_LOGW(TAG, "Estado no publicado: sin conexión MQTT");
}

void guide_control_identity_reset(void) {
  if (!s_lock)
    return;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_driver->all_off();
  guide_core_clear(&s_core);
  new_boot_id(s_boot, sizeof(s_boot));
  guide_core_set_boot(&s_core, s_boot);
  xSemaphoreGive(s_lock);
  ESP_LOGI(TAG, "Identidad restablecida: salidas apagadas, boot=%s", s_boot);
}

const char *guide_control_boot_id(void) { return s_boot; }

uint8_t guide_control_capacity(void) { return s_core.capacity; }
