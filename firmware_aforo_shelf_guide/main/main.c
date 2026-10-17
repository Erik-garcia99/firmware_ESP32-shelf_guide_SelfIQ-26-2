/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor Erik Garcia Chavez
 * @date octuber 2026
 * @assginature : proyecto de carrera
 * @teacher : Jose Isabel Garcia Rocha
 *
 ************************************************* */

// liberias estanadar
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// logs
#include "esp_err.h"
#include "esp_log.h"
// flash
#include "nvs.h"
#include "nvs_flash.h"

// freertos
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// librerias propias
#include "global.h"
#include "modules/TCP/tcp_lib.h"
#include "modules/WIFI/wifi_lib.h"

//+++++++++++++++++++++++++++++++++++ colas

QueueHandle_t
    tcp_rx_queue; // cola que usaremos como swap entre el modulo de TCP que
                  // recibe el frame y main que desempaqueta la infromacion.

//++++++++++++++++++++++++++++++++++ grupo de eventos
EventGroupHandle_t g_tcp_event_group;

//++++++++++++++++++++++++++++++++ varibales globales

const char *TAG = "MAIN: ";

//++++++++++++++++++++++++++++++++ estrucutras
TYPE_FORMAT_REQUEST_T format_reques = {0};
TYPE_SEND_INFO_T send_info = {0};
TYPE_ESP_WIFI_T esp_wifi = {0};
TYPE_TCP_CLIENT_T tcp_client = {0};

//++++++++++++++++++++++++++++++++++ enums

TYPE_OP_TYPE_T eOp_type;
ACTION_T eAction;
TYPE_FRAME_T eType_frame;
DATA_FIELD_T eData_field_t;

//+++++++++++++++++++++++++++++++funciones

// static uint8_t request_credentials(char **ssid, char **pswd);

// funciones que usare para guardar crecnuales en flash, (WIFI lo hace cons us
// propias funciones)
esp_err_t save_credentials(uint8_t identifator);
esp_err_t load_cred(uint8_t identificator);
/*********************************/
/*guardamos un checksum, para identificar la integridad de los datos */
/*********************************/
/*
 * @brief funcion que se encarga de actualiuzar las credencuales.
 *
 * */
esp_err_t update_setup_cred(char *key, void *anchor, uint8_t identificator);

//++++++++++++++++++++++++++++++++ tareas

void app_main(void) {

  // variables
  esp_err_t ret;
  EventBits_t bits = 0;
  // inicalizacion de varibales
  tcp_rx_queue = xQueueCreate(10, sizeof(TYPE_FORMAT_REQUEST_T *));

  // incializacion de flash para cargar o escribir en flash
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // WIFI: primero intenta con las credenciales guardadas en flash
  wifi_init_sta();
  wifi_connect_saved();

  while (1) {
    // primero se intenta conectar con las credenclaes que puede estar guardasd
    // en flash
    bits = xEventGroupWaitBits(g_wifi_event_group,
                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT |
                                   WIFI_NO_CREDS_BIT,
                               pdTRUE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
      ESP_LOGI(TAG, "WIFI conectado");
      break;
      // si me pude conectar al WIFI el proximo paso es conectarme a TCP
    } else if (bits & WIFI_NO_CREDS_BIT) {

      ESP_LOGW(TAG,
               "imposble conectarse, no hay crenciales o fallo al intentar");
      // tenemos 2 opciones
      /* realmente la esp32 viene vacia y no tiene crencuales guardadas --> por
       * lo que lo mas porabale es que la red ed la rasberry este levantada al
       * igual que le TCP_server, por lo que por este no hay problema
       *
       * puede que las credenicales esten bien pero hay una falla en internet,
       *
       * en otro caso tiene credennciales guardadas, en este caso, pueden ser
       * uno de los sigueintes casos :
       *
       * la red esta caida.
       * se cambio las credenciales de red
       * se corrompieron las credncieles
       *
       * --> al final cada ESP con MQTT tendra que enviar un keep_alive,
       * entpnces si no se recibe un keep_alive en un lapso de 5 a 10 minutos se
       * va a concluir que algo paso con la conexion en ese esp32
       *
       * por lo que indicara en la APP se se incie comunicacion, es cunado se
       * levanta TCP, y tiene que inciar una comnucacion entre la rasberry y la
       * esp32.
       *
       * */

      // en este caso

      continue;
    } else if (bits & WIFI_FAIL_BIT) {
    }
  }
}

