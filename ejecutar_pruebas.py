"""Ejecuta todos los casos de pruebas/entradas con ./reto, guarda la salida
en pruebas/obtenidas y compara con pruebas/esperadas (ignora espacios)."""
import subprocess, sys
from pathlib import Path

exe = str(Path(sys.argv[1] if len(sys.argv) > 1 else "./reto").resolve())
base = Path("pruebas")
filas, ok = [], 0
for f in sorted((base / "entradas").glob("*.in")):
    esperado = (base / "esperadas" / (f.stem + ".out")).read_text()
    try:
        r = subprocess.run([exe], input=f.read_text(), text=True,
                           capture_output=True, timeout=2)
        obtenido, pasa = r.stdout, r.returncode == 0 and r.stdout.split() == esperado.split()
    except (subprocess.TimeoutExpired, OSError):
        obtenido, pasa = "", False
    (base / "obtenidas" / (f.stem + ".out")).write_text(obtenido)
    ok += pasa
    filas.append((f.stem, "OK" if pasa else "FALLO"))
lineas = ["| Caso | Resultado |", "|---|---|"] + ["| %s | %s |" % x for x in filas]
lineas.append("\nTotal: %d / %d" % (ok, len(filas)))
(base / "resultados.md").write_text("\n".join(lineas) + "\n")
print("\n".join(lineas))
sys.exit(0 if ok == len(filas) else 1)
