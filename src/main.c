#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "datos.h"
#include "json.h"
#include "red.h"
#include "sensor.h"

#define RUTA_DB          "estacion.db"
#define PAUSA_ENTRE_ENVIOS_MS 250   /* 4 envios/s, bajo el limite de 5/s */
#define TOPE_ESPERA_S    30

static volatile sig_atomic_t g_parar = 0;

static void al_senal(int s) {
    (void)s;
    g_parar = 1;
}

/* ---------- bitacora con hora ---------- */

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static void bitacora(const char *etiqueta, const char *fmt, ...) {
    char hora[16] = "--:--:--";
    time_t ahora = time(NULL);
    struct tm tmv;
    if (localtime_r(&ahora, &tmv)) strftime(hora, sizeof hora, "%H:%M:%S", &tmv);
    fprintf(stderr, "[%s] %-8s ", hora, etiqueta);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static long long reloj_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Espera exponencial 1, 2, 4 ... TOPE_ESPERA_S segundos */
static long espera_exponencial_s(unsigned intento) {
    long s = 1L << (intento < 5 ? intento : 5);
    return s > TOPE_ESPERA_S ? TOPE_ESPERA_S : s;
}

/* Peticion con reintentos para los niveles 0 a 2. Devuelve 1 si llego el
 * codigo esperado; en caso contrario deja el detalle en bitacora. */
static int pedir_con_reintentos(Red *red, const char *ruta, const char *json,
                                long esperado, Resp *out) {
    unsigned caidas = 0;
    for (int vuelta = 0; vuelta < 6 && !g_parar; vuelta++) {
        if (json) red_post(red, ruta, json, out);
        else red_get(red, ruta, out);

        long espera_s;
        if (out->cc != CURLE_OK) {
            espera_s = espera_exponencial_s(caidas++);
        } else if (out->http == esperado) {
            return 1;
        } else if (out->http == 503) {
            espera_s = out->retry_after > 0 ? out->retry_after + 1 : 5;
        } else if (out->http == 429) {
            espera_s = 1;
        } else {
            bitacora("http", "%s respondio %ld (se esperaba %ld)", ruta, out->http, esperado);
            return 0;
        }
        bitacora("http", "%s: reintento en %lds", ruta, espera_s);
        resp_liberar(out);
        dormir_ms(espera_s * 1000);
    }
    return 0;
}

/* ============ nivel 0: config ============ */

static int orden_config(sqlite3 *db, Red *red) {
    Resp r = {0};
    if (!pedir_con_reintentos(red, "/api/v1/config", NULL, 200, &r)) {
        resp_liberar(&r);
        return 1;
    }
    int rc = r.cuerpo ? datos_config_desde_texto(db, r.cuerpo) : -1;
    resp_liberar(&r);
    if (rc != 0) return 1;

    char umbral[64], intervalo[64], hora[64];
    if (datos_config_get(db, "umbral_temp", umbral, sizeof umbral) ||
        datos_config_get(db, "intervalo_s", intervalo, sizeof intervalo) ||
        datos_config_get(db, "hora", hora, sizeof hora)) {
        bitacora("config", "faltan campos en la respuesta de /config");
        return 1;
    }
    time_t h = (time_t)strtol(hora, NULL, 10);
    char legible[64] = "?";
    struct tm tmv;
    if (localtime_r(&h, &tmv)) strftime(legible, sizeof legible, "%Y-%m-%d %H:%M:%S", &tmv);
    printf("umbral_temp: %s\nintervalo_s: %s\nhora_servidor: %s (%s)\n",
           umbral, intervalo, legible, hora);
    return 0;
}

/* ============ nivel 1: descargar ============ */

static int tam_pagina_config(sqlite3 *db) {
    char v[64];
    if (datos_config_get(db, "lote_max", v, sizeof v) == 0) {
        long l = strtol(v, NULL, 10);
        if (l >= 1 && l <= 200) return (int)l;
    }
    return 200;
}

/* Cuenta filas unicas y suma de ids, y envia POST /respuestas/descarga */
static int publicar_descarga(sqlite3 *db, Red *red) {
    long unicas = 0;
    long long suma = 0;
    if (datos_totales_descarga(db, &unicas, &suma) != 0) return 1;
    char cuerpo[128];
    snprintf(cuerpo, sizeof cuerpo, "{\"filas_unicas\":%ld,\"suma_ids\":%lld}", unicas, suma);
    printf("filas_unicas=%ld suma_ids=%lld\n", unicas, suma);

    Resp r = {0};
    int ok = pedir_con_reintentos(red, "/api/v1/respuestas/descarga", cuerpo, 200, &r);
    if (ok && r.cuerpo) printf("%s", r.cuerpo);
    resp_liberar(&r);
    return ok ? 0 : 1;
}

static int orden_descargar(sqlite3 *db, Red *red) {
    int tam_pagina = tam_pagina_config(db);

    if (datos_exec(db, "BEGIN") != 0) return 1;
    long ancla = 0;
    for (int pagina = 1;; pagina++) {
        char ruta[128];
        snprintf(ruta, sizeof ruta, "/api/v1/lecturas?desde_id=%ld&limite=%d", ancla, tam_pagina);
        Resp r = {0};
        if (!pedir_con_reintentos(red, ruta, NULL, 200, &r) || !r.cuerpo) {
            bitacora("descarga", "interrumpida en la pagina %d; ROLLBACK", pagina);
            resp_liberar(&r);
            datos_exec(db, "ROLLBACK");
            return 1;
        }
        long mayor = ancla;
        int recibidas = 0;
        int rc = datos_importar_csv(db, r.cuerpo, r.len, &mayor, &recibidas);
        resp_liberar(&r);
        if (rc != 0) {
            datos_exec(db, "ROLLBACK");
            return 1;
        }
        bitacora("descarga", "pagina %d: %d filas, id mayor %ld", pagina, recibidas, mayor);
        if (recibidas < tam_pagina) break;
        if (mayor <= ancla) {
            bitacora("descarga", "el id no avanza; se detiene");
            break;
        }
        ancla = mayor;
        dormir_ms(PAUSA_ENTRE_ENVIOS_MS);
    }
    if (datos_exec(db, "COMMIT") != 0) return 1;
    return publicar_descarga(db, red);
}

/* Extra: paginas en paralelo con la API multi de libcurl.
 * Supone paginas contiguas (desde_id = 0, T, 2T...). Si quedan huecos o algo
 * falla, se deshace y se completa con la descarga secuencial. */
#define PAGINAS_POR_OLA 5

static int orden_descargar_paralelo(sqlite3 *db, Red *red) {
    int tam_pagina = tam_pagina_config(db);
    if (datos_exec(db, "BEGIN") != 0) return 1;

    int terminado = 0, falla = 0;
    for (int ola = 0; !terminado && !falla && ola < 100; ola++) {
        char rutas_txt[PAGINAS_POR_OLA][128];
        const char *rutas[PAGINAS_POR_OLA];
        Resp res[PAGINAS_POR_OLA];
        for (int i = 0; i < PAGINAS_POR_OLA; i++) {
            long desde = (long)(ola * PAGINAS_POR_OLA + i) * tam_pagina;
            snprintf(rutas_txt[i], sizeof rutas_txt[i],
                     "/api/v1/lecturas?desde_id=%ld&limite=%d", desde, tam_pagina);
            rutas[i] = rutas_txt[i];
        }
        if (red_get_paralelo(red, rutas, res, PAGINAS_POR_OLA) != 0) falla = 1;

        for (int i = 0; !falla && i < PAGINAS_POR_OLA; i++) {
            if (res[i].cc != CURLE_OK || res[i].http != 200 || !res[i].cuerpo) { falla = 1; break; }
            long mayor = 0;
            int recibidas = 0;
            if (datos_importar_csv(db, res[i].cuerpo, res[i].len, &mayor, &recibidas) != 0) { falla = 1; break; }
            bitacora("paralelo", "ola %d pagina %d: %d filas", ola + 1, i + 1, recibidas);
            if (recibidas < tam_pagina) terminado = 1;
        }
        for (int i = 0; i < PAGINAS_POR_OLA; i++) resp_liberar(&res[i]);
        if (!terminado && !falla) dormir_ms(1000); /* 5 peticiones por ola, 5/s de limite */
    }

    if (falla) {
        bitacora("paralelo", "fallo una pagina; ROLLBACK y descarga secuencial");
        datos_exec(db, "ROLLBACK");
        return orden_descargar(db, red);
    }
    if (datos_exec(db, "COMMIT") != 0) return 1;
    if (datos_lecturas_contiguas(db) != 1) {
        bitacora("paralelo", "hay huecos en los ids; completo con la descarga secuencial");
        return orden_descargar(db, red);
    }
    return publicar_descarga(db, red);
}

/* ============ nivel 2: analizar ============ */

static int orden_analizar(sqlite3 *db, Red *red) {
    char v[64];
    if (datos_config_get(db, "umbral_temp", v, sizeof v) != 0) {
        if (orden_config(db, red) != 0) return 1;
        if (datos_config_get(db, "umbral_temp", v, sizeof v) != 0) return 1;
    }
    char cuerpo[2048];
    if (datos_analisis_json(db, strtod(v, NULL), cuerpo, sizeof cuerpo) != 0) return 1;
    printf("%s\n", cuerpo);

    Resp r = {0};
    int ok = pedir_con_reintentos(red, "/api/v1/respuestas/analisis", cuerpo, 200, &r);
    if (ok && r.cuerpo) printf("%s", r.cuerpo);
    resp_liberar(&r);
    return ok ? 0 : 1;
}

/* ============ niveles 3 y 4: enviar ============ */

typedef struct {
    sqlite3 *db;
    Red *red;
    char topico_latido[96];
    long intervalo_ms, latido_ms;
    long ultima_seq;
    long long prox_lectura, prox_latido;
    long long no_enviar_antes;   /* 503, 429 o caida de red */
    long long ultimo_envio;
    unsigned caidas_seguidas;
} Emisor;

static long leer_config_larga(sqlite3 *db, const char *clave, long defecto) {
    char v[64];
    if (datos_config_get(db, clave, v, sizeof v) != 0) return defecto;
    long n = strtol(v, NULL, 10);
    return n >= 1 ? n : defecto;
}

static int emisor_iniciar(Emisor *e, sqlite3 *db, Red *red) {
    memset(e, 0, sizeof *e);
    e->db = db;
    e->red = red;
    e->intervalo_ms = leer_config_larga(db, "intervalo_s", 5) * 1000;
    e->latido_ms = leer_config_larga(db, "latido_s", 10) * 1000;
    e->ultima_seq = datos_ultima_seq(db); /* el contador vive en SQLite */
    if (e->ultima_seq < 0) return -1;
    snprintf(e->topico_latido, sizeof e->topico_latido, "itla/estudiantes/%s/latido", red->matricula);
    e->prox_lectura = e->prox_latido = reloj_ms();
    return 0;
}

static void emisor_generar(Emisor *e) {
    Telem t;
    t.seq = e->ultima_seq + 1;
    t.ts = (long)time(NULL);
    Muestra m = sensor_leer((time_t)t.ts);
    t.temp = m.temp;
    t.corriente = m.corriente;
    if (datos_telem_guardar(e->db, &t) == 0) {
        e->ultima_seq = t.seq;
        bitacora("lectura", "seq %ld guardada (%.2f C, %.2f A)", t.seq, t.temp, t.corriente);
    }
    e->prox_lectura += e->intervalo_ms;
}

static void emisor_latir(Emisor *e) {
    char msg[96];
    snprintf(msg, sizeof msg, "LATIDO;%ld;%ld;%ld", e->ultima_seq,
             datos_telem_pendientes(e->db), (long)time(NULL));
    red_mqtt_publicar(e->red, e->topico_latido, msg); /* QoS 0: si falla se pierde */
    e->prox_latido += e->latido_ms;
    long long ya = reloj_ms();
    if (e->prox_latido < ya) e->prox_latido = ya;
}

/* Intenta enviar la lectura pendiente mas antigua y ajusta no_enviar_antes. */
static void emisor_drenar(Emisor *e) {
    Telem t;
    int hay = datos_telem_pendiente(e->db, &t);
    if (hay <= 0) {
        if (hay < 0) e->no_enviar_antes = reloj_ms() + 1000;
        return;
    }

    char cuerpo[160];
    if (json_telemetria(&t, cuerpo, sizeof cuerpo) != 0) {
        bitacora("envio", "no se pudo armar el JSON de seq %ld", t.seq);
        e->no_enviar_antes = reloj_ms() + 1000;
        return;
    }

    Resp r = {0};
    red_post(e->red, "/api/v1/telemetria", cuerpo, &r);
    long long pausa_ms = 0;

    if (r.cc != CURLE_OK) {
        pausa_ms = espera_exponencial_s(e->caidas_seguidas++) * 1000;
        bitacora("red", "seq %ld sin enviar (%s); espero %llds", t.seq,
                 curl_easy_strerror(r.cc), pausa_ms / 1000);
        datos_telem_intento(e->db, t.seq);
    } else {
        e->caidas_seguidas = 0;
        switch (r.http) {
        case 200:
        case 201:
            datos_telem_marcar_enviado(e->db, t.seq);
            bitacora("envio", "seq %ld ok (%s)", t.seq, r.http == 201 ? "creada" : "duplicada");
            break;
        case 503:
            pausa_ms = (r.retry_after > 0 ? r.retry_after : 5) * 1000LL + 1000;
            bitacora("envio", "503: sin peticiones durante %llds", pausa_ms / 1000);
            datos_telem_intento(e->db, t.seq);
            break;
        case 429:
            pausa_ms = 2000;
            datos_telem_intento(e->db, t.seq);
            break;
        default:
            pausa_ms = 5000;
            bitacora("envio", "seq %ld: HTTP %ld inesperado", t.seq, r.http);
            datos_telem_intento(e->db, t.seq);
            break;
        }
    }
    resp_liberar(&r);
    e->ultimo_envio = reloj_ms();
    if (pausa_ms > 0) e->no_enviar_antes = e->ultimo_envio + pausa_ms;
}

static int orden_enviar(sqlite3 *db, Red *red) {
    Emisor e;
    if (emisor_iniciar(&e, db, red) != 0) return 1;
    sensor_iniciar();
    bitacora("enviar", "lectura cada %lds (%s), latido cada %lds; Ctrl+C para salir",
             e.intervalo_ms / 1000, sensor_origen(), e.latido_ms / 1000);

    while (!g_parar) {
        long long t = reloj_ms();
        if (t >= e.prox_lectura) emisor_generar(&e);
        if (t >= e.prox_latido) emisor_latir(&e);
        t = reloj_ms();
        if (t >= e.no_enviar_antes && t - e.ultimo_envio >= PAUSA_ENTRE_ENVIOS_MS)
            emisor_drenar(&e);
        dormir_ms(50);
    }
    sensor_cerrar();
    bitacora("enviar", "fin; pendientes en cola: %ld", datos_telem_pendientes(db));
    return 0;
}

/* ============ nivel 5: escuchar ============ */

typedef struct {
    sqlite3 *db;
    Red *red;
} Oyente;

static int codigo_alarma_valido(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n > 16) return 0;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)s[i])) return 0;
    return 1;
}

