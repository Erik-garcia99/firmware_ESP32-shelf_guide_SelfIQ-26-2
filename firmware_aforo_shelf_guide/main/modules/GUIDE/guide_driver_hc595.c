/* Driver de respaldo: 74HC595 a 3.3 V.
 *
 * SER=GPIO4, SRCLK=GPIO5, RCLK=GPIO6. Cada bit del byte de estado corresponde
 * a una salida (bit 0 = QA ... bit 7 = QH), que es lo que declara
 * `channels_map` como 0..7. OE va a GND y SRCLR a 3.3 V según GL-0; si en
 * banco aparece un destello al energizar, se corrige el montaje (OE a GPIO con
 * pull-up o RC en SRCLR), no el protocolo. */

#include "guide_driver.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#define PIN_SER 4
#define PIN_SRCLK 5
#define PIN_RCLK 6
#define SHIFT_BITS 8U

static const char *TAG = "guide_hc595";
static const uint8_t OUTPUTS[] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
static bool s_ready;
static uint8_t s_state;

static void latch(void) {
  gpio_set_level(PIN_RCLK, 0);
  esp_rom_delay_us(2);
  gpio_set_level(PIN_RCLK, 1);
}

static void shift_out(uint8_t value) {
  for (uint8_t bit = 0; bit < SHIFT_BITS; ++bit) {
    gpio_set_level(PIN_SER, (value >> bit) & 1U);
    gpio_set_level(PIN_SRCLK, 0);
    esp_rom_delay_us(2);
    gpio_set_level(PIN_SRCLK, 1);
  }
  latch();
}

static void all_off(void) {
  s_state = 0;
  if (s_ready)
    shift_out(s_state);
}

static esp_err_t init(void) {
  gpio_config_t cfg = {
      .pin_bit_mask = (1ULL << PIN_SER) | (1ULL << PIN_SRCLK) | (1ULL << PIN_RCLK),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  esp_err_t err = gpio_config(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "No se pudieron configurar SER/SRCLK/RCLK: %s", esp_err_to_name(err));
    return err;
  }
  gpio_set_level(PIN_SER, 0);
  gpio_set_level(PIN_SRCLK, 0);
  gpio_set_level(PIN_RCLK, 0);
  s_ready = true;
  all_off();
  ESP_LOGI(TAG, "Driver 74HC595 listo: %u canales", (unsigned)SHIFT_BITS);
  return ESP_OK;
}

static esp_err_t set(uint8_t channel, bool on) {
  if (!s_ready)
    return ESP_ERR_INVALID_STATE;
  if (channel >= SHIFT_BITS)
    return ESP_ERR_INVALID_ARG;
  uint8_t next = on ? (uint8_t)(s_state | (1U << channel))
                    : (uint8_t)(s_state & ~(1U << channel));
  s_state = next;
  shift_out(next);
  return ESP_OK;
}

const guide_driver_t guide_driver_hc595 = {
    .name = "hc595",
    .capacity = (uint8_t)SHIFT_BITS,
    .map = OUTPUTS,
    .init = init,
    .set = set,
    .all_off = all_off,
};
