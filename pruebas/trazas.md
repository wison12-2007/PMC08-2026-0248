# Traza manual y fallo corregido

## Traza manual (público 1, unidad 30: tipo 0, p = 40, k = 2)
| x  | ¿x>=40? | racha | activo | score |
|----|---------|-------|--------|-------|
| 10 | no      | 0     | 0      | 0     |
| 50 | sí      | 1     | 0      | 0     |
| 70 | sí      | 2     | 1      | 1     |
| 30 | no      | 0     | 0      | 1     |
| 90 | sí      | 1     | 0      | 1     |

Esperado a mano: `30 1 0`. Obtenido por el programa: `30 1 0`. Coincide.

## Fallo encontrado y corregido: `val03_p0_id_maximo`
- Esperado: `9999 1 1 / TOTAL 1 / ORDER 9999 / FIND 9999 1`.
- Obtenido la primera vez: `ERROR`.
- Causa: el archivo de entrada tenía solo 7 muestras con `m = 8` (error del
  caso de prueba, no del programa). El programa detectó correctamente la
  entrada incompleta.
- Corrección: se añadió la octava muestra y el caso pasó a OK.
