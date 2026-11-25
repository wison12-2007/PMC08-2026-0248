#ifndef DATOS_H
#define DATOS_H

#include <stddef.h>
#include "sqlite3.h"

typedef struct {
    long seq;
    long ts;
    double temp;
    double corriente;
} Telem;

sqlite3 *datos_abrir(const char *ruta);
/* Solo para SQL fijo, sin datos variables (BEGIN, COMMIT, PRAGMA...) */
int datos_exec(sqlite3 *db, const char *sql);

/* config */
int datos_config_desde_texto(sqlite3 *db, const char *texto);
int datos_config_get(sqlite3 *db, const char *clave, char *out, size_t n); /* 0 si existe */

/* nivel 1 y 2 */
int datos_importar_csv(sqlite3 *db, const char *csv, size_t len, long *max_id, int *filas);
int datos_lecturas_contiguas(sqlite3 *db); /* 1 si ids 1..MAX sin huecos */
int datos_totales_descarga(sqlite3 *db, long *filas, long long *suma_ids);
int datos_analisis_json(sqlite3 *db, double umbral, char *out, size_t n);

/* nivel 3 */
long datos_ultima_seq(sqlite3 *db);
int  datos_telem_guardar(sqlite3 *db, const Telem *t);
int  datos_telem_pendiente(sqlite3 *db, Telem *t);   /* 1 hay, 0 no hay, -1 error */
int  datos_telem_marcar_enviado(sqlite3 *db, long seq);
int  datos_telem_intento(sqlite3 *db, long seq);
long datos_telem_pendientes(sqlite3 *db);

/* nivel 5 */
int datos_alarma_guardar(sqlite3 *db, const char *codigo, const char *maquina,
                         long ts_servidor, long ts_recibida);
int datos_alarma_confirmada(sqlite3 *db, const char *codigo); /* 1 si ya tiene ts_ack */
int datos_alarma_marcar_ack(sqlite3 *db, const char *codigo, long ts_ack);

#endif
