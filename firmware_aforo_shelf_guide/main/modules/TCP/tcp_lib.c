/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor Erik Garcia Chavez
 * @date octuber 2026
 * @assginature : proyecto de carrera
 * @teacher : Jose Isabel Garcia Rocha
 *
 ************************************************* */

// librerias estandar

// liberias estanadar
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <errno.h>
// logs
#include "esp_err.h"
#include "esp_log.h"

// freertos
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// librerias propias

#include "global.h"
#include "modules/TCP/tcp_lib.h"

//+++++++++++++++++++++ varibales globales
const char *TAG = "TCP_CLIENT : ";

esp_err_t tcp_cliente_init() {

  // reinicamos todo antes de inciar
  tcp_client.connected = 0;
  if (tcp_client.sock >= 0) {
    close(tcp_client.sock);
    tcp_client.sock = -1;
  }

  for (int n_retry = 0; n_retry < 5; n_retry++) {

    ESP_LOGI(TAG, "intento %d de establecer conexion", n_retry);

    // creamos el descriptor del socket
    tcp_client.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcp_client.sock < 0) {
      ESP_LOGE(TAG, "error al crear el descriptor del socket(): %d", errno);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue; // saltamos todo y se vuelve a intentar.
    }
    // timeout de recv
    struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(tcp_client.sock, SOL_SOCKET, SO_RCVTIMEO, &timeout,
               sizeof(timeout));

    // direccion del servidor
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(tcp_client.host_port),
    };

    inet_pton(AF_INET, tcp_client.host_ip, &server_addr.sin_addr);

    // conectar
    if (connect(tcp_client.sock, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
      ESP_LOGE(TAG, "descriptor <connect()> fallo  %d", errno);
      close(tcp_client.sock);
      tcp_client.sock = -1;
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    // exito, salir de inmediato
    tcp_client.connected = 1;

    ESP_LOGI(TAG, "EXITO!, conectado a %s:%hu", tcp_client.host_ip,
             tcp_client.host_port);
    return ESP_OK;
  }
  ESP_LOGI(TAG, "fail!, no se pudo conectar al %s:%hu", tcp_client.host_ip,
           tcp_client.host_port);
  // se agotaron los 5 intentos
  tcp_client.connected = 0;
  tcp_client.sock = -1;
  return ESP_FAIL;
}
