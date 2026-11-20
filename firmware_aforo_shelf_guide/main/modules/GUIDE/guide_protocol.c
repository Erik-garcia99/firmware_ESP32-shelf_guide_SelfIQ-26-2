/* Textos del contrato guía LED. Sin ESP-IDF: se prueban en host. */

#include "guide_protocol.h"

#include <string.h>

guide_action_t guide_action_from_text(const char *text) {
  if (!text)
    return GUIDE_ACTION_NONE;
  if (!strcmp(text, "guide.start"))
    return GUIDE_ACTION_START;
  if (!strcmp(text, "guide.stop"))
    return GUIDE_ACTION_STOP;
  if (!strcmp(text, "guide.status"))
    return GUIDE_ACTION_STATUS;
  return GUIDE_ACTION_NONE;
}

const char *guide_applied_text(guide_applied_t applied) {
  switch (applied) {
  case GUIDE_APPLIED_ON:
    return "on";
  case GUIDE_APPLIED_OFF:
    return "off";
  case GUIDE_APPLIED_ERROR:
    return "error";
  case GUIDE_APPLIED_REJECTED:
  default:
    return "rejected";
  }
}

const char *guide_action_text(guide_action_t action) {
  switch (action) {
  case GUIDE_ACTION_START:
    return "guide.start";
  case GUIDE_ACTION_STOP:
    return "guide.stop";
  case GUIDE_ACTION_STATUS:
    return "guide.status";
  case GUIDE_ACTION_NONE:
  default:
    return "";
  }
}
