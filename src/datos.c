#include "datos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char ESQUEMA[] =
    "CREATE TABLE IF NOT EXISTS config("
    "  clave TEXT PRIMARY KEY, valor TEXT);"
    "CREATE TABLE IF NOT EXISTS lecturas_planta("
    "  id INTEGER PRIMARY KEY, maquina TEXT, ts INTEGER, temp REAL,"
    "  corriente REAL, vibracion REAL, estado TEXT);"
    "CREATE TABLE IF NOT EXISTS telemetria("
    "  seq INTEGER PRIMARY KEY, ts INTEGER NOT NULL, temp REAL NOT NULL,"
    "  corriente REAL NOT NULL,"
    "  enviado INTEGER NOT NULL DEFAULT 0 CHECK(enviado IN (0,1)),"
    "  intentos INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS alarmas("
    "  codigo TEXT PRIMARY KEY, maquina TEXT, ts_servidor INTEGER,"
    "  ts_recibida INTEGER, ts_ack INTEGER);";

static int fallo(sqlite3 *db, const char *ctx) {
    fprintf(stderr, "SQLite (%s): %s\n", ctx, sqlite3_errmsg(db));
    return -1;
}

int datos_exec(sqlite3 *db, const char *sql) {
    char *msg = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &msg) != SQLITE_OK) {
        fprintf(stderr, "SQLite (%s): %s\n", sql, msg ? msg : "error");
        sqlite3_free(msg);
        return -1;
    }
    return 0;
}

sqlite3 *datos_abrir(const char *ruta) {
    sqlite3 *db = NULL;
    if (sqlite3_open(ruta, &db) != SQLITE_OK) {
        fprintf(stderr, "No se pudo abrir %s: %s\n", ruta, db ? sqlite3_errmsg(db) : "sin memoria");
        sqlite3_close(db);
        return NULL;
    }
    sqlite3_busy_timeout(db, 2000);
    if (datos_exec(db, "PRAGMA journal_mode=WAL;") != 0 || datos_exec(db, ESQUEMA) != 0) {
        sqlite3_close(db);
        return NULL;
    }
    return db;
}

/* ---------- config ---------- */

int datos_config_desde_texto(sqlite3 *db, const char *texto) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO config(clave, valor) VALUES(?, ?)",
                           -1, &st, NULL) != SQLITE_OK)
        return fallo(db, "config prepare");

    int rc = 0;
    const char *p = texto;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t ll = nl ? (size_t)(nl - p) : strlen(p);
        char linea[256];
        if (ll > 0 && p[ll - 1] == '\r') ll--;
        if (ll > 0 && ll < sizeof linea) {
            memcpy(linea, p, ll);
            linea[ll] = '\0';
            char *eq = strchr(linea, '=');
            if (eq) {
                *eq = '\0';
                sqlite3_reset(st);
                sqlite3_bind_text(st, 1, linea, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 2, eq + 1, -1, SQLITE_TRANSIENT);
                if (sqlite3_step(st) != SQLITE_DONE) { rc = fallo(db, "config step"); break; }
            }
        }
        p = nl ? nl + 1 : p + strlen(p);
    }
    sqlite3_finalize(st);
    return rc;
}

int datos_config_get(sqlite3 *db, const char *clave, char *out, size_t n) {
    sqlite3_stmt *st = NULL;
    int rc = -1;
    if (sqlite3_prepare_v2(db, "SELECT valor FROM config WHERE clave = ?", -1, &st, NULL) != SQLITE_OK)
        return fallo(db, "config_get prepare");
    sqlite3_bind_text(st, 1, clave, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *v = sqlite3_column_text(st, 0);
        if (v && snprintf(out, n, "%s", (const char *)v) < (int)n) rc = 0;
    }
    sqlite3_finalize(st);
    return rc;
}

/* ---------- nivel 1: descarga ---------- */

