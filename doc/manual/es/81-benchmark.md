
# Benchmarks

pgmoneta registra el tiempo de cada fase de un backup. El arnés de benchmarks ejecuta una carga un
número fijo de veces, lee esos tiempos y compara dos ramas, para que un cambio que afirma mejorar el
rendimiento pueda demostrarlo.

Los benchmarks responden *cuánto tarda*; los tests responden *pasa o falla*. Para pruebas de
corrección consulta [TEST.md](https://github.com/pgmoneta/pgmoneta/blob/main/doc/TEST.md).

## Dependencias

docker o podman, y la imagen de prueba de PostgreSQL. El arnés no construye la imagen; ejecuta
`<PATH_TO_PGMONETA>/test/check.sh` una vez primero, que la crea.

## Ejecutar benchmarks

Mide una rama, cambia, mide de nuevo y compara:

```
git switch main
<PATH_TO_PGMONETA>/benchmarks/bench.sh run -s 25

git switch mi-rama
<PATH_TO_PGMONETA>/benchmarks/bench.sh run -s 25

<PATH_TO_PGMONETA>/benchmarks/bench.sh compare main mi-rama -c backup_azure
```

```
case         : backup_azure
iterations   : 5 (median)
build        : Release
machine      : 8 cores, Linux
baseline     : main @ d2e85f2
candidate    : mi-rama @ 334c261

phase                     baseline     candidate  change
--------------------  ------------  ------------  ------------
total                      12690ms        8487ms  1.50x faster
basebackup                  3274ms        3383ms  no change

phase (emulated)          baseline     candidate  change
--------------------  ------------  ------------  ------------
remote_azure                8514ms        4889ms  1.74x faster
```

| opción | |
|---|---|
| `-c <caso>` | ejecuta un solo caso |
| `-i <n>` | iteraciones medidas, por defecto 5 |
| `-s <n>` | siembra un conjunto de datos de pgbench de escala `<n>`, por defecto 0 (ninguno) |

`bench.sh list` muestra los casos registrados; `bench.sh clean` elimina la compilación y el estado de
ejecución.

Los resultados van a `benchmarks/results/<rama>/<caso>.<timestamp>.json` y **no** se versionan:
describen una máquina en un momento concreto. Pega la salida de `compare` en el pull request.

## Elegir un conjunto de datos

Sin `-s`, el arnés respalda un clúster vacío de `initdb`, unos 40 MB en archivos muy pequeños. Eso
ejercita la ruta de código pero mide mal: las fases quedan tan pequeñas que la variación normal las
domina, y una diferencia de 5 ms sobre una fase de 9 ms se lee como un cambio relativo grande.

`-s <n>` siembra un conjunto de datos de `pgbench` de escala `<n>` más varias tablas pequeñas antes de
medir, y se ejecuta igual en ambas ramas. La escala 25 da unos 430 MB.

Siembra lo suficiente para que la fase que te interesa tarde segundos en lugar de milisegundos. Las
fases que tu cambio no toca reportarán entonces `no change` en lugar de reaccionar al ruido. Ten en
cuenta que el paralelismo de subida por archivo depende tanto del *número* de archivos como del tamaño
total, por eso la siembra también crea muchas tablas pequeñas.

## Interpretar la salida

**Compara dos ramas medidas en la misma máquina.** Los tiempos absolutos dependen de la CPU, el disco
y la carga, y el número de workers está limitado por el número de núcleos. `compare` avisa cuando el
número de núcleos difiere.

**Las diferencias por debajo del 10% se reportan como `no change`**, ya que unos pocos puntos de
variación entre ejecuciones son normales incluso con la mediana.

**El bloque `phase (emulated)` no es una medición.** Esas fases suben a un contenedor local (Azurite,
Garage, SFTP) en lugar de a un almacén de objetos real, así que muestran la dirección de un cambio
pero no son una cifra que puedas citar para producción.

## Añadir un caso

Añade un archivo `.c` en [cases](https://github.com/pgmoneta/pgmoneta/tree/main/benchmarks/cases) y usa
`BENCH_CASE()`. Los casos se registran solos, así que no hay nada más que editar.

```c
#include <bench.h>
#include <mctf_se.h>

BENCH_CASE(backup_azure, MCTF_BACKEND_AZURITE)
{
   return mctf_se_backup("primary");
}
```

El cuerpo ejecuta una iteración del trabajo a medir. El arnés levanta el backend, ejecuta un
calentamiento no medido, ejecuta el cuerpo N veces, lee los tiempos por fase de `backup.info`, toma la
mediana y escribe el resultado. Los backends son `MCTF_BACKEND_AZURITE`, `MCTF_BACKEND_GARAGE` (S3) y
`MCTF_BACKEND_SSH`.

Las fases reportadas vienen de la tabla `bench_phases` en `benchmarks/src/bench.c`; cada entrada es un
nombre y el desplazamiento de un campo en `struct backup`, así que añadir una fase es una línea.

## La compilación

Los benchmarks se compilan en **Release** dentro de `build-bench/`, separado de `build/`.

Esto importa más de lo que parece: la compilación de pruebas activa AddressSanitizer,
UndefinedBehaviorSanitizer y `-O0`, así que los tiempos medidos ahí reflejan la instrumentación y no
pgmoneta, y parecen del todo plausibles mientras lo hacen. `benchmarks/CMakeLists.txt` se niega a
configurarse si el tipo de compilación no es `Release`.

## Limitaciones

Los benchmarks se ejecutan manualmente, como en Apache DataFusion; no son una puerta de CI, ya que una
comprobación limitada por E/S y red en runners compartidos produce falsos positivos. La unidad de
trabajo es un backup completo y no una función, así que no hay medición por función, y cada caso
levanta un contenedor porque `mctf_se` solo proporciona los backends remotos.

Se recomienda que ejecutes benchmarks antes de abrir un PR que afirme una mejora de rendimiento, y que
adjuntes la salida de `compare` a la descripción del PR.
