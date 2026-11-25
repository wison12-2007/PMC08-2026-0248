#include "json.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef USAR_CJSON
#include "cJSON.h"

static int volcar(cJSON *obj, char *out, size_t n) {
    if (!obj) return -1;
    char *txt = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!txt) return -1;
    int rc = snprintf(out, n, "%s", txt) < (int)n ? 0 : -1;
    cJSON_free(txt);
    return rc;
}

static double dos_decimales(double v) {
    return round(v * 100.0) / 100.0;
}

int json_telemetria(const Telem *t, char *out, size_t n) {
    cJSON *o = cJSON_CreateObject();
    if (!o) return -1;
    cJSON_AddNumberToObject(o, "seq", (double)t->seq);
    cJSON_AddNumberToObject(o, "ts", (double)t->ts);
    cJSON_AddNumberToObject(o, "temp", dos_decimales(t->temp));
    cJSON_AddNumberToObject(o, "corriente", dos_decimales(t->corriente));
    return volcar(o, out, n);
}

int json_ack(const char *codigo, char *out, size_t n) {
    cJSON *o = cJSON_CreateObject();
    if (!o) return -1;
    cJSON_AddStringToObject(o, "codigo", codigo);
    return volcar(o, out, n);
}

int json_campo_texto(const char *json, const char *clave, char *out, size_t n) {
    cJSON *raiz = cJSON_Parse(json);
    if (!raiz) return -1;
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(raiz, clave);
    int rc = -1;
    if (cJSON_IsString(c) && c->valuestring && snprintf(out, n, "%s", c->valuestring) < (int)n) rc = 0;
    cJSON_Delete(raiz);
    return rc;
}

#else /* sin cJSON: snprintf */

int json_telemetria(const Telem *t, char *out, size_t n) {
    int w = snprintf(out, n, "{\"seq\":%ld,\"ts\":%ld,\"temp\":%.2f,\"corriente\":%.2f}",
                     t->seq, t->ts, t->temp, t->corriente);
    return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

int json_ack(const char *codigo, char *out, size_t n) {
    int w = snprintf(out, n, "{\"codigo\":\"%s\"}", codigo);
    return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

int json_campo_texto(const char *json, const char *clave, char *out, size_t n) {
    (void)json; (void)clave; (void)out; (void)n;
    return -1;
}
#endif
