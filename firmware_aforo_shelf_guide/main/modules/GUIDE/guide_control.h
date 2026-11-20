#ifndef SELFIQ_GUIDE_CONTROL_H
#define SELFIQ_GUIDE_CONTROL_H

/* Puente entre MQTT y el control LED. La capa de red solo entrega el payload;
 * aquí se valida, se aplica la salida y se emite el resultado. */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* Publica un JSON ya construido en el tópico de resultados (command/ack).
 * Devuelve ESP_ERR_INVALID_STATE si no hay conexión: el resultado no se
 * reencola, pero el apagado y el estado consultable siguen siendo válidos. */
typedef esp_err_t (*guide_publish_fn)(const char *json, int len);

/* Debe llamarse antes de Wi-Fi/MQTT: deja las salidas apagadas. */
esp_err_t guide_control_init(void);

void guide_control_set_publisher(guide_publish_fn publish);

/* Payload recibido en `selfiq/<branch>/<node>/shelf_monitor/command`. */
void guide_control_handle_payload(const char *data, int data_len, int total_len,
                                  int offset, bool retain);

/* Se publica tras registrarse y al cambiar de identidad. */
void guide_control_publish_capabilities(void);

/* Respuesta a `guide.status` (estado consultable al volver la red). */
void guide_control_publish_state(const char *command_id);

/* node.reset: apaga todo y estrena boot_id (las órdenes viejas dejan de valer). */
void guide_control_identity_reset(void);

const char *guide_control_boot_id(void);
uint8_t guide_control_capacity(void);

#endif /* SELFIQ_GUIDE_CONTROL_H */
