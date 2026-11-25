#ifndef SENSOR_H
#define SENSOR_H

#include <time.h>

typedef struct {
    double temp;      /* C */
    double corriente; /* A (siempre simulada: el DHT22 no mide corriente) */
} Muestra;

void sensor_iniciar(void);
void sensor_cerrar(void);
Muestra sensor_leer(time_t ahora);
const char *sensor_origen(void); /* "dht22" o "simulado" */

#endif