static void confirmar_alarma(Oyente *o, const char *codigo) {
    char cuerpo[64];
    if (json_ack(codigo, cuerpo, sizeof cuerpo) != 0) return;
    Resp r = {0};
    red_post(o->red, "/api/v1/alarmas/ack", cuerpo, &r);
    if (r.cc != CURLE_OK) {
        bitacora("alarma", "ack de %s fallo (%s); queda pendiente", codigo, curl_easy_strerror(r.cc));
    } else if (r.http == 200 || r.http == 409) {
        datos_alarma_marcar_ack(o->db, codigo, (long)time(NULL));
        bitacora("alarma", "%s confirmada (HTTP %ld) %s", codigo, r.http,
                 r.cuerpo ? r.cuerpo : "");
    } else {
        bitacora("alarma", "ack de %s: HTTP %ld", codigo, r.http);
    }
    resp_liberar(&r);
}

static void al_llegar_mensaje(const char *topico, size_t tlen, const char *texto, void *ud) {
    (void)topico; (void)tlen;
    Oyente *o = ud;
    if (strncmp(texto, "ALARMA;", 7) != 0) return; /* NINGUNA y RELOJ no interesan */

    char copia[128];
    if (snprintf(copia, sizeof copia, "%s", texto + 7) >= (int)sizeof copia) return;
    char *codigo = copia;
    char *maquina = strchr(codigo, ';');
    if (!maquina) return;
    *maquina++ = '\0';
    char *ts = strchr(maquina, ';');
    if (!ts) return;
    *ts++ = '\0';
    if (!codigo_alarma_valido(codigo)) return;

    if (datos_alarma_guardar(o->db, codigo, maquina, strtol(ts, NULL, 10), (long)time(NULL)) != 0)
        return;
    if (datos_alarma_confirmada(o->db, codigo) == 1) return; /* retenida y ya confirmada */
    confirmar_alarma(o, codigo);
}

