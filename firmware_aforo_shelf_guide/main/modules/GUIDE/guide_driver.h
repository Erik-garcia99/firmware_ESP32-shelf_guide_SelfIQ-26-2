#ifndef SELFIQ_GUIDE_DRIVER_H
#define SELFIQ_GUIDE_DRIVER_H

/* Interfaz de salida física. La guía siempre trabaja con canales lógicos
 * (led_index); el driver traduce a GPIO o a un registro de desplazamiento. */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
  const char *name;      /* "direct" | "hc595" (contrato §7.5) */
  uint8_t capacity;      /* canales utilizables */
  const uint8_t *map;    /* canal lógico -> GPIO o salida del registro */
  esp_err_t (*init)(void);
  esp_err_t (*set)(uint8_t channel, bool on);
  void (*all_off)(void);
} guide_driver_t;

extern const guide_driver_t guide_driver_direct;
extern const guide_driver_t guide_driver_hc595;

#endif /* SELFIQ_GUIDE_DRIVER_H */