static void bind_txt(sqlite3_stmt *st, int i, const char *s) {
    if (*s) sqlite3_bind_text(st, i, s, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(st, i);
}

static void bind_ent(sqlite3_stmt *st, int i, const char *s) {
    char *fin = NULL;
    long long v = strtoll(s, &fin, 10);
    if (*s && *fin == '\0') sqlite3_bind_int64(st, i, v);
    else sqlite3_bind_null(st, i);
}

static void bind_real(sqlite3_stmt *st, int i, const char *s) {
    char *fin = NULL;
    double v = strtod(s, &fin);
    if (*s && *fin == '\0') sqlite3_bind_double(st, i, v);
    else sqlite3_bind_null(st, i);
}

int datos_importar_csv(sqlite3 *db, const char *csv, size_t len, long *max_id, int *filas) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "INSERT OR IGNORE INTO lecturas_planta"
            "(id, maquina, ts, temp, corriente, vibracion, estado) VALUES(?,?,?,?,?,?,?)",
            -1, &st, NULL) != SQLITE_OK)
        return fallo(db, "importar prepare");

    *filas = 0;
    int rc = 0, primera = 1;
    const char *p = csv, *fin = csv + len;
    while (p < fin) {
        const char *nl = memchr(p, '\n', (size_t)(fin - p));
        size_t ll = nl ? (size_t)(nl - p) : (size_t)(fin - p);
        const char *linea = p;
        p = nl ? nl + 1 : fin;
        if (ll > 0 && linea[ll - 1] == '\r') ll--;
        if (ll == 0) continue;
        if (primera) { primera = 0; continue; } /* cabecera */

        (*filas)++;
        char copia[256];
        if (ll >= sizeof copia) continue;
        memcpy(copia, linea, ll);
        copia[ll] = '\0';

        /* separa por comas conservando los campos vacios (no strtok) */
        char *campo[7];
        int nc = 1;
        campo[0] = copia;
        for (char *c = copia; *c; c++) {
            if (*c == ',') {
                if (nc >= 7) { nc = 99; break; }
                *c = '\0';
                campo[nc++] = c + 1;
            }
        }
        if (nc != 7) continue;

        char *e = NULL;
        long id = strtol(campo[0], &e, 10);
        if (campo[0][0] == '\0' || *e != '\0') continue;
        if (id > *max_id) *max_id = id;

        sqlite3_reset(st);
        sqlite3_clear_bindings(st);
        sqlite3_bind_int64(st, 1, id);
        bind_txt(st, 2, campo[1]);
        bind_ent(st, 3, campo[2]);
        bind_real(st, 4, campo[3]);
        bind_real(st, 5, campo[4]);
        bind_real(st, 6, campo[5]);
        bind_txt(st, 7, campo[6]);
        if (sqlite3_step(st) != SQLITE_DONE) { rc = fallo(db, "importar step"); break; }
    }
    sqlite3_finalize(st);
    return rc;
}

int datos_totales_descarga(sqlite3 *db, long *filas, long long *suma_ids) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*), COALESCE(SUM(id), 0) FROM lecturas_planta",
                           -1, &st, NULL) != SQLITE_OK)
        return fallo(db, "totales prepare");
    int rc = -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        *filas = (long)sqlite3_column_int64(st, 0);
        *suma_ids = sqlite3_column_int64(st, 1);
        rc = 0;
    } else {
        fallo(db, "totales step");
    }
    sqlite3_finalize(st);
    return rc;
}

int datos_lecturas_contiguas(sqlite3 *db) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) = COALESCE(MAX(id), 0) FROM lecturas_planta",
                           -1, &st, NULL) != SQLITE_OK)
        return fallo(db, "contiguas prepare");
    int rc = -1;
    if (sqlite3_step(st) == SQLITE_ROW) rc = sqlite3_column_int(st, 0);
    else fallo(db, "contiguas step");
    sqlite3_finalize(st);
    return rc;
}

/* ---------- nivel 2: analisis en SQL ---------- */

static const char SQL_ANALISIS[] =
    "SELECT maquina, SUM(v), COUNT(*) - SUM(v), "
    "  MIN(CASE WHEN v = 1 THEN temp END), "
    "  MAX(CASE WHEN v = 1 THEN temp END), "
    "  ROUND(AVG(CASE WHEN v = 1 THEN temp END), 2), "
    "  SUM(CASE WHEN v = 1 AND temp > ? THEN 1 ELSE 0 END) "
    "FROM (SELECT maquina, temp, "
    "        CASE WHEN estado = 'OK' AND temp IS NOT NULL AND temp <> -999.0 "
    "              AND ts IS NOT NULL AND corriente IS NOT NULL "
    "              AND vibracion IS NOT NULL THEN 1 ELSE 0 END AS v "
    "      FROM lecturas_planta WHERE maquina IS NOT NULL AND maquina <> '') "
    "GROUP BY maquina ORDER BY maquina";

int datos_analisis_json(sqlite3 *db, double umbral, char *out, size_t n) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL_ANALISIS, -1, &st, NULL) != SQLITE_OK)
        return fallo(db, "analisis prepare");
    sqlite3_bind_double(st, 1, umbral);

    size_t off = 0;
    int w = snprintf(out, n, "{\"maquinas\":[");
    if (w < 0 || (size_t)w >= n) { sqlite3_finalize(st); return -1; }
    off = (size_t)w;

    int filas = 0, rc = 0, s;
    while ((s = sqlite3_step(st)) == SQLITE_ROW) {
        w = snprintf(out + off, n - off,
            "%s{\"maquina\":\"%s\",\"validas\":%lld,\"descartadas\":%lld,"
            "\"temp_min\":%.2f,\"temp_max\":%.2f,\"temp_prom\":%.2f,\"sobre_umbral\":%lld}",
            filas ? "," : "",
            (const char *)sqlite3_column_text(st, 0),
            (long long)sqlite3_column_int64(st, 1),
            (long long)sqlite3_column_int64(st, 2),
            sqlite3_column_double(st, 3),
            sqlite3_column_double(st, 4),
            sqlite3_column_double(st, 5),
            (long long)sqlite3_column_int64(st, 6));
        if (w < 0 || (size_t)w >= n - off) { rc = -1; break; }
        off += (size_t)w;
        filas++;
    }
    if (rc == 0 && s != SQLITE_DONE) rc = fallo(db, "analisis step");
    sqlite3_finalize(st);
    if (rc == 0 && snprintf(out + off, n - off, "]}") >= (int)(n - off)) rc = -1;
    if (rc == 0 && filas == 0) {
        fprintf(stderr, "No hay lecturas: ejecuta primero ./estacion descargar\n");
        rc = -1;
    }
    return rc;
}

