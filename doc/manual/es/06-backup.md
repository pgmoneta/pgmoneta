\newpage

# Backup

## Crear un backup completo

Podemos hacer un backup completo del servidor primario con el siguiente comando

```
pgmoneta-cli backup primary
```

y obtendrás una salida como esta

```
Header:
  ClientVersion: 0.21.1
  Command: 1
  Output: 0
  Timestamp: 20240928065644
Outcome:
  Status: true
  Time: 00:00:20
Request:
  Server: primary
Response:
  Backup: 20240928065644
  BackupSize: 8531968
  Compression: 2
  Encryption: 0
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 48799744
  Server: primary
  ServerVersion: 0.21.1
```

## Ver backups

Podemos listar todos los backups para un servidor con el siguiente comando

```
pgmoneta-cli list-backup primary
```

y obtendrás una salida como esta

```
Header:
  ClientVersion: 0.21.1
  Command: 2
  Output: 0
  Timestamp: 20240928065812
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Server: primary
Response:
  Backups:
    - Backup: 20240928065644
      BackupSize: 8531968
      Comments: ''
      Compression: 2
      Encryption: 0
      Incremental: false
      Keep: false
      RestoreSize: 48799744
      Server: primary
      Valid: 1
      WAL: 0
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.21.1
```

## Ordenar backups

Puedes ordenar la lista de backups por timestamp usando la opción `--sort`:

```
pgmoneta-cli list-backup primary --sort asc
```

para orden ascendente (más antiguos primero), o 

```
pgmoneta-cli list-backup primary --sort desc
```

para orden descendente (más nuevos primero).

## Crear un backup incremental

Podemos hacer un backup incremental del servidor primario con el siguiente comando

```
pgmoneta-cli backup primary 20240928065644
```

y obtendrás una salida como esta

```
Header:
  ClientVersion: 0.21.1
  Command: 1
  Output: 0
  Timestamp: 20240928065730
Outcome:
  Status: true
  Time: 00:00:20
Request:
  Server: primary
Response:
  Backup: 20240928065750
  BackupSize: 124312
  Compression: 2
  Encryption: 0
  Incremental: true
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 48799744
  Server: primary
  ServerVersion: 0.21.1
```