static int orden_escuchar(sqlite3 *db, Red *red) {
    Oyente o = { db, red };
    char topico[96];
    snprintf(topico, sizeof topico, "itla/planta/%s/alarma", red->matricula);
    bitacora("escuchar", "suscrito a %s; Ctrl+C para salir", topico);
    return red_mqtt_escuchar(red, topico, al_llegar_mensaje, &o, &g_parar) == 0 ? 0 : 1;
}

/* ============ extra: estado ============ */

static int orden_estado(sqlite3 *db, Red *red) {
    printf("cola local: %ld lecturas sin enviar (ultima seq %ld)\n",
           datos_telem_pendientes(db), datos_ultima_seq(db));
    Resp r = {0};
    int ok = pedir_con_reintentos(red, "/api/v1/mi-progreso", NULL, 200, &r);
    if (ok && r.cuerpo) printf("%s", r.cuerpo);
    resp_liberar(&r);
    return ok ? 0 : 1;
}

/* ============ despachador por tabla ============ */

typedef struct {
    const char *nombre;
    int (*ejecutar)(sqlite3 *, Red *);
    const char *ayuda;
} Orden;

static const Orden ORDENES[] = {
    { "config",    orden_config,    "nivel 0: lee /config" },
    { "descargar", orden_descargar, "nivel 1: baja las lecturas de planta" },
    { "paralelo",  orden_descargar_paralelo, "extra: descarga en paralelo (curl multi)" },
    { "analizar",  orden_analizar,  "nivel 2: estadisticas por maquina" },
    { "enviar",    orden_enviar,    "niveles 3 y 4: telemetria y latido" },
    { "escuchar",  orden_escuchar,  "nivel 5: alarmas MQTT" },
    { "estado",    orden_estado,    "extra: cola local y /mi-progreso" },
};

