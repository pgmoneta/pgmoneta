
## Tests

**Dependencias**

Para instalar todas las dependencias requeridas, simplemente ejecuta `<PATH_TO_PGMONETA>/test/check.sh setup`. Necesitas instalar docker o podman
por separado. El script actualmente solo funciona en sistemas Linux (recomendamos Fedora 39+).

**Ejecutar Tests**

Para ejecutar los tests, simplemente ejecuta `<PATH_TO_PGMONETA>/test/check.sh`. El script construirá una imagen de PostgreSQL 17 la primera vez que lo ejecutes,
e iniciará un contenedor docker/podman usando la imagen (así que asegúrate de tener al menos uno de ellos instalado y el motor de contenedor correspondiente iniciado). 
El servidor postgres en contenedor tendrá un usuario `repl` con el atributo de replicación, un usuario normal `myuser` y una base de datos `mydb`.

El script luego inicia pgmoneta y ejecuta pruebas en tu entorno local. Las pruebas se ejecutan localmente para que puedas aprovechar stdout para depuración y
el entorno de prueba no tendrá problemas de entorno de contenedor extraño, y para que podamos reutilizar las dependencias instaladas y el caché de cmake para acelerar el desarrollo
y depuración.

Toda la configuración, registros, informes de cobertura y datos estarán en `/tmp/pgmoneta-test/`, y se ejecutará una limpieza ya sea que el script salga normalmente o no. pgmoneta será forzado a apagar si no termina normalmente. Así que no te preocupes por que tu configuración local sea alterada. El contenedor será detenido y eliminado cuando el script salga o sea terminado.

**Solo configurar (sin pruebas):** Ejecuta `<PATH_TO_PGMONETA>/test/check.sh build` para preparar el entorno de prueba (imagen, compilación de pgmoneta, contenedor, configuración) sin ejecutar pruebas. Esto siempre hace una compilación completa.

**Prueba única o módulo:** Ejecuta `<PATH_TO_PGMONETA>/test/check.sh -t <test_name>` o `<PATH_TO_PGMONETA>/test/check.sh -m <module_name>` (forma larga: `--test`, `--module`). El script configura el entorno automáticamente cuando es necesario, así que no necesitas ejecutar el conjunto completo primero. Para iteración rápida, ejecuta `<PATH_TO_PGMONETA>/test/check.sh build` una vez, luego `<PATH_TO_PGMONETA>/test/check.sh -t <test_name>` (o `-m <module_name>`) repetidamente. Las variables de entorno se reinician cuando la ejecución de prueba finaliza o es abortada.

Se recomienda que **SIEMPRE** ejecutes tests antes de presentar un PR.

**Descripción general del marco MCTF**

MCTF (Minimal C Test Framework) es el test framework personalizado de pgmoneta diseñado para simplicidad y facilidad de uso.

**Lo que MCTF puede hacer:**
- **Registro automático de pruebas** - Los tests se registran automáticamente a través de atributos de constructor
- **Organización de módulos** - Los nombres de módulos se extraen automáticamente de los nombres de archivos (p. ej., `test_utils.c` → módulo `utils`)
- **Aserciones flexibles** - Macros de aserción con mensajes de error opcionales de estilo printf
- **Filtrado de pruebas** - Ejecuta pruebas por patrón de nombre (`-t`) o por módulo (`-m`)
- **Omisión de pruebas** - Omite tests condicionalmente usando `MCTF_SKIP()` cuando los requisitos previos no se cumplen
- **Corte de registro de pgmoneta por prueba y validación** - Captura la ventana de registro de cada prueba en `/tmp/pgmoneta-test/log/<module>__<test_name>.pgmoneta.log`; las pruebas positivas fallan en líneas `ERROR` inesperadas, mientras que `MCTF_TEST_NEGATIVE` se usa para escenarios de error esperado
- **Tiempo de ejecución máximo (puerta de rendimiento)** - `MCTF_TEST_MAX(name, seconds)` falla el test si se ejecuta más tiempo del límite; `MCTF_TEST_MAX_NEGATIVE(name, seconds)` agrega un límite de tiempo a una prueba negativa. Usa para detectar regresiones de rendimiento (p. ej., después de cambios de OpenSSL).
- **Hooks del ciclo de vida** – Configuración/desmontaje automático por prueba y por módulo a través de `MCTF_TEST_SETUP`, `MCTF_TEST_TEARDOWN`, `MCTF_MODULE_SETUP`, `MCTF_MODULE_TEARDOWN`
- **Instantánea de configuración/restauración** – `pgmoneta_test_config_save()` / `pgmoneta_test_config_restore()` para aislar cambios de configuración de memoria compartida entre pruebas
- **Patrón de limpieza** - Limpieza estructurada usando etiquetas goto para gestión de recursos
- **Seguimiento de errores** - Seguimiento automático de errores con números de línea y mensajes de error personalizados
- **Múltiples tipos de aserción** - Varias macros de aserción (`MCTF_ASSERT`, `MCTF_ASSERT_PTR_NONNULL`, `MCTF_ASSERT_INT_EQ`, `MCTF_ASSERT_STR_EQ`, etc.)

