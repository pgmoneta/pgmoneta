\newpage

# Restaurar

## Restaurar un backup

Podemos restaurar un backup desde el primario con el siguiente comando:

```
pgmoneta-cli restore primary newest current /tmp
```

donde

* `current` significa copiar Write-Ahead Log (WAL), y restaurar al primer checkpoint estable
* `name=X` significa copiar Write-Ahead Log (WAL), y restaurar a la etiqueta especificada
* `xid=X` significa copiar Write-Ahead Log (WAL), y restaurar al XID especificado
* `time=X` significa copiar Write-Ahead Log (WAL), y restaurar a la marca de tiempo especificada
* `lsn=X` significa copiar Write-Ahead Log (WAL), y restaurar al Log Sequence Number (LSN) especificado
* `inclusive=X` significa que la restauración es inclusiva de la información especificada
* `timeline=X` significa que la restauración se realiza a la línea de tiempo de información especificada
* `action=X` significa qué acción debe ejecutarse después de la restauración (pause, shutdown)
* `primary` significa que el cluster está configurado como primario
* `replica` significa que el cluster está configurado como réplica

[Más información](https://www.postgresql.org/docs/current/runtime-config-wal.html#RUNTIME-CONFIG-WAL-RECOVERY-TARGET)

## Selección automática de backup

Cuando especificas un objetivo de recuperación (`lsn=X`, `time=X`, o `timeline=X`), pgmoneta puede seleccionar automáticamente
el backup apropiado que contiene el objetivo. En lugar de especificar una marca de tiempo de backup,
usa `newest` y pgmoneta encontrará el backup más reciente que puede usarse para recuperación al objetivo especificado.

### LSN objetivo

Restaura a un LSN específico, con selección automática de backup:

```
pgmoneta-cli restore primary newest lsn=0/16B0938 /tmp
```

pgmoneta seleccionará el backup válido más reciente cuyo LSN de inicio es menor o igual a `0/16B0938`.

### Punto en el tiempo objetivo

Restaura a un punto específico en el tiempo:

```
pgmoneta-cli restore primary newest time=2025-01-15\ 10:30:00 /tmp
```

pgmoneta seleccionará el backup válido más reciente que comenzó antes o en la marca de tiempo especificada.
El formato de marca de tiempo es `YYYY-MM-DD HH:MM:SS`.

### Línea de tiempo objetivo

Restaura desde una línea de tiempo específica:

```
pgmoneta-cli restore primary newest timeline=2 /tmp
```

pgmoneta seleccionará el backup válido más reciente de la línea de tiempo especificada.

Y obtendrás el siguiente output:

```
Header:
  ClientVersion: 0.21.0
  Command: 3
  Output: 0
  Timestamp: 20240928130406
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Backup: newest
  Directory: /tmp
  Position: current
  Server: primary
Response:
  Backup: 20240928065644
  BackupSize: 8531968
  Comments: ''
  Compression: 2
  Encryption: 0
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 48799744
  Server: primary
  ServerVersion: 0.21.0
```


Este comando toma el backup más reciente y todos los segmentos Write-Ahead Log (WAL) y lo restaura en el directorio `/tmp/primary-20240928065644` para una copia actualizada.

## Restaurar desde S3

Si tus backups están almacenados en S3, primero necesitas descargarlos al directorio de backup local usando `pgmoneta-cli s3 restore`, y luego restaurar normalmente.

Paso 1: Descargar el backup desde S3

```
pgmoneta-cli s3 restore primary 20260316000957
```

Esto descarga los archivos de backup desde S3, verifica la integridad de `backup.info` usando SHA512, y coloca el backup en el directorio de backup local.

Paso 2: Restaurar el backup

```
pgmoneta-cli restore primary 20260316000957 current /tmp
```

Esto descomprime, desencripta, aplica el WAL, y produce un directorio de datos de PostgreSQL utilizable.

## Hot standby

Para usar hot standby, simplemente agrega

```
hot_standby = /your/local/hot/standby/directory
```

a la sección del servidor correspondiente en `pgmoneta.conf`. [**pgmoneta**][pgmoneta] creará el directorio si no existe,
y mantendrá el backup más reciente en el directorio definido.

También puedes configurar múltiples directorios de hot standby (hasta 8) proporcionando paths separados por comas:
```
/path/to/hot/standby1,/path/to/hot/standby2,/path/to/hot/standby3
```
[**pgmoneta**][pgmoneta] mantendrá copias idénticas del hot standby en todos los directorios especificados.

Puedes usar

```
hot_standby_overrides = /your/local/hot/standby/overrides/
```

para sobrescribir archivos en los directorios `hot_standby`. Las sobrescrituras se aplicarán a todos los directorios hot_standby.

### Tablespaces

Por defecto, los tablespaces se asignarán a una ruta similar a la original, por ejemplo `/tmp/mytblspc` se convierte en `/tmp/mytblspchs`.

Sin embargo, puedes usar el nombre del directorio para asignarlo a otro directorio, como

```
hot_standby_tablespaces = /tmp/mytblspc->/tmp/mybcktblspc
```

También puedes usar el `OID` para la parte clave, como

```
hot_standby_tablespaces = 16392->/tmp/mybcktblspc
```

Se pueden especificar múltiples tablespaces usando una `,` entre ellos.