/* ---------- nivel 3: telemetria ---------- */

long datos_ultima_seq(sqlite3 *db) {
    sqlite3_stmt *st = NULL;
    long v = -1;
    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(seq), 0) FROM telemetria", -1, &st, NULL) != SQLITE_OK) {
        fallo(db, "ultima_seq prepare");
        return -1;
    }
    if (sqlite3_step(st) == SQLITE_ROW) v = (long)sqlite3_column_int64(st, 0);
    else fallo(db, "ultima_seq step");
    sqlite3_finalize(st);
    return v;
}

int datos_telem_guardar(sqlite3 *db, const Telem *t) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO telemetria(seq, ts, temp, corriente, enviado, intentos) VALUES(?,?,?,?,0,0)",
            -1, &st, NULL) != SQLITE_OK)
        return fallo(db, "telem_guardar prepare");
    sqlite3_bind_int64(st, 1, t->seq);
    sqlite3_bind_int64(st, 2, t->ts);
    sqlite3_bind_double(st, 3, t->temp);
    sqlite3_bind_double(st, 4, t->corriente);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : fallo(db, "telem_guardar step");
    sqlite3_finalize(st);
    return rc;
}

int datos_telem_pendiente(sqlite3 *db, Telem *t) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT seq, ts, temp, corriente FROM telemetria WHERE enviado = 0 ORDER BY seq LIMIT 1",
            -1, &st, NULL) != SQLITE_OK)
        return fallo(db, "pendiente prepare");
    int rc, s = sqlite3_step(st);
    if (s == SQLITE_ROW) {
        t->seq = (long)sqlite3_column_int64(st, 0);
        t->ts = (long)sqlite3_column_int64(st, 1);
        t->temp = sqlite3_column_double(st, 2);
        t->corriente = sqlite3_column_double(st, 3);
        rc = 1;
    } else if (s == SQLITE_DONE) {
        rc = 0;
    } else {
        rc = fallo(db, "pendiente step");
    }
    sqlite3_finalize(st);
    return rc;
}

static int actualizar_seq(sqlite3 *db, const char *sql, long seq) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return fallo(db, "update prepare");
    sqlite3_bind_int64(st, 1, seq);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : fallo(db, "update step");
    sqlite3_finalize(st);
    return rc;
}

int datos_telem_marcar_enviado(sqlite3 *db, long seq) {
    return actualizar_seq(db, "UPDATE telemetria SET enviado = 1, intentos = intentos + 1 WHERE seq = ?", seq);
}

int datos_telem_intento(sqlite3 *db, long seq) {
    return actualizar_seq(db, "UPDATE telemetria SET intentos = intentos + 1 WHERE seq = ?", seq);
}

long datos_telem_pendientes(sqlite3 *db) {
    sqlite3_stmt *st = NULL;
    long v = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM telemetria WHERE enviado = 0", -1, &st, NULL) != SQLITE_OK) {
        fallo(db, "pendientes prepare");
        return 0;
    }
    if (sqlite3_step(st) == SQLITE_ROW) v = (long)sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return v;
}

/* ---------- nivel 5: alarmas ---------- */

int datos_alarma_guardar(sqlite3 *db, const char *codigo, const char *maquina,
                         long ts_servidor, long ts_recibida) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "INSERT OR IGNORE INTO alarmas(codigo, maquina, ts_servidor, ts_recibida) VALUES(?,?,?,?)",
            -1, &st, NULL) != SQLITE_OK)
        return fallo(db, "alarma_guardar prepare");
    sqlite3_bind_text(st, 1, codigo, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, maquina, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, ts_servidor);
    sqlite3_bind_int64(st, 4, ts_recibida);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : fallo(db, "alarma_guardar step");
    sqlite3_finalize(st);
    return rc;
}

int datos_alarma_confirmada(sqlite3 *db, const char *codigo) {
    sqlite3_stmt *st = NULL;
    int rc = 0;
    if (sqlite3_prepare_v2(db, "SELECT ts_ack IS NOT NULL FROM alarmas WHERE codigo = ?",
                           -1, &st, NULL) != SQLITE_OK)
        return fallo(db, "alarma_confirmada prepare");
    sqlite3_bind_text(st, 1, codigo, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) rc = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return rc;
}

int datos_alarma_marcar_ack(sqlite3 *db, const char *codigo, long ts_ack) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "UPDATE alarmas SET ts_ack = ? WHERE codigo = ?", -1, &st, NULL) != SQLITE_OK)
        return fallo(db, "alarma_ack prepare");
    sqlite3_bind_int64(st, 1, ts_ack);
    sqlite3_bind_text(st, 2, codigo, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : fallo(db, "alarma_ack step");
    sqlite3_finalize(st);
    return rc;
}
