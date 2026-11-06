#ifndef RETO_H
#define RETO_H

/* Capacidades y limites del enunciado */
#define MAX_UNIDADES 20
#define MAX_MUESTRAS 8
#define LONG_NOMBRE 16
#define MAX_ID 9999

/* Configuracion anidada de cada unidad */
typedef struct {
    int tipo; /* 0 o 1; k = tipo + 2 */
    int p;    /* umbral, 0..100 */
} Config;

typedef struct {
    int id;
    char nombre[LONG_NOMBRE];
    Config config;
    int score;
    int estado; /* 0 o 1 */
} Registro;

/* Procesa las muestras de UNA unidad. Devuelve el score y deja el
   estado final (0/1) en *estado. */
int calcular(const int muestras[], int m, int tipo, int p, int *estado);

/* Suma recursiva de los scores; caso base n == 0. */
int total_rec(const Registro regs[], int n);

/* Ordenamiento por insercion sobre una copia: score desc, id asc. */
void ordenar_por_score(Registro regs[], int n);

/* Ordenamiento por insercion sobre una copia: id asc. */
void ordenar_por_id(Registro regs[], int n);

/* Busqueda binaria de q en una copia ordenada por id.
   Devuelve el score o -1 si no existe. */
int buscar(const Registro porId[], int n, int q);

#endif /* RETO_H */
