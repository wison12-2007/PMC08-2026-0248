# Plan y análisis — PMC08-2026-0248

## Análisis
- **Entrada:** `n m q`, luego `n` registros `id nombre tipo p x1..xm` (stdin).
- **Salida:** una línea `id score estado` por unidad (orden original), luego
  `TOTAL`, `ORDER` y `FIND`. Si hay cualquier error: solo `ERROR`.
- **Modelo por unidad:** `k = tipo + 2`; muestra alta si `x >= p`.
  Baja => `racha = 0`, `activo = 0`. Alta => `racha++`; si `racha >= k` y no
  está activo => `activo = 1`, `score++`. `estado = activo` al final.
- **Independencia:** `racha`, `activo` y `score` son locales a `calcular`,
  por lo que se reinician en cada unidad.

## Pseudocódigo
```
calcular(v, m, tipo, p, *estado):
    k <- tipo + 2; racha <- 0; activo <- 0; score <- 0
    para i de 0 a m-1:
        si v[i] >= p:
            racha <- racha + 1
            si activo = 0 y racha >= k: activo <- 1; score <- score + 1
        si no:
            racha <- 0; activo <- 0
    *estado <- activo
    devolver score

total_rec(regs, n):
    si n = 0 devolver 0
    devolver regs[n-1].score + total_rec(regs, n-1)

ordenar_por_score(copia, n):   # inserción
    para i de 1 a n-1: insertar copia[i] en la parte ordenada con criterio
        (score mayor primero; si empatan, id menor primero)

ordenar_por_id(copia, n): inserción por id ascendente

buscar(porId, n, q):           # binaria
    bajo <- 0; alto <- n-1
    mientras bajo <= alto:
        medio <- bajo + (alto-bajo)/2
        si porId[medio].id = q devolver su score
        si porId[medio].id < q: bajo <- medio+1 si no alto <- medio-1
    devolver -1

main:
    leer y VALIDAR toda la entrada (rangos, id únicos, etiqueta, incompleta)
    si hay error: imprimir ERROR y terminar
    calcular cada unidad; imprimir líneas; TOTAL (recursivo)
    copia ordenada por score -> ORDER
    copia ordenada por id -> FIND
```

## Decisiones
- Se valida todo antes de imprimir: no hay resultados parciales.
- `ORDER` y `FIND` trabajan sobre copias; el arreglo original conserva el orden.
- Sin `qsort`, `bsearch` ni memoria dinámica; capacidades con `#define`.
