# PMC08-2026-0248 — Simulador de celda mecatrónica (C11)

- **Matrícula:** 2026-0248
- **Correo institucional:** 20260248@itla.edu.do
- **Asignatura:** Programación para Mecatrónicos

## Modelo y decisiones
Cada unidad se procesa de forma independiente con `calcular` (racha, activo y
score locales). Una muestra es alta si `x >= p`; la activación requiere
`k = tipo + 2` altas consecutivas y las altas extra no suman. El estado se
devuelve por apuntador (`int *estado`). Los registros usan `struct` anidada
(`Config` con `tipo` y `p`) y una matriz `int muestras[20][8]`. Toda la entrada
se valida antes de imprimir; si hay un error se imprime solo `ERROR`.
`total_rec` es recursivo, el ordenamiento es por inserción y la búsqueda es
binaria sobre una copia ordenada por id. Detalle en `plan.md`.

## Compilar y ejecutar
```
gcc -std=c11 -Wall -Wextra -Wpedantic main.c -o reto
./reto < pruebas/entradas/pub1.in          # Linux/macOS
reto.exe < pruebas\entradas\pub1.in        # Windows (cmd)
python3 evaluar.py publicas.json ./reto    # banco público
python3 ejecutar_pruebas.py ./reto         # pruebas públicas + propias
```

## Resultados
- Banco público (`evaluar.py`): 6 / 6 grupos OK.
- Pruebas propias y públicas (`ejecutar_pruebas.py`): 23 / 23 OK, detalle en
  `pruebas/resultados.md`. Casos propios: normales, bordes (n=1/m=1, p=0, id
  9999, 20x8), q ausente, empates, y 12 entradas inválidas.
- Compila sin advertencias con `-Wall -Wextra -Wpedantic`; ejecutado también
  con `-fsanitize=address,undefined` sin errores.
- Traza manual y fallo corregido: `pruebas/trazas.md`.

## Fuentes consultadas
- Corona Nakamura, M. A. y Ancona Valdez, M. de los Á. (2011). *Diseño de
  algoritmos y su codificación en lenguaje C*. McGraw-Hill.
- Documentación de la biblioteca estándar de C (`stdio.h`, `string.h`,
  `ctype.h`).

## Historial de commits
| Commit | Avance |
|---|---|
| (hash) | 1. Estructura, interpretación del problema y plan (`plan.md`, `reto.h`) |
| (hash) | 2. Desarrollo parcial verificable (`calcular`, lectura y validación) |
| (hash) | 3. Pruebas, validación y revisión (`pruebas/`, `evaluar.py`) |
| (hash) | 4. Correcciones finales y documentación (`README.md`) |

El hash del commit final se entrega aparte, junto con el enlace del repositorio.