**Lo que MCTF NO puede hacer (limitaciones):**
- **Sin pruebas parametrizadas** - Las pruebas no pueden estar parametrizadas (cada variación necesita una función de prueba separada)
- **Sin ejecución paralela o asincrónica** - Las pruebas se ejecutan secuencial y síncronamente
- **Sin interrupción forzada en tiempo de espera** - Max-time solo falla después de que la prueba regresa; no interrumpe una prueba atascada (confía en señales del SO o tiempos de espera externos para eso)
- **Sin organización de prueba más allá de módulos** - Sin suites de prueba, grupos, etiquetas o metadatos más allá de los nombres de módulos extraídos de nombres de archivos

**Agregar Testcases**

Para agregar un caso de prueba adicional, ve al directorio [testcases](https://github.com/pgmoneta/pgmoneta/tree/main/test/testcases) dentro del proyecto `pgmoneta`.

Crea un archivo `.c` que contenga la prueba y usa la macro `MCTF_TEST()` para definir tu prueba. Las pruebas se registran automáticamente y los nombres de módulos se extraen de los nombres de archivos.

Usa `MCTF_TEST_NEGATIVE()` para pruebas que intencionalmente ejercitan rutas de error y se espera que emitan líneas `ERROR` en `pgmoneta.log`.

**Validación de registro de pgmoneta por prueba**

MCTF captura una porción por prueba de `pgmoneta.log` y la escribe en:

`/tmp/pgmoneta-test/log/<module>__<test_name>.pgmoneta.log`

Comportamiento:

- `MCTF_TEST`: falla si la prueba en sí pasa pero la porción de registro contiene líneas `ERROR` inesperadas
- `MCTF_TEST_NEGATIVE`: omite la puerta de fallo de error de registro para esa prueba (aún debe satisfacer aserciones de prueba)
- Las líneas `WARN` se incluyen en resúmenes pero no fallan una prueba que pasa

**Tiempo de ejecución máximo de test (rendimiento gate)**

Usa `MCTF_TEST_MAX(test_name, max_seconds)` para forzar un tiempo de ejecución máximo permitido. Si la prueba se completa exitosamente pero toma más de `max_seconds`, se reporta como **FAILED** con un mensaje de que se excedió el tiempo máximo.

- **`MCTF_TEST_MAX(name, seconds)`** – Prueba positiva con un límite de tiempo.
- **`MCTF_TEST_MAX_NEGATIVE(name, seconds)`** – Prueba negativa (errores de registro permitidos) con un límite de tiempo.

```c
MCTF_TEST_MAX(test_backup_full, 60)
{
   // La prueba debe pasar y terminar dentro de 60 segundos
   ...
}
```

- El límite es en **segundos**; solo se mide el tiempo de ejecución del cuerpo de la prueba (después de configuración por prueba, antes de desmontaje por prueba).
- Si la prueba falla por aserción u es omitida, la verificación de max-time no anula eso.
- En tiempo de espera, el mensaje de fallo se ve como: `Test exceeded maximum time: 65.234s (limit 60s)`.

**Hooks del ciclo de vida**

MCTF proporciona cuatro macros de gancho del ciclo de vida que se auto-registran (mismo patrón `__attribute__((constructor))` que `MCTF_TEST`):

| Macro | Cuándo se ejecuta | Caso de uso |
|---|---|---|
| `MCTF_TEST_SETUP(module)` | Antes de **cada** prueba | Asignar recursos por prueba |
| `MCTF_TEST_TEARDOWN(module)` | Después de **cada** prueba (siempre) | Liberar recursos por prueba |
| `MCTF_MODULE_SETUP(module)` | Una vez antes de la primera prueba | Iniciar un daemon / abrir una conexión |
| `MCTF_MODULE_TEARDOWN(module)` | Una vez después de la última prueba | Detener un daemon / cerrar una conexión |

`module` debe coincidir con el nombre derivado del nombre de archivo (p. ej. `test_cache.c` -> `cache`).

Usa `MCTF_TEST_SETUP/TEARDOWN` cuando cada prueba necesita un entorno limpio y aislado (p. ej. `shmem` privado). Usa `MCTF_MODULE_SETUP/TEARDOWN` cuando la configuración es cara y segura de compartir entre todas las pruebas en el módulo. Se pueden combinar.

**Snapshot de configuración/restauración**

Debido a que todas las pruebas comparten el mismo proceso y `shmem`, las mutaciones de configuración en una prueba se filtran en la siguiente. Usa `pgmoneta_test_config_save()` y `pgmoneta_test_config_restore()` (declaradas en `tscommon.h`) dentro de `MCTF_TEST_SETUP/TEARDOWN` para capturar y deshacer `struct main_configuration` alrededor de cada prueba:

```c
MCTF_TEST_SETUP(mymodule)
{
   pgmoneta_test_config_save();
   pgmoneta_memory_init();
}

MCTF_TEST_TEARDOWN(mymodule)
{
   pgmoneta_memory_destroy();
   pgmoneta_test_config_restore();
}
```

**Estructura de prueba de ejemplo:**
```c
#include <mctf.h>
#include <tscommon.h>

MCTF_TEST(test_my_feature)
{
   pgmoneta_test_setup();

   // Tu código de prueba aquí
   int result = some_function();
   MCTF_ASSERT(result == 0, cleanup, "function should return 0");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
```

**Uso de MCTF_ASSERT:**

La macro `MCTF_ASSERT` soporta mensajes de error opcionales con formato de estilo printf:

- **Sin mensaje:** `MCTF_ASSERT(condition, cleanup);` - No se muestra mensaje de error
- **Con mensaje simple:** `MCTF_ASSERT(condition, cleanup, "error message");`
- **Con mensaje formateado:** `MCTF_ASSERT(condition, cleanup, "got %d, expected 0", value);`
  - Los argumentos de formato (como `value`) son opcionales y solo se necesitan cuando el mensaje contiene especificadores de formato (`%d`, `%s`, etc.)
  - Múltiples argumentos de formato: `MCTF_ASSERT(a == b, cleanup, "expected %d but got %d", expected, actual);`

**Recurso de prueba**

Si tienes recursos como entrada de caso de prueba, colócalos bajo `test/resource/<your-test-case-name>/`. El `check.sh` los copiará
a `TEST_BASE_DIR/resource/<your-test-case-name>/`, es decir `/tmp/pgmoneta-test/base/resource/<your-test-case-name>/`. Y en tu
código de prueba puedes abrir el archivo desde allí directamente.

**Directorio de prueba**

Después de ejecutar las pruebas, encontrarás:

* **registro de pgmoneta:** `/tmp/pgmoneta-test/log/`
  * **porciones de registro de pgmoneta por prueba:** `/tmp/pgmoneta-test/log/<module>__<test_name>.pgmoneta.log`
* **registro de postgres:** `/tmp/pgmoneta-test/pg_log/`, el nivel de registro se establece en debug5 y tiene el nombre de aplicación (**pgmoneta**) mostrado en el registro.
* **informes de cobertura de código:** `/tmp/pgmoneta-test/coverage/`

Si necesitas crear un directorio en tiempo de ejecución, créalo bajo `/tmp/pgmoneta-test/base/`, que también contiene `backup/`, `restore/`, `conf` y `workspace/`.
El directorio base será limpiado después de que las pruebas se realicen. En `tscommon.h` encontrarás `TEST_BASE_DIR` y otras variables globales que contienen los directorios correspondientes, 
obtenidas de variables de entorno.

**Limpieza**

`<PATH_TO_PGMONETA>/test/check.sh clean` eliminará el directorio de prueba y la imagen construida. Si estás usando docker, hay posibilidades de que consuma tu 
espacio en disco secretamente, en ese caso considera limpiar usando `docker system prune --volume`. Úsalo con cuidado ya que
anula todos los volúmenes de docker.

**Puerto**

Por defecto, el pod expone el puerto 6432 para que pgmoneta se conecte. Esto se puede cambiar mediante `export PGMONETA_TEST_PORT=<your-port>` antes de ejecutar `check.sh`. O también
puedes ejecutar `PGMONETA_TEST_PORT=<your-port> <PATH_TO_PGMONETA>/test/check.sh`.

**Configuración**

| Nombre             | Predeterminado | Valor           | Descripción                                         |
|--------------------|---------|-----------------|-----------------------------------------------------|
| PGMONETA_TEST_PORT | 6432    | número de puerto     | El puerto que pgmoneta usa para conectarse al pod de bd |


### Agregar testcases relacionados con wal

Mientras avanzamos hacia el objetivo de crear testcases completo para compilar y probar los mecanismos de generación y reproducción de wal de pgmoneta, necesitamos agregar algunos casos de prueba que generarán archivos wal y luego los reproducirán. Actualmente necesitamos agregar casos de prueba para los siguientes tipos de registros wal:

<details>
<summary>Haz clic para expandir</summary>

- **XLOG**
  - XLOG_CHECKPOINT_SHUTDOWN
  - XLOG_CHECKPOINT_ONLINE
  - XLOG_NOOP
  - XLOG_NEXTOID
  - XLOG_SWITCH
  - XLOG_BACKUP_END
  - XLOG_PARAMETER_CHANGE
  - XLOG_RESTORE_POINT
  - XLOG_FPI
  - XLOG_FPI_FOR_HINT
  - XLOG_FPW_CHANGE
  - XLOG_END_OF_RECOVERY
  - XLOG_OVERWRITE_CONTRECORD

- **XACT**
  - XLOG_XACT_COMMIT
  - XLOG_XACT_ABORT
  - XLOG_XACT_PREPARE
  - XLOG_XACT_COMMIT_PREPARED
  - XLOG_XACT_ABORT_PREPARED
  - XLOG_XACT_ASSIGNMENT

- **SMGR**
  - XLOG_SMGR_CREATE
  - XLOG_SMGR_TRUNCATE

- **DBASE**
  - XLOG_DBASE_CREATE
  - XLOG_DBASE_DROP

- **TBLSPC**
  - XLOG_TBLSPC_CREATE
  - XLOG_TBLSPC_DROP

- **RELMAP**
  - XLOG_RELMAP_UPDATE

- **STANDBY**
  - XLOG_RUNNING_XACTS
  - XLOG_STANDBY_LOCK

- **HEAP2**
  - XLOG_HEAP2_FREEZE_PAGE
  - XLOG_HEAP2_VACUUM
  - XLOG_HEAP2_VISIBLE
  - XLOG_HEAP2_MULTI_INSERT
  - XLOG_HEAP2_PRUNE

- **HEAP**
  - XLOG_HEAP_INSERT
  - XLOG_HEAP_DELETE
  - XLOG_HEAP_UPDATE
  - XLOG_HEAP_INPLACE
  - XLOG_HEAP_LOCK
  - XLOG_HEAP_CONFIRM

- **BTREE**
  - XLOG_BTREE_INSERT_LEAF
  - XLOG_BTREE_INSERT_UPPER
  - XLOG_BTREE_INSERT_META
  - XLOG_BTREE_SPLIT_L
  - XLOG_BTREE_SPLIT_R
  - XLOG_BTREE_VACUUM
  - XLOG_BTREE_DELETE
  - XLOG_BTREE_UNLINK_PAGE
  - XLOG_BTREE_NEWROOT
  - XLOG_BTREE_REUSE_PAGE

- **HASH**
  - XLOG_HASH_INIT_META_PAGE
  - XLOG_HASH_INIT_BITMAP_PAGE
  - XLOG_HASH_INSERT
  - XLOG_HASH_ADD_OVFL_PAGE
  - XLOG_HASH_DELETE
  - XLOG_HASH_SPLIT_ALLOCATE_PAGE
  - XLOG_HASH_SPLIT_PAGE
  - XLOG_HASH_SPLIT_COMPLETE
  - XLOG_HASH_MOVE_PAGE_CONTENTS
  - XLOG_HASH_SQUEEZE_PAGE

- **GIN**
  - XLOG_GIN_CREATE_PTREE
  - XLOG_GIN_INSERT
  - XLOG_GIN_SPLIT
  - XLOG_GIN_VACUUM_PAGE
  - XLOG_GIN_DELETE_PAGE
  - XLOG_GIN_UPDATE_META_PAGE
  - XLOG_GIN_INSERT_LISTPAGE
  - XLOG_GIN_DELETE_LISTPAGE

- **GIST**
  - XLOG_GIST_PAGE_UPDATE
  - XLOG_GIST_PAGE_SPLIT
  - XLOG_GIST_DELETE

- **SEQ**
  - XLOG_SEQ_LOG

- **SPGIST**
  - XLOG_SPGIST_ADD_LEAF
  - XLOG_SPGIST_MOVE_LEAFS
  - XLOG_SPGIST_ADD_NODE
  - XLOG_SPGIST_SPLIT_TUPLE
  - XLOG_SPGIST_VACUUM_LEAF
  - XLOG_SPGIST_VACUUM_ROOT
  - XLOG_SPGIST_VACUUM_REDIRECT

- **BRIN**
  - XLOG_BRIN_CREATE_INDEX
  - XLOG_BRIN_UPDATE
  - XLOG_BRIN_SAMEPAGE_UPDATE
  - XLOG_BRIN_REVMAP_EXTEND
  - XLOG_BRIN_DESUMMARIZE

- **REPLORIGIN**
  - XLOG_REPLORIGIN_SET
  - XLOG_REPLORIGIN_DROP

- **LOGICALMSG**
  - XLOG_LOGICAL_MESSAGE

</details>

Para cada tipo de registro, necesitamos agregar un caso de prueba que generará el registro wal y luego lo reproducirá. Para todos los tipos, los procedimientos de lectura y escritura serán los mismos, pero la generación del registro wal será diferente. Para agregar casos de prueba para un tipo de registro específico, necesitarás seguir los procedimientos mencionados en la sección anterior. Para escribir el caso de prueba en sí, haz lo siguiente:
1. Implementa la función `pgmoneta_test_generate_<type>_v<version>` en `test/libpgmonetatest/tswalutils/tswalutils_<version>.c` (agrega el prototipo de función en `test/include/tswalutils.h` también). Esta función es responsable de generar el registro wal del tipo que estás agregando que simula un registro wal real de PostgreSQL.
2. Agrega esto en el cuerpo del caso de prueba 
```c
MCTF_TEST(test_check_point_shutdown_v17)
{
   struct walfile* wf = NULL;
   struct walfile* read_wf = NULL;
   char* path = NULL;

   pgmoneta_test_setup();

   wf = pgmoneta_test_generate_check_point_shutdown_v17();
   MCTF_ASSERT_PTR_NONNULL(wf, cleanup, "failed to generate walfile");

   MCTF_ASSERT(!pgmoneta_write_walfile(wf, 0, path), cleanup, "failed to write walfile to disk");
   MCTF_ASSERT(!pgmoneta_read_walfile(0, path, &read_wf), cleanup, "failed to read walfile from disk");
   MCTF_ASSERT_PTR_NONNULL(read_wf, cleanup, "read walfile is null");
   MCTF_ASSERT(!compare_walfile(wf, read_wf), cleanup, "walfile comparison failed");

cleanup:
   destroy_walfile(wf);
   destroy_walfile(read_wf);
   free(path);
   MCTF_FINISH();
}
```
y reemplaza `pgmoneta_test_generate_check_point_shutdown_v17` con la función que implementaste en el paso 1.

Si el tipo de registro que estás agregando tiene diferencias entre versiones de PostgreSQL (13-17), necesitarás implementar una función de generación por versión (`generate_rec_x` -> `generate_rec_x_v16`, `generate_rec_x_v17`, etc.).

Para simplicidad, por favor crea un conjunto de pruebas por versión de postgres donde la implementación reside en `test/libpgmonetatest/tswalutils/tswalutils_<version>.c` y los casos de prueba en `test/testcases/test_wal_utils.c` y agrega caso de prueba por tipo de registro dentro de esta versión. Puedes ver [este caso de prueba](../../../../../test/testcases/test_wal_utils.c) como referencia.