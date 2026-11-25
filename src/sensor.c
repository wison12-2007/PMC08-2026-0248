#include "sensor.h"

#include <math.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

static double ruido(double amplitud) {
    return ((double)rand() / (double)RAND_MAX * 2.0 - 1.0) * amplitud;
}

static double limitar(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static double temp_simulada(time_t ahora) {
    double s = (double)(ahora % 100000);
    return limitar(35.0 + 7.0 * sin(2.0 * PI * s / 600.0) + ruido(1.5), 25.0, 45.0);
}

static double corriente_simulada(time_t ahora) {
    double s = (double)(ahora % 100000);
    return limitar(3.5 + 1.2 * sin(2.0 * PI * s / 240.0) + ruido(0.3), 2.0, 5.0);
}

#ifdef USAR_DHT22
/* ---- DHT22 real por GPIO con libgpiod v1 (make DHT22=1) ----
 * Variables opcionales: DHT_CHIP (por defecto gpiochip0), DHT_GPIO (por defecto 4).
 * Si una lectura falla (checksum, tiempo agotado), se usa la simulada. */
#include <gpiod.h>
#include <stdio.h>
#include <string.h>

static struct gpiod_chip *g_chip;
static struct gpiod_line *g_linea;
static int g_dht_ok;

static long long ahora_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* Espera hasta que la linea valga `nivel`; devuelve los us esperados o -1 */
static long esperar_nivel(int nivel, long limite_us) {
    long long t0 = ahora_ns();
    while (gpiod_line_get_value(g_linea) != nivel) {
        if ((ahora_ns() - t0) / 1000 > limite_us) return -1;
    }
    return (long)((ahora_ns() - t0) / 1000);
}

static int dht22_leer(double *temp) {
    unsigned char b[5] = {0};

    /* el host baja la linea >= 1 ms para pedir una lectura */
    if (gpiod_line_set_config(g_linea, GPIOD_LINE_REQUEST_DIRECTION_OUTPUT, 0, 0) < 0) return -1;
    if (gpiod_line_set_value(g_linea, 0) < 0) return -1;
    struct timespec t = { 0, 2000000L };
    nanosleep(&t, NULL);
    if (gpiod_line_set_config(g_linea, GPIOD_LINE_REQUEST_DIRECTION_INPUT, 0, 0) < 0) return -1;

    /* respuesta del sensor: ~80 us en bajo, ~80 us en alto */
    if (esperar_nivel(0, 200) < 0) return -1;
    if (esperar_nivel(1, 150) < 0) return -1;
    if (esperar_nivel(0, 150) < 0) return -1;

    for (int i = 0; i < 40; i++) {
        if (esperar_nivel(1, 100) < 0) return -1;     /* fin de los ~50 us en bajo */
        long alto = esperar_nivel(0, 120);            /* ~27 us = 0, ~70 us = 1 */
        if (alto < 0) return -1;
        b[i / 8] = (unsigned char)((b[i / 8] << 1) | (alto > 45 ? 1 : 0));
    }
    if ((unsigned char)(b[0] + b[1] + b[2] + b[3]) != b[4]) return -1;

    double v = (double)(((b[2] & 0x7F) << 8) | b[3]) / 10.0;
    *temp = (b[2] & 0x80) ? -v : v;
    return 0;
}
#endif

void sensor_iniciar(void) {
    srand((unsigned)time(NULL));
#ifdef USAR_DHT22
    const char *chip = getenv("DHT_CHIP");
    const char *pin = getenv("DHT_GPIO");
    int linea = pin ? atoi(pin) : 4;
    g_chip = gpiod_chip_open_by_name(chip && *chip ? chip : "gpiochip0");
    if (g_chip) g_linea = gpiod_chip_get_line(g_chip, (unsigned int)linea);
    if (g_linea && gpiod_line_request_input(g_linea, "estacion-dht22") == 0) g_dht_ok = 1;
    if (!g_dht_ok) fprintf(stderr, "DHT22 no disponible; se usan lecturas simuladas\n");
#endif
}

void sensor_cerrar(void) {
#ifdef USAR_DHT22
    if (g_linea) gpiod_line_release(g_linea);
    if (g_chip) gpiod_chip_close(g_chip);
    g_linea = NULL;
    g_chip = NULL;
    g_dht_ok = 0;
#endif
}

static int g_ultimo_real;

const char *sensor_origen(void) {
#ifdef USAR_DHT22
    return g_dht_ok ? "dht22" : "simulado";
#else
    return "simulado";
#endif
}

Muestra sensor_leer(time_t ahora) {
    Muestra m;
    m.temp = temp_simulada(ahora);
    m.corriente = corriente_simulada(ahora);
    g_ultimo_real = 0;
#ifdef USAR_DHT22
    if (g_dht_ok) {
        for (int intento = 0; intento < 5; intento++) {
            double t;
            if (dht22_leer(&t) == 0) { m.temp = t; g_ultimo_real = 1; break; }
            struct timespec e = { 0, 300000000L }; /* el DHT22 pide >= 2 s entre lecturas; el bucle ya espera */
            nanosleep(&e, NULL);
        }
    }
#endif
    (void)g_ultimo_real;
    return m;
}
