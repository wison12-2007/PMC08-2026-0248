#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "reto.h"

int calcular(const int muestras[], int m, int tipo, int p, int *estado)
{
    int k = tipo + 2;
    int racha = 0;
    int activo = 0;
    int score = 0;

    for (int i = 0; i < m; i++) {
        if (muestras[i] >= p) {
            racha++;
            if (!activo && racha >= k) {
                activo = 1;
                score++;
            }
        } else {
            racha = 0;
            activo = 0;
        }
    }
    *estado = activo;
    return score;
}

int total_rec(const Registro regs[], int n)
{
    if (n == 0) {
        return 0;
    }
    return regs[n - 1].score + total_rec(regs, n - 1);
}

void ordenar_por_score(Registro regs[], int n)
{
    for (int i = 1; i < n; i++) {
        Registro actual = regs[i];
        int j = i - 1;
        while (j >= 0 && (regs[j].score < actual.score ||
                          (regs[j].score == actual.score &&
                           regs[j].id > actual.id))) {
            regs[j + 1] = regs[j];
            j--;
        }
        regs[j + 1] = actual;
    }
}

void ordenar_por_id(Registro regs[], int n)
{
    for (int i = 1; i < n; i++) {
        Registro actual = regs[i];
        int j = i - 1;
        while (j >= 0 && regs[j].id > actual.id) {
            regs[j + 1] = regs[j];
            j--;
        }
        regs[j + 1] = actual;
    }
}

int buscar(const Registro porId[], int n, int q)
{
    int bajo = 0;
    int alto = n - 1;

    while (bajo <= alto) {
        int medio = bajo + (alto - bajo) / 2;
        if (porId[medio].id == q) {
            return porId[medio].score;
        }
        if (porId[medio].id < q) {
            bajo = medio + 1;
        } else {
            alto = medio - 1;
        }
    }
    return -1;
}

/* Verifica que la etiqueta tenga 1..15 caracteres alfanumericos o '_'. */
static int nombre_valido(const char *s)
{
    size_t len = strlen(s);

    if (len < 1 || len > LONG_NOMBRE - 1) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!isalnum(c) && c != '_') {
            return 0;
        }
    }
    return 1;
}

/* Lee y valida TODA la entrada. Devuelve 1 si es valida, 0 si hay error. */
static int leer_entrada(int *n, int *m, int *q, Registro regs[],
                        int muestras[][MAX_MUESTRAS])
{
    if (scanf("%d %d %d", n, m, q) != 3) {
        return 0;
    }
    if (*n < 1 || *n > MAX_UNIDADES || *m < 1 || *m > MAX_MUESTRAS ||
        *q < 1 || *q > MAX_ID) {
        return 0;
    }

    for (int i = 0; i < *n; i++) {
        char etiqueta[64];

        if (scanf("%d %63s %d %d", &regs[i].id, etiqueta,
                  &regs[i].config.tipo, &regs[i].config.p) != 4) {
            return 0;
        }
        if (regs[i].id < 1 || regs[i].id > MAX_ID ||
            !nombre_valido(etiqueta) ||
            (regs[i].config.tipo != 0 && regs[i].config.tipo != 1) ||
            regs[i].config.p < 0 || regs[i].config.p > 100) {
            return 0;
        }
        strcpy(regs[i].nombre, etiqueta);

        for (int j = 0; j < i; j++) {
            if (regs[j].id == regs[i].id) {
                return 0;
            }
        }
        for (int j = 0; j < *m; j++) {
            if (scanf("%d", &muestras[i][j]) != 1) {
                return 0;
            }
            if (muestras[i][j] < 0 || muestras[i][j] > 100) {
                return 0;
            }
        }
    }
    return 1;
}

int main(void)
{
    int n = 0;
    int m = 0;
    int q = 0;
    Registro regs[MAX_UNIDADES];
    int muestras[MAX_UNIDADES][MAX_MUESTRAS];

    if (!leer_entrada(&n, &m, &q, regs, muestras)) {
        printf("ERROR\n");
        return 0;
    }

    for (int i = 0; i < n; i++) {
        regs[i].score = calcular(muestras[i], m, regs[i].config.tipo,
                                 regs[i].config.p, &regs[i].estado);
        printf("%d %d %d\n", regs[i].id, regs[i].score, regs[i].estado);
    }
    printf("TOTAL %d\n", total_rec(regs, n));

    Registro copia[MAX_UNIDADES];

    for (int i = 0; i < n; i++) {
        copia[i] = regs[i];
    }
    ordenar_por_score(copia, n);
    printf("ORDER");
    for (int i = 0; i < n; i++) {
        printf(" %d", copia[i].id);
    }
    printf("\n");

    for (int i = 0; i < n; i++) {
        copia[i] = regs[i];
    }
    ordenar_por_id(copia, n);
    printf("FIND %d %d\n", q, buscar(copia, n, q));
    return 0;
}
