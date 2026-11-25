#ifndef JSON_H
#define JSON_H

#include <stddef.h>
#include "datos.h"

/* Cuerpos JSON que se envian al servidor. Con make CJSON=1 se arman con cJSON;
 * sin esa opcion, con snprintf. Devuelven 0 si caben en out, -1 si no. */
int json_telemetria(const Telem *t, char *out, size_t n);
int json_ack(const char *codigo, char *out, size_t n);
/* Extrae un campo de texto de un objeto JSON (solo con cJSON; si no, -1). */
int json_campo_texto(const char *json, const char *clave, char *out, size_t n);

#endif
