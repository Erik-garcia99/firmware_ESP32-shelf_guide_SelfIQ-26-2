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

//funciones 


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


void recv_task(void *params){
  uint8_t rx_buffer[MAX_DATA];
  int len=0;
  uint8_t offset;
  TYPE_FORMAT_REQUEST_T *frame = NULL;
  while(1){
    offset=0;
    len = recv(tcp_client.sock, rx_buffer, sizeof(MAX_DATA)-1, 0);

    if(len < 0){
      
      if(errno == EAGAIN || errno == EWOULDBLOCK){
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;  
      }

      ESP_LOGE(TAG, "recv fallo errno: %d", errno);
      tcp_client.connected = 0;
      if(tcp_client.sock >= 0){
        close(tcp_client.sock);
        tcp_client.sock = -1;
      }
      xEventGroupSetBits(g_tcp_event_group, TCP_DISCONNECTED);
      vTaskDelete(NULL); 

    }
    else if(len == 0){
      //conexion cerrada por el servidor 
      ESP_LOGE(TAG, "TCP_LIB: el servidor cerro la conexion");
      tcp_client.connected = 0;
      close(tcp_client.sock);
      tcp_client.sock = -1;
      xEventGroupSetBits(g_tcp_event_group, TCP_DISCONNECTED);
      vTaskDelete(NULL); // esta tarea ya no tiene socket que leer
    }
    //en otro caso llego infromacion, recv egresa el numeoro de bytes que recibio 
    /*el numero de bytes minimos que debemos de recibir para saber que lo que se recibo fue un frame correcto es de 5 bytes 
    con 5 bytes es lo que compone un ACK simple, indicando que el mensaje llego.*/

    
    frame = pvPortMalloc(sizeof(TYPE_FORMAT_REQUEST_T));

    if(frame ==NULL){
      ESP_LOGE(TAG, "sin memoria");
      continue;
    }

    /***
     * 
     * la forma en como llega
     *  | HEADER | LEN | TYPE_FRAME |ACTION  | DATA |
     * 
     */

    //ahora empezamos a serparar y ordenar el payload que llego
    //topo el HEAD
    uint16_t head;
    memcpy(&head, &rx_buffer[offset], 2); offset += 2;
    frame->header = ntohs(head);

    //tomo la longitud del resto del payload
    memcpy(&frame->len, &rx_buffer[offset], 1); offset++;
    //obtengo que tipo de frame es y la accion que quiere relaizar 

    uint8_t type_action = rx_buffer[offset]; offset++; 
    //perimeo viene el tipo y despues la action
    frame->type_frame = (type_action >> 4) & 0x0f;
    frame->action = type_action & 0x0f;

    //calculamos de cuando es el frame que se llego 
    //len trae el dato totan pero LEN del frame trae el resto de valor de todo lo demas 

    uint8_t data_len = frame->len - 1; // quitamos 1 bte por tipo de red y la accion. el resto que queda es 
    //la infromacion que llego 

    //aqui va a ver un gran problema, el ornden en como llegan, poque creo que llegan en Big endian, pero
    //la ESP23 trata los datos en fromato Little endiean 
    //entonces debemos de sber y ver como es que llegan los datos. 

    //puedo hacer na funcion para convertir ese big-endean en little-endean de forma dinamica. 

    memcpy(&frame->data, &rx_buffer[offset], data_len); offset += data_len;

  }


}