// el seungdo parametro represetna en WIFI la contrasneia
// en TCP represeta en puerto
// como en uno es string y en otro es numerico, se utiliza de tipoc void* para
// evitar el problema de casting, accedindo a memoria
esp_err_t update_setup_cred(char *key, void *anchor, uint8_t identificator) {

  esp_err_t ret;

  if (identificator == eTCP_CRED) {
    // entonces debemos de actualizar las credenicales de TPC
    uint16_t *port = (uint16_t *)anchor;

    tcp_client.host_port = *port;
    tcp_client.host_ip = realloc(tcp_client.host_ip, strlen(key) + 1);

    if (tcp_client.host_ip == NULL) {
      ESP_LOGE(TAG, "no se pudo asingar memoria\n");
      return ESP_FAIL;
    }
    strcpy(tcp_client.host_ip, key);

    // pasamos a guardar los datos en flash.
    // y regresamos el valor si es que se pudo guardar o no.
    return save_credentials(eTCP_CRED);

  } else if (identificator == eWIFI_CRED) {

    return ESP_OK;
  }
}

// en este puntp ya transformamos valores en trama binaria en su rpresentacion
// ascii o lo dejamos en froma numericos
//
// // SSID y PSWD para WIFI -> STRING
// //IP - STRING - PORT - uint16;
esp_err_t save_credentials(uint8_t identifator) {

  esp_err_t ret;
  nvs_handle_t h;

  if (identifator == eTCP_CRED) {

    // vamos a guardar las credeniclaes de TPC en flash, para levanatar
    // servicios mas rapido y comprbar corrupciones en los datos

    if (tcp_client.host_ip == NULL) {
      ESP_LOGE(TAG, "estado de IP invalida");
      return ESP_FAIL;
    }
    uint16_t port = tcp_client.host_port;
    uint64_t sum = checksum(tcp_client.host_ip, &port, eTCP_CRED);

    ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "tipo de error: %s", esp_err_to_name(ret));
      return ret;
    }
    ret = nvs_set_str(h, KEY_TCP_IP, tcp_client.host_ip);
    if (ret == ESP_OK) {
      ret = nvs_set_u16(h, KEY_TCP_PORT, port);
    }
    if (ret == ESP_OK) {
      ret = nvs_set_u64(h, KEY_TCP_SUM, sum);
    }
    if (ret == ESP_OK) {
      ret = nvs_commit(h); // sin commit no se escribe en flash
    }
    nvs_close(h);
    return ret;
  }
  // si el identificador es uno no estbalecido pues fallo.
  return ESP_FAIL;
}

esp_err_t load_cred(uint8_t identificator) {

  nvs_handle_t h;
  esp_err_t ret;
  ret = nvs_open(NVS_NS, NVS_READONLY, &h);
  if (ret != ESP_OK) {
    return ret;
  }

  if (identificator == eTCP_CRED) {
    char *ip = NULL;
    size_t len = 0;
    uint16_t port = 0;
    uint64_t saved_sum = 0;
    // con buffer NULL, nvs_get_str solo devuelve el tamanio (incluye el '\0')
    ret = nvs_get_str(h, KEY_TCP_IP, NULL, &len);
    if (ret == ESP_OK) {
      ip = malloc(len);
      if (ip == NULL) {
        ret = ESP_ERR_NO_MEM;
      }
    }
    if (ret == ESP_OK) {
      ret = nvs_get_str(h, KEY_TCP_IP, ip, &len);
    }
    if (ret == ESP_OK) {
      ret = nvs_get_u16(h, KEY_TCP_PORT, &port);
    }
    if (ret == ESP_OK) {
      ret = nvs_get_u64(h, KEY_TCP_SUM, &saved_sum);
    }
    nvs_close(h);

    // recalculamos la suma con lo que leimos y la comparamos con la guardada
    if (ret == ESP_OK && checksum(ip, &port, eTCP_CRED) != saved_sum) {
      ret = ESP_ERR_INVALID_CRC;
    }

    if (ret != ESP_OK) {
      free(ip);
      return ret;
    }

    free(tcp_client.host_ip);
    tcp_client.host_ip = ip;
    tcp_client.host_port = port;
    return ESP_OK;
  }
  // algo parecido sera con wifi, mas o menos.

  return ESP_FAIL;
}

// vamos a sumar los valores, totales de los datos
// SSID + pswd
// IP + PORT
uint64_t checksum(const char *key, const void *anchor, uint8_t identificator) {
  uint64_t total = 0;

  // key siempre es un string (SSID o IP): sumamos el valor de cada caracter
  for (size_t i = 0; key[i] != '\0'; i++) {
    total += (uint8_t)key[i];
  }

  if (identificator == eTCP_CRED) {
    // anchor es el puerto: es un numero, se suma directo
    total += *(const uint16_t *)anchor;

  } else if (identificator == eWIFI_CRED) {
    // anchor es la contrasenia: otro string, se suma caracter por caracter
    const char *pswd = (const char *)anchor;
    for (size_t i = 0; pswd[i] != '\0'; i++) {
      total += (uint8_t)pswd[i];
    }
  }

  return total;
}

// tareas
