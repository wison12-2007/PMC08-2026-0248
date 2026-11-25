#include "red.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define USER_AGENT   "estacion-itla/1.0"
#define MAX_RESP     (4u * 1024u * 1024u)
#define PUERTO_HTTP  4000
#define PUERTO_MQTT  1883

/* ---------- utilidades ---------- */

void dormir_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void recortar(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

static int env_copiar(const char *nombre, char *dst, size_t n) {
    const char *v = getenv(nombre);
    if (!v || !*v) {
        fprintf(stderr, "Falta la variable de entorno %s\n", nombre);
        return -1;
    }
    if (snprintf(dst, n, "%s", v) >= (int)n) {
        fprintf(stderr, "%s es demasiado largo\n", nombre);
        return -1;
    }
    recortar(dst);
    return 0;
}

/* ---------- callbacks HTTP ---------- */

static size_t al_escribir(char *p, size_t size, size_t nmemb, void *ud) {
    Resp *r = ud;
    size_t t = size * nmemb;
    if (r->len + t > MAX_RESP) return 0;
    char *nb = realloc(r->cuerpo, r->len + t + 1);
    if (!nb) return 0;
    r->cuerpo = nb;
    memcpy(r->cuerpo + r->len, p, t);
    r->len += t;
    r->cuerpo[r->len] = '\0';
    return t;
}

static size_t al_cabecera(char *p, size_t size, size_t nmemb, void *ud) {
    static const char clave[] = "retry-after:";
    Resp *r = ud;
    size_t t = size * nmemb;
    if (t > sizeof clave - 1 && strncasecmp(p, clave, sizeof clave - 1) == 0)
        r->retry_after = strtol(p + sizeof clave - 1, NULL, 10);
    return t;
}

static size_t al_descartar(char *p, size_t size, size_t nmemb, void *ud) {
    (void)p; (void)ud;
    return size * nmemb;
}

/* Opciones comunes a todo handle HTTP: UA, tiempos maximos, Bearer, callbacks */
static void configurar_http(CURL *h, const Red *r) {
    curl_easy_setopt(h, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_HTTPAUTH, (long)CURLAUTH_BEARER);
    curl_easy_setopt(h, CURLOPT_XOAUTH2_BEARER, r->token);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, al_escribir);
    curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, al_cabecera);
}

/* ---------- apertura y cierre ---------- */