Los backups incrementales se soportan cuando usas [PostgreSQL 17+](https://www.postgresql.org). Ten en cuenta que actualmente no se permite ramificación para backups incrementales -- un backup puede tener como máximo 1 backup incremental hijo.

Nota: También se proporciona soporte para backups incrementales en PostgreSQL versiones 14–16, a nivel de vista previa.

## Backup incremental para PostgreSQL 14-16

### Funcionamiento

Esta sección te proporcionará una breve idea de cómo `pgmoneta` realiza backups incrementales

* Obtiene los bloques modificados dentro de un rango de WAL LSN (típicamente desde el checkpoint del backup anterior al LSN inicial del backup actual) y genera un resumen del mismo
* Obtiene todos los archivos del servidor en el directorio de datos del servidor
* Itera sobre los archivos del servidor -
    * Si el archivo no está en 'base'/'global', realiza backup completo del archivo (ya que los archivos fuera de 'base'/'global' no se registran en WAL apropiadamente)
    * En caso contrario, si se encontró que el archivo fue modificado usando el resumen, realiza backup incremental de este archivo.
    * En caso contrario, el archivo no ha cambiado, ahora si el tamaño del archivo es 0 o su límite de bloque intersecta el segmento (significando que el archivo está truncado completamente/parcialmente)
        * Realiza backup completo del archivo
        * En caso contrario, realiza backup incremental vacío solo con el encabezado
* Copia todos los segmentos de WAL después e incluyendo el segmento de WAL en el que está presente el LSN inicial
* Genera archivo manifest sobre el directorio de datos del backup incremental

### Dependencias

Para la versión de PostgreSQL 14-16, confiamos en soluciones de backup incremental a nivel de bloque nativas de `pgmoneta`. Para facilitar esta solución, `pgmoneta` depende altamente de la extensión [pgmoneta_ext](https://github.com/pgmoneta/pgmoneta_ext) y de las funciones de administración del sistema de PostgreSQL. Los siguientes son la lista de funciones de administrador de las que `pgmoneta` depende:

| Función                    | Función de administración del sistema | Privilegio mínimo | Parámetros | Descripción                                            |
|-----------------------------|---|--------|------------|--------------------------------------------------------|
| `pgmoneta_server_backup_start`    |   pg_start_backup/pg_backup_start |    EXECUTE    | label | Devuelve una fila con el LSN de inicio del backup |
| `pgmoneta_server_backup_stop`    |   pg_stop_backup/pg_backup_stop |    EXECUTE    | Ninguno | Devuelve una fila con dos columnas - LSN de parada del backup y contenido del archivo de etiqueta de backup  |
| `pgmoneta_server_read_binary`    |   pg_read_binary_file |    pg_read_server_files & EXECUTE    | (offset, length, path/to/file) | Devuelve el contenido del archivo proporcionado del servidor de longitud particular en un offset particular |
| `pgmoneta_server_file_stat`    |   pg_stat_file |    pg_read_server_files & EXECUTE    | path/to/file | Devuelve los metadatos del archivo proporcionado del servidor como tamaño de archivo, tiempo de modificación, etc |

Para información completa sobre la API del servidor, consulta [esto](https://github.com/pgmoneta/pgmoneta/blob/main/doc/manual/en/80-server-api.md)

Las funciones de extensión en las que depende la solución nativa son:

- `pgmoneta_ext_get_file('<path/to/file>')`
- `pgmoneta_ext_get_files('<path/to/dir>')`

Puedes leer más sobre estas funciones y sus privilegios requeridos [aquí](https://github.com/pgmoneta/pgmoneta_ext/blob/main/doc/manual/en/04-functions.md)

### Setup

Supongamos que queremos configurar PostgreSQL 14. Asegúrate de que PostgreSQL 14 esté instalado en tu sistema. Dado que nuestra solución depende de `pgmoneta_ext`, compila e instala la extensión para la versión 14 usando los siguientes comandos:

```
git clone https://github.com/pgmoneta/pgmoneta_ext
cd pgmoneta_ext
mkdir build
cd build
cmake ..
make
sudo make install
```

Si hay varias versiones de PostgreSQL instaladas, modifica la variable `PATH` para que la ruta de los binarios de la versión 14 aparezca primero.

**Inicializa un clúster de PostgreSQL**

```
initdb -D <path>
```

**Modifica el archivo `postgresql.conf` para habilitar los siguientes parámetros**

```
password_encryption = scram-sha-256
shared_preload_libraries = 'pgmoneta_ext'
```

**Agrega la siguiente entrada al archivo `pg_hba.conf`**

```
host    replication     repl           127.0.0.1/32            scram-sha-256
```

**Inícia el clúster usando el comando**

```
pg_ctl -D <path> -l logfile start
```

**Por último, realiza los siguientes comandos para crear la extensión y otorgar los permisos necesarios al usuario de conexión**

```
cat > init-permissions.sh << 'OUTER_EOF'
#!/bin/bash

# Verifica parámetros de entrada
if [ $# -lt 2 ]; then
    echo "Usage: $0 <connection_user_name> <postgres_version>"
    exit 1
fi

CONN_USER_NAME="$1"
PG_VERSION="$2"

SCRIPT_BASE_NAME=$(basename -s .sh "$0")
OUTPUT_SQL_FILE="${SCRIPT_BASE_NAME}.sql"

# Verifica si existe la extensión pgmoneta_ext
EXT_EXISTS=$(psql -d postgres -Atc \
    "SELECT 1 FROM pg_extension WHERE extname = 'pgmoneta_ext';")

if [ $? -ne 0 ]; then
    echo "Failed checking for pgmoneta_ext extension"
    exit 1
fi

# -------------------------------------------------------------------
# Comienza a escribir SQL en el archivo de salida
# -------------------------------------------------------------------

cat <<EOF > "$OUTPUT_SQL_FILE"
SET password_encryption = 'scram-sha-256';

DO \$\$
BEGIN
    -- Crea nuevo usuario de replicación si no existe
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = '$CONN_USER_NAME') THEN
        EXECUTE 'CREATE ROLE $CONN_USER_NAME WITH LOGIN REPLICATION PASSWORD '"$CONN_USER_NAME"'';
    END IF;

    -- Crea ranura de replicación si falta
    IF NOT EXISTS (
        SELECT 1 FROM pg_replication_slots WHERE slot_name = '$CONN_USER_NAME'
    ) THEN
        PERFORM pg_create_physical_replication_slot('$CONN_USER_NAME', true, false);
    END IF;
END
\$\$;

-- Crea extensión si falta
EOF

if [ "$EXT_EXISTS" != "1" ]; then
  echo "DROP EXTENSION IF EXISTS pgmoneta_ext;" >> "$OUTPUT_SQL_FILE"
  echo "CREATE EXTENSION pgmoneta_ext;" >> "$OUTPUT_SQL_FILE"
fi

cat <<EOF >> "$OUTPUT_SQL_FILE"

GRANT EXECUTE ON FUNCTION pgmoneta_ext_get_file(text) TO $CONN_USER_NAME;
GRANT EXECUTE ON FUNCTION pgmoneta_ext_get_files(text) TO $CONN_USER_NAME;

-- Privilegio para leer archivos del servidor
GRANT pg_read_server_files TO $CONN_USER_NAME;

GRANT EXECUTE ON FUNCTION pg_read_binary_file(text, bigint, bigint, boolean) TO $CONN_USER_NAME;
GRANT EXECUTE ON FUNCTION pg_stat_file(text, boolean) TO $CONN_USER_NAME;

-- Privilegios de función de backup dependiendo de la versión
EOF

# Asignaciones de backup basadas en versión
if [ "$PG_VERSION" -ge 15 ]; then
  cat <<EOF >> "$OUTPUT_SQL_FILE"
  GRANT EXECUTE ON FUNCTION pg_backup_start(text, boolean) TO $CONN_USER_NAME;
  GRANT EXECUTE ON FUNCTION pg_backup_stop(boolean) TO $CONN_USER_NAME;
  EOF
else
  cat <<EOF >> "$OUTPUT_SQL_FILE"
  GRANT EXECUTE ON FUNCTION pg_start_backup(text, boolean, boolean) TO $CONN_USER_NAME;
  GRANT EXECUTE ON FUNCTION pg_stop_backup(boolean, boolean) TO $CONN_USER_NAME;
  EOF
fi

OUTER_EOF

chmod 755 ./init-permissions.sh
./init-permissions.sh repl 14
psql -f init-permissions.sql postgres
```

Esto generará un archivo sql `init-permissions.sql` utiliza esto para inicializar el cluster:

```
psql -f init-permissions.sql postgres
```

Una vez completada esta configuración, puedes proceder a crear backups incrementales sin problemas.

## Información de backup

Puedes listar la información sobre un backup

```
pgmoneta-cli -c pgmoneta.conf info primary newest
```

y obtendrás el siguiente output:

```
Header:
  ClientVersion: 0.21.1
  Command: info
  Output: text
  Timestamp: 20241025163541
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Backup: newest
  Server: primary
Response:
  Backup: 20241019163516
  BackupSize: 6.54MB
  CheckpointHiLSN: 0
  CheckpointLoLSN: 4F0000B8
  Comments: ''
  Compression: zstd
  Elapsed: 4
  Encryption: none
  EndHiLSN: 0
  EndLoLSN: 4F000158
  EndTimeline: 1
  Incremental: false
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  NumberOfTablespaces: 0
  RestoreSize: 45.82MB
  Server: primary
  ServerVersion: 0.21.1
  StartHiLSN: 0
  StartLoLSN: 4F000060
  StartTimeline: 1
  Tablespaces: {}
  Valid: yes
  WAL: 00000001000000000000004F
```

## Verificar un backup

Puedes usar la interfaz de línea de comandos para verificar un backup usando:

```
pgmoneta-cli verify primary oldest /tmp
```

que verificará el backup más antiguo del host `[primary]`.

[**pgmoneta**][pgmoneta] crea un archivo de suma de verificación SHA512 (`backup.sha512`) para cada backup en el directorio raíz del backup, que puede usarse para verificar la integridad de los archivos.

Usando `sha512sum`:
```
cd <path-to-specific-backup-directory>

sha512sum --check backup.sha512
```

El parámetro `verification` puede usarse para controlar la frecuencia con la que pgmoneta verifica la integridad de los archivos de backup. Puedes configurar esto en `pgmoneta.conf`:

```
[pgmoneta]
.
.
.
verification = 3600
```
Por ejemplo, establecer `verification = 3600` o `verification = 1H` realizará verificaciones de integridad cada hora.

## Encriptación

Por defecto, la encriptación está deshabilitada. Para habilitar esta característica, modifica `pgmoneta.conf`:

```
encryption = aes-256-cbc
```

Se admiten muchos modos de encriptación, consulta la documentación de la propiedad `encryption` para más detalles.

### Comandos de Encriptación y Desencriptación

[**pgmoneta**][pgmoneta] utiliza la misma clave creada por `pgmoneta-admin master-key` para encriptar y desencriptar archivos.

Encripta un archivo con `pgmoneta-cli encrypt`, el archivo será encriptado en su lugar y eliminará el archivo desencriptado en caso de éxito.

```sh
pgmoneta-cli -c pgmoneta.conf encrypt '<path-to-your-file>/file.tar.zstd'
```

Desencripta un archivo con `pgmoneta-cli decrypt`, el archivo será desencriptado en su lugar y eliminará el archivo encriptado en caso de éxito.

```sh
pgmoneta-cli -c pgmoneta.conf decrypt '<path-to-your-file>/file.tar.zstd.aes'
```

`pgmoneta-cli encrypt` y `pgmoneta-cli decrypt` están construidos para trabajar con archivos creados por `pgmoneta-cli archive`. Sin embargo, pueden usarse en otros archivos.

## Agregar anotaciones

**Agregar un comentario**

Puedes agregar un comentario usando:

```
pgmoneta-cli -c pgmoneta.conf annotate primary newest add mykey mycomment
```

**Actualizar un comentario**

Puedes actualizar un comentario usando:

```
pgmoneta-cli -c pgmoneta.conf annotate primary newest update mykey mynewcomment
```

**Eliminar un comentario**

Puedes eliminar un comentario usando:

```
pgmoneta-cli -c pgmoneta.conf annotate primary newest remove mykey
```

**Ver comentarios**

Puedes ver los comentarios usando:

```
pgmoneta-cli -c pgmoneta.conf info primary newest
```

## Archivo

Para crear un archivo de un backup utiliza:

```
pgmoneta-cli -c pgmoneta.conf archive primary newest current /tmp/
```

que tomará el backup más reciente y todos los segmentos Write-Ahead Log (WAL) y creará
un archivo denominado `/tmp/primary-<timestamp>.tar.zstd`. Este archivo contendrá
una copia actualizada.

## Crontab

Vamos a crear un `crontab` de manera que se haga un backup cada día,

Primero, haz un backup completo si estás usando PostgreSQL 17+,

```
pgmoneta-cli backup primary
```

luego puedes usar backup incremental para tus trabajos diarios,

```
crontab -e
```

e inserta:

```
0 6 * * * pgmoneta-cli backup primary latest
```

para hacer un backup incremental cada día a las 6 am.

De lo contrario, utiliza el backup completo en el trabajo cron.
