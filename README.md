# Firmware Guía de Estantes — SelfIQ

Firmware del módulo de **guía visual de estantes mediante LED** del sistema SelfIQ.

Este repositorio contiene exclusivamente el firmware correspondiente al módulo basado en:

- ESP32-SUPERMINI o ESP-C3
- LED ultrabrillante
- Comunicación Wi-Fi local
- MQTT

El propósito de este módulo es proporcionar una guía visual al usuario dentro del establecimiento, encendiendo el LED asociado al estante donde se encuentra el producto buscado.

---

## Descripción

El módulo de guía forma parte de la capa IoT local del sistema SelfIQ.

La ESP32-SUPERMINI se conecta a la red IoT local y permanece suscrita a uno o más tópicos MQTT.

Cuando el sistema determina qué estante debe señalarse, se publica un comando MQTT hacia el dispositivo correspondiente.

El firmware interpreta dicho comando y activa el LED asociado al estante.

Este módulo no realiza procesamiento complejo ni se comunica directamente con servicios de Internet.

---

## Función dentro de la arquitectura

El flujo general del módulo es:

```text
Frontend / Sistema
        │
        ▼
Backend Cloud
        │
        ▼
Raspberry Pi Zero 2 W
        │
        │ MQTT
        ▼
ESP32-SUPERMINI
        │
        ▼
Procesamiento del comando
        │
        ▼
LED del estante
