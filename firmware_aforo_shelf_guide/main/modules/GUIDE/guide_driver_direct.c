/* Driver del piloto: un GPIO por canal.
 *
 * Montaje activo en alto: GPIO -> resistencia 220 ohm -> ánodo -> GND. En
 * reset el GPIO queda en alta impedancia, así que el LED arranca apagado.
 * Lista acordada en GL-0/GL-1: 0,1,3,4,5,6,7,10 (evita strapping 2/8/9 y el
 * SPI flash 12..17). */

#include "guide_driver.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "guide_direct";
static const uint8_t PINS[] = {0U, 1U, 3U, 4U, 5U, 6U, 7U, 10U};
static bool s_ready;

static void all_off(void) {
  for (size_t i = 0; i < sizeof(PINS) / sizeof(PINS[0]); ++i)
    gpio_set_level(PINS[i], 0);
}

static esp_err_t init(void) {
  gpio_config_t cfg = {
      .pin_bit_mask = 0,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  for (size_t i = 0; i < sizeof(PINS) / sizeof(PINS[0]); ++i)
    cfg.pin_bit_mask |= (1ULL << PINS[i]);
  esp_err_t err = gpio_config(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "No se pudieron configurar las salidas: %s", esp_err_to_name(err));
    return err;
  }
  /* Salidas en bajo antes de cualquier red: la guía nunca arranca encendida. */
  all_off();
  s_ready = true;
  ESP_LOGI(TAG, "Driver directo listo: %u canales", (unsigned)sizeof(PINS));
  return ESP_OK;
}

static esp_err_t set(uint8_t channel, bool on) {
  if (!s_ready)
    return ESP_ERR_INVALID_STATE;
  if (channel >= sizeof(PINS) / sizeof(PINS[0]))
    return ESP_ERR_INVALID_ARG;
  return gpio_set_level(PINS[channel], on ? 1 : 0);
}

const guide_driver_t guide_driver_direct = {
    .name = "direct",
    .capacity = (uint8_t)(sizeof(PINS) / sizeof(PINS[0])),
    .map = PINS,
    .init = init,
    .set = set,
    .all_off = all_off,
};
