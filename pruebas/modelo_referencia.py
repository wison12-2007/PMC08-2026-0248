"""Modelo de referencia en Python (independiente del C) para generar el
caso de capacidad maxima n=20, m=8 con semilla fija."""
import random

def unidad(xs, tipo, p):
    k, racha, activo, score = tipo + 2, 0, 0, 0
    for x in xs:
        if x >= p:
            racha += 1
            if not activo and racha >= k:
                activo, score = 1, score + 1
        else:
            racha = activo = 0
    return score, activo

random.seed(2026)
n, m, q = 20, 8, 777
ids = random.sample(range(1, 10000), n)
filas = [(i, "U%d" % i, random.randint(0, 1), random.randint(20, 60),
          [random.randint(0, 100) for _ in range(m)]) for i in ids]
ent = ["%d %d %d" % (n, m, q)]
res, sc = [], {}
for i, nom, t, p, xs in filas:
    ent.append("%d %s %d %d %s" % (i, nom, t, p, " ".join(map(str, xs))))
    s, e = unidad(xs, t, p)
    sc[i] = s
    res.append("%d %d %d" % (i, s, e))
res.append("TOTAL %d" % sum(sc.values()))
res.append("ORDER " + " ".join(str(i) for i in sorted(sc, key=lambda i: (-sc[i], i))))
res.append("FIND %d %d" % (q, sc.get(q, -1)))
open("pruebas/entradas/val05_capacidad_20x8.in", "w").write("\n".join(ent) + "\n")
open("pruebas/esperadas/val05_capacidad_20x8.out", "w").write("\n".join(res) + "\n")
