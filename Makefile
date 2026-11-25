# Opciones (cambiar una opcion exige `make clean` antes):
#   make DHT22=1   sensor DHT22 real con libgpiod (sudo apt install libgpiod-dev)
#   make CJSON=1   arma los cuerpos JSON con cJSON (lib/cjson/cJSON.c y cJSON.h)
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L -Ilib/sqlite -MMD -MP
LDLIBS  = -lcurl -lm
SRC     = src/main.c src/red.c src/datos.c src/sensor.c src/json.c
OBJ     = $(patsubst src/%.c,build/%.o,$(SRC)) build/sqlite3.o

ifeq ($(DHT22),1)
CFLAGS += -DUSAR_DHT22
LDLIBS += -lgpiod
endif

ifeq ($(CJSON),1)
CFLAGS += -DUSAR_CJSON -Ilib/cjson
OBJ    += build/cJSON.o
endif

all: estacion

estacion: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/sqlite3.o: lib/sqlite/sqlite3.c | build
	$(CC) -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -Ilib/sqlite -c $< -o $@

build/cJSON.o: lib/cjson/cJSON.c | build
	$(CC) -O2 -Ilib/cjson -c $< -o $@

build:
	mkdir -p build

run: estacion
	./estacion config

clean:
	rm -rf build estacion

-include $(OBJ:.o=.d)

.PHONY: all run clean
