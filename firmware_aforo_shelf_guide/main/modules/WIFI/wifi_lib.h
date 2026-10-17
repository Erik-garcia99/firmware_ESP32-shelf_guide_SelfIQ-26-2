/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor Erik Garcia Chavez
 * @date octuber 2026
 * @assginature : proyecto de carrera
 * @teacher : Jose Isabel Garcia Rocha
 *
 ************************************************* */

#ifndef WIFI_LIB_H
#define WIFI_LIB_H

#include <esp_err.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

//++++++++++++++++++++++ macros
// intentara conectarse con las credenicales actuales 5 veces, esperara 1
// minutos antes de volver a petdir de nuevo las credenciales.
#define ESP_MAX_RETRY 5

// bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_NO_CREDS_BIT BIT2
#define WIFI_UPDATE BIT3

//++++++++++++++++ grupos de eventos
extern EventGroupHandle_t g_wifi_event_group;

//++++++++++++++++++ estrucutras

/**
 *
 * @brief esta estrucutura guaradaremos infromacion de la conexion o del estado
 * de la conexion
 *
 * ssid - nombre de la red a coenctarse @memberof esp_wifi_t
 * pswd - contrasenia de la red o de la cuenta @memberof esp_wifi_t
 * ip - ip otorgada al dispositivo por parte de la red @memberof esp_wifi_t
 * connected - 1 o 0 indicando que se pudo realizar la conexion y se eucneutra
 * coinectado el ESP @memberof esp_wifi_t
 *
 */
typedef struct {
  char *ssid;
  char *pswd;
  esp_ip4_addr_t *ip;
  int connected;
} TYPE_ESP_WIFI_T;

extern TYPE_ESP_WIFI_T esp_wifi;

//++++++++++++++++++ funciones
void wifi_init_sta(void);      // init del stack, una sola vez
void wifi_connect_saved(void); // intenta conectar con lo guardado en flash
esp_err_t wifi_set_credentials(const char *ssid,
                               const char *pswd); // guarda en flash y conecta

#endif
