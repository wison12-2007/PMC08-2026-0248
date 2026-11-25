#ifndef RED_H
#define RED_H

#include <curl/curl.h>
#include <signal.h>
#include <stddef.h>

typedef struct {
    CURL *http;                 /* handle reutilizado para todo el HTTP */
    CURL *mqtt;                 /* handle reutilizado para publicar */
    struct curl_slist *hdr_json;
    char host[128];
    char matricula[32];
    char token[64];
} Red;

typedef struct {
    CURLcode cc;        /* nivel 1: resultado de libcurl */
    long http;          /* nivel 2: codigo HTTP */
    long retry_after;   /* segundos, 0 si no vino */
    char *cuerpo;       /* siempre terminado en '\0' si no es NULL */
    size_t len;
} Resp;

typedef void (*MqttMsgCb)(const char *topico, size_t tlen, const char *texto, void *ud);

int  red_abrir(Red *r);
void red_cerrar(Red *r);

int  red_get(Red *r, const char *ruta, Resp *out);
int  red_post(Red *r, const char *ruta, const char *json, Resp *out);
/* n peticiones GET a la vez con la API multi; outs tiene n elementos */
int  red_get_paralelo(Red *r, const char *const *rutas, Resp *outs, int n);
void resp_liberar(Resp *r);
/* 1 si cc==CURLE_OK y http==esperado; si no, imprime el motivo y devuelve 0 */
int  resp_ok(const Resp *r, long esperado, const char *ctx);

int  red_mqtt_publicar(Red *r, const char *topico, const char *texto);
/* Bloquea hasta que *salir sea distinto de 0; reconecta con espera creciente. */
int  red_mqtt_escuchar(Red *r, const char *topico, MqttMsgCb cb, void *ud,
                       volatile sig_atomic_t *salir);

void dormir_ms(long ms);

#endif
