# estacion: estación de telemetría ITLA

Programa en C11 con libcurl y SQLite. Un solo ejecutable, `estacion`, con cinco subcomandos que comparten `estacion.db`.

## Compilar

1. Descarga el *amalgamation* de SQLite (sqlite.org) y copia `sqlite3.c` y `sqlite3.h` en `lib/sqlite/`.
2. Instala libcurl con soporte MQTT (`curl --version` debe listar `mqtt` en *Protocols*):
   `sudo apt install libcurl4-openssl-dev`
3. `make` (compila con `-std=c11 -Wall -Wextra`). `make clean` limpia.

## Configurar y usar

```
export ITLA_HOST=10.0.0.200        # o itla-meca.duckdns.org desde casa
export ITLA_MATRICULA=...
export ITLA_TOKEN=...              # 32 caracteres; nunca en el código

./estacion config      # nivel 0: lee /config y lo guarda en la tabla config
./estacion descargar   # nivel 1: pagina /lecturas y envía la respuesta
./estacion analizar    # nivel 2: estadísticas con SQL y envío
./estacion enviar      # niveles 3 y 4: telemetría con cola + latido MQTT
./estacion escuchar    # nivel 5: alarmas MQTT, guardado y ack (otra terminal)
./estacion estado      # extra: cola local y /mi-progreso
./estacion paralelo    # extra: descarga en paralelo con la API multi de libcurl
```

`enviar` y `escuchar` deben correr a la vez: el servidor solo manda alarmas si hubo un latido en los últimos 60 s. Para una sesión de 15 min o más, déjalos corriendo sin interrumpir.

## Decisiones de diseño

- **Estructura de main.c**: despachador por tabla de órdenes, el envío como una máquina de estados (`Emisor`) y una bitácora con hora.
- **Módulos**: `red.c` (todo libcurl), `datos.c` (todo SQLite), `sensor.c` (lectura simulada), `main.c` (subcomandos).
- **Errores en dos niveles**: cada petición guarda `CURLcode` y código HTTP en `Resp`; `resp_ok` los revisa juntos.
- **Descarga**: una transacción `BEGIN`/`COMMIT` para todas las páginas; `INSERT OR IGNORE` sobre `id PRIMARY KEY` absorbe los duplicados. El CSV se separa a mano (no `strtok`) para conservar campos vacíos, que quedan como `NULL`.
- **Análisis**: una sola consulta con `CASE`, `GROUP BY`, `MIN/MAX/AVG` y el umbral como parámetro `?`.
- **Cola resiliente**: cada lectura se guarda con `enviado = 0` y se envía en orden de `seq`. El próximo `seq` sale de `MAX(seq)` en SQLite, así que sobrevive a reinicios. Ante 503 se espera `Retry-After` (+1 s) sin hacer más peticiones; ante fallos de red, 1, 2, 4 … 30 s. Máximo 4 envíos por segundo para no recibir 429.
- **Latido**: publicado desde el mismo bucle; si falla, se pierde (QoS 0) y no se encola.
- **Suscripción**: se decodifica el formato binario de libcurl (2 bytes de largo de tópico + tópico + texto con `\n`). Si la conexión cae, se reconecta con espera creciente. El `ack` usa otro handle. Las alarmas ya confirmadas (retenidas) se ignoran.
- **SQLite concurrente**: `PRAGMA journal_mode=WAL` y `sqlite3_busy_timeout(db, 2000)`.
- **Seguridad**: token y host solo por variables de entorno; el token nunca se imprime.

## Uso de herramientas de IA

Utilicé Claude únicamente como apoyo para revisar y verificar el código. La implementación y las pruebas del proyecto fueron revisadas para comprender su funcionamiento.


- **Descarga en paralelo** (`./estacion paralelo`): cinco páginas por ola con `curl_multi`. Supone páginas contiguas; si hay huecos o falla una, hace ROLLBACK y completa con `descargar`.
- **Sensor real**: `make clean && make DHT22=1` (necesita `libgpiod-dev`, libgpiod 1.x). Variables `DHT_CHIP` (por defecto `gpiochip0`) y `DHT_GPIO` (por defecto 4). Si una lectura falla usa la simulada; la corriente siempre es simulada.
- **systemd**: `deploy/estacion-enviar.service` y `deploy/estacion-escuchar.service`, que arrancan tras `network-online.target`. Instalar:
  ```
  sudo mkdir -p /opt/estacion && sudo cp estacion /opt/estacion/
  sudo cp deploy/estacion.env.ejemplo /etc/estacion.env && sudo chmod 600 /etc/estacion.env   # rellenar
  sudo cp deploy/estacion-*.service /etc/systemd/system/
  sudo systemctl daemon-reload && sudo systemctl enable --now estacion-enviar estacion-escuchar
  ```
- **Reconexión y conexión viva**: la suscripción reconecta con espera creciente y activa `CURLOPT_UPKEEP_INTERVAL_MS` (libcurl 7.62 o superior).
- **cJSON**: `make clean && make CJSON=1` (con `cJSON.c`/`cJSON.h` en `lib/cjson/`). Se usa para armar los cuerpos JSON que se envían. Las respuestas del servidor son texto plano, así que no hay JSON que leer de ellas.