static void mostrar_uso(const char *prog) {
    fprintf(stderr, "Uso: %s <orden>\n", prog);
    for (size_t i = 0; i < sizeof ORDENES / sizeof ORDENES[0]; i++)
        fprintf(stderr, "  %-10s %s\n", ORDENES[i].nombre, ORDENES[i].ayuda);
    fprintf(stderr, "Variables: ITLA_HOST, ITLA_MATRICULA, ITLA_TOKEN\n");
}

int main(int argc, char **argv) {
    if (argc != 2) { mostrar_uso(argv[0]); return 2; }

    const Orden *orden = NULL;
    for (size_t i = 0; i < sizeof ORDENES / sizeof ORDENES[0]; i++)
        if (strcmp(argv[1], ORDENES[i].nombre) == 0) orden = &ORDENES[i];
    if (!orden) { mostrar_uso(argv[0]); return 2; }

    #ifdef _WIN32
    signal(SIGINT, al_senal);
    signal(SIGTERM, al_senal);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = al_senal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
#endif

    Red red;
    if (red_abrir(&red) != 0) return 1;
    sqlite3 *db = datos_abrir(RUTA_DB);
    if (!db) { red_cerrar(&red); return 1; }

    int rc = orden->ejecutar(db, &red);

    sqlite3_close(db);
    red_cerrar(&red);
    return rc;
}