int red_abrir(Red *r) {
    memset(r, 0, sizeof *r);
    if (env_copiar("ITLA_HOST", r->host, sizeof r->host) != 0) return -1;
    if (env_copiar("ITLA_MATRICULA", r->matricula, sizeof r->matricula) != 0) return -1;
    if (env_copiar("ITLA_TOKEN", r->token, sizeof r->token) != 0) return -1;
    if (strlen(r->token) != 32)
        fprintf(stderr, "Aviso: ITLA_TOKEN tiene %zu caracteres (se esperan 32)\n",
                strlen(r->token));

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return -1;
    r->http = curl_easy_init();
    r->mqtt = curl_easy_init();
    r->hdr_json = curl_slist_append(NULL, "Content-Type: application/json");
    if (!r->http || !r->mqtt || !r->hdr_json) {
        fprintf(stderr, "No se pudo iniciar libcurl\n");
        red_cerrar(r);
        return -1;
    }

    configurar_http(r->http, r);

    curl_easy_setopt(r->mqtt, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(r->mqtt, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(r->mqtt, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(r->mqtt, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(r->mqtt, CURLOPT_USERNAME, r->matricula);
    curl_easy_setopt(r->mqtt, CURLOPT_PASSWORD, r->token);
    curl_easy_setopt(r->mqtt, CURLOPT_WRITEFUNCTION, al_descartar);
    return 0;
}

void red_cerrar(Red *r) {
    if (r->http) curl_easy_cleanup(r->http);
    if (r->mqtt) curl_easy_cleanup(r->mqtt);
    if (r->hdr_json) curl_slist_free_all(r->hdr_json);
    r->http = r->mqtt = NULL;
    r->hdr_json = NULL;
    curl_global_cleanup();
}

/* ---------- HTTP ---------- */

static int pedir(Red *r, const char *ruta, const char *json, Resp *out) {
    char url[320];
    memset(out, 0, sizeof *out);
    int n = snprintf(url, sizeof url, "http://%s:%d%s", r->host, PUERTO_HTTP, ruta);
    if (n < 0 || (size_t)n >= sizeof url) {
        out->cc = CURLE_URL_MALFORMAT;
        return -1;
    }

    curl_easy_setopt(r->http, CURLOPT_URL, url);
    curl_easy_setopt(r->http, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(r->http, CURLOPT_HEADERDATA, out);
    if (json) {
        curl_easy_setopt(r->http, CURLOPT_POST, 1L);
        curl_easy_setopt(r->http, CURLOPT_POSTFIELDS, json);
        curl_easy_setopt(r->http, CURLOPT_POSTFIELDSIZE, (long)strlen(json));
        curl_easy_setopt(r->http, CURLOPT_HTTPHEADER, r->hdr_json);
    } else {
        curl_easy_setopt(r->http, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(r->http, CURLOPT_HTTPHEADER, NULL);
    }

    out->cc = curl_easy_perform(r->http);
    if (out->cc != CURLE_OK) return -1;
    curl_easy_getinfo(r->http, CURLINFO_RESPONSE_CODE, &out->http);
    return 0;
}

int red_get(Red *r, const char *ruta, Resp *out) {
    return pedir(r, ruta, NULL, out);
}

int red_post(Red *r, const char *ruta, const char *json, Resp *out) {
    return pedir(r, ruta, json, out);
}

void resp_liberar(Resp *r) {
    free(r->cuerpo);
    r->cuerpo = NULL;
    r->len = 0;
}

int resp_ok(const Resp *r, long esperado, const char *ctx) {
    if (r->cc != CURLE_OK) {
        fprintf(stderr, "%s: error de red: %s\n", ctx, curl_easy_strerror(r->cc));
        return 0;
    }
    if (r->http != esperado) {
        fprintf(stderr, "%s: HTTP %ld (se esperaba %ld)\n", ctx, r->http, esperado);
        return 0;
    }
    return 1;
}

/* ---------- MQTT: publicar ---------- */

int red_mqtt_publicar(Red *r, const char *topico, const char *texto) {
    char url[320];
    int n = snprintf(url, sizeof url, "mqtt://%s:%d/%s", r->host, PUERTO_MQTT, topico);
    if (n < 0 || (size_t)n >= sizeof url) return -1;

    curl_easy_setopt(r->mqtt, CURLOPT_URL, url);
    curl_easy_setopt(r->mqtt, CURLOPT_POSTFIELDS, texto);
    curl_easy_setopt(r->mqtt, CURLOPT_POSTFIELDSIZE, (long)strlen(texto));
    CURLcode cc = curl_easy_perform(r->mqtt);
    if (cc != CURLE_OK) {
        fprintf(stderr, "MQTT publicar: %s\n", curl_easy_strerror(cc));
        return -1; /* QoS 0: el latido se pierde, no se encola */
    }
    return 0;
}

/* ---------- MQTT: suscribirse ---------- */

struct Mqtt {
    char buf[4096];
    size_t len;
    MqttMsgCb cb;
    void *ud;
};

static size_t al_recibir_mqtt(char *ptr, size_t size, size_t nmemb, void *ud) {
    size_t n = size * nmemb;
    struct Mqtt *m = ud;
    if (n > sizeof m->buf - m->len) { /* no cabe: descarta lo acumulado */
        m->len = 0;
        if (n > sizeof m->buf) return n;
    }
    memcpy(m->buf + m->len, ptr, n);
    m->len += n;

    for (;;) { /* procesa todos los mensajes completos */
        if (m->len < 2) break;
        size_t tlen = ((size_t)(unsigned char)m->buf[0] << 8) | (unsigned char)m->buf[1];
        if (m->len < 2 + tlen) break;
        char *carga = m->buf + 2 + tlen;
        char *fin = memchr(carga, '\n', m->len - 2 - tlen);
        if (!fin) break; /* el mensaje aun no llego entero */
        *fin = '\0';
        m->cb(m->buf + 2, tlen, carga, m->ud);
        size_t usado = (size_t)(fin - m->buf) + 1;
        memmove(m->buf, m->buf + usado, m->len - usado);
        m->len -= usado;
    }
    return n; /* siempre size * nmemb */
}

static int al_progreso(void *ud, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un) {
    (void)dt; (void)dn; (void)ut; (void)un;
    const volatile sig_atomic_t *salir = ud;
    return *salir ? 1 : 0; /* distinto de 0 aborta perform */
}

static void esperar_interrumpible(unsigned seg, volatile sig_atomic_t *salir) {
    for (unsigned i = 0; i < seg * 10 && !*salir; i++) dormir_ms(100);
}

int red_mqtt_escuchar(Red *r, const char *topico, MqttMsgCb cb, void *ud,
                      volatile sig_atomic_t *salir) {
    char url[320];
    int n = snprintf(url, sizeof url, "mqtt://%s:%d/%s", r->host, PUERTO_MQTT, topico);
    if (n < 0 || (size_t)n >= sizeof url) return -1;

    struct Mqtt *m = calloc(1, sizeof *m);
    if (!m) return -1;
    m->cb = cb;
    m->ud = ud;

    unsigned espera = 1;
    while (!*salir) {
        CURL *h = curl_easy_init();
        if (!h) { free(m); return -1; }
        curl_easy_setopt(h, CURLOPT_URL, url);
        curl_easy_setopt(h, CURLOPT_USERAGENT, USER_AGENT);
        curl_easy_setopt(h, CURLOPT_USERNAME, r->matricula);
        curl_easy_setopt(h, CURLOPT_PASSWORD, r->token);
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, al_recibir_mqtt);
        curl_easy_setopt(h, CURLOPT_WRITEDATA, m);
        curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, al_progreso);
        curl_easy_setopt(h, CURLOPT_XFERINFODATA, (void *)salir);
#if LIBCURL_VERSION_NUM >= 0x073E00 /* 7.62.0 */
        curl_easy_setopt(h, CURLOPT_UPKEEP_INTERVAL_MS, 15000L); /* mantiene viva la conexion */
#endif

        m->len = 0;
        time_t inicio = time(NULL);
        CURLcode cc = curl_easy_perform(h);
        curl_easy_cleanup(h);
        if (*salir) break;

        if (time(NULL) - inicio >= 10) espera = 1; /* la conexion vivio: reinicia la espera */
        fprintf(stderr, "Suscripcion cerrada (%s); reconectando en %us\n",
                curl_easy_strerror(cc), espera);
        esperar_interrumpible(espera, salir);
        if (espera < 30) espera = espera * 2 > 30 ? 30 : espera * 2;
    }
    free(m);
    return 0;
}

/* ---------- HTTP paralelo (API multi) ---------- */

#define MAX_PARALELO 16

int red_get_paralelo(Red *r, const char *const *rutas, Resp *outs, int n) {
    if (n < 1 || n > MAX_PARALELO) return -1;
    CURLM *multi = curl_multi_init();
    if (!multi) return -1;

    CURL *hs[MAX_PARALELO] = {0};
    for (int i = 0; i < n; i++) {
        char url[320];
        memset(&outs[i], 0, sizeof outs[i]);
        int w = snprintf(url, sizeof url, "http://%s:%d%s", r->host, PUERTO_HTTP, rutas[i]);
        if (w < 0 || (size_t)w >= sizeof url) {
            outs[i].cc = CURLE_URL_MALFORMAT;
            continue;
        }
        hs[i] = curl_easy_init();
        if (!hs[i]) {
            outs[i].cc = CURLE_OUT_OF_MEMORY;
            continue;
        }
        configurar_http(hs[i], r);
        curl_easy_setopt(hs[i], CURLOPT_URL, url);
        curl_easy_setopt(hs[i], CURLOPT_WRITEDATA, &outs[i]);
        curl_easy_setopt(hs[i], CURLOPT_HEADERDATA, &outs[i]);
        curl_easy_setopt(hs[i], CURLOPT_PRIVATE, (char *)&outs[i]);
        curl_easy_setopt(hs[i], CURLOPT_HTTPGET, 1L);
        curl_multi_add_handle(multi, hs[i]);
    }

    int corriendo = 0;
    do {
        if (curl_multi_perform(multi, &corriendo) != CURLM_OK) break;
        if (corriendo) curl_multi_wait(multi, NULL, 0, 500, NULL);
    } while (corriendo);

    int pendientes = 0;
    CURLMsg *msg;
    while ((msg = curl_multi_info_read(multi, &pendientes)) != NULL) {
        if (msg->msg != CURLMSG_DONE) continue;
        char *priv = NULL;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &priv);
        Resp *o = (Resp *)priv;
        if (!o) continue;
        o->cc = msg->data.result;
        if (o->cc == CURLE_OK)
            curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &o->http);
    }

    for (int i = 0; i < n; i++) {
        if (!hs[i]) continue;
        curl_multi_remove_handle(multi, hs[i]);
        curl_easy_cleanup(hs[i]);
    }
    curl_multi_cleanup(multi);
    return 0;
}
