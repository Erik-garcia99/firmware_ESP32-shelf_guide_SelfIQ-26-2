#!/usr/bin/env bash
# Pruebas de host de la lógica de guía LED. No requiere ESP-IDF ni hardware:
# compila los módulos puros (sin driver/gpio.h ni cJSON) con el gcc del sistema.
set -euo pipefail
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/selfiq_guide_host_tests"
gcc -std=c11 -Wall -Wextra -Werror -pedantic \
  -I../main/modules/GUIDE \
  ../main/modules/GUIDE/guide_core.c \
  ../main/modules/GUIDE/guide_protocol.c \
  ../main/modules/GUIDE/guide_parse.c \
  test_guide_core.c -o "$OUT"
"$OUT"
