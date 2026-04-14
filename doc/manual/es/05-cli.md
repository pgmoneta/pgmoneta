\newpage

# Interfaz de línea de comandos

La interfaz de línea de comandos **pgmoneta-cli** controla tu interacción con **pgmoneta**.

**Es importante que uses solo la interfaz de línea de comandos pgmoneta-cli para operar en tu directorio de backup.**

Usar otros comandos en el directorio de backup podría causar problemas.

``` sh
pgmoneta-cli 0.21.0
  Command line utility for pgmoneta

Usage:
  pgmoneta-cli [ -c CONFIG_FILE ] [ COMMAND ]

Options:
  -c, --config CONFIG_FILE                        Set the path to the pgmoneta_cli.conf file
  -h, --host HOST                                 Set the host name
  -p, --port PORT                                 Set the port number
  -U, --user USERNAME                             Set the user name
  -P, --password PASSWORD                         Set the password
  -L, --logfile FILE                              Set the log file
  -v, --verbose                                   Output text string of result
  -V, --version                                   Display version information
  -F, --format text|json|raw                      Set the output format
  -C, --compress none|gz|zstd|lz4|bz2             Compress the wire protocol
  -E, --encrypt none|aes|aes256|aes192|aes128     Encrypt the wire protocol
  -s, --sort asc|desc                             Sort result (for list-backup)
      --cascade                                   Cascade a retain/expunge backup
      --force                                     Force delete a backup
  -?, --help                                      Display help

Commands:
  annotate                 Annotate a backup with comments
  archive                  Archive a backup from a server
  backup                   Backup a server
  clear <what>             Clear data, with:
                           - 'prometheus' to reset the Prometheus statistics
  compress                 Compress a file using configured method
  conf <action>            Manage the configuration, with one of subcommands:
                           - 'get' to obtain information about a runtime configuration value
                             conf get <parameter_name>
                           - 'ls' to print the configurations used
                           - 'reload' to reload the configuration
                           - 'set' to modify a configuration value;
                             conf set <parameter_name> <parameter_value>;
  s3  <action>             Manage S3, with one of subcommands:
                           - 'ls' to get the list of files in s3

  decompress               Decompress a file using configured method
  decrypt                  Decrypt a file using master-key
  delete                   Delete a backup from a server
  encrypt                  Encrypt a file using master-key
  expunge                  Expunge a backup from a server
  info                     Information about a backup
  list-backup              List the backups for a server
  mode                     Switch the mode for a server
  ping                     Check if pgmoneta is alive
  progress                 Get progress for a command
  restore                  Restore a backup from a server
  retain                   Retain a backup from a server
  shutdown                 Shutdown pgmoneta
  status [details]         Status of pgmoneta, with optional details
  verify                   Verify a backup from a server

pgmoneta: https://pgmoneta.github.io/
Report bugs: https://github.com/pgmoneta/pgmoneta/issues
```

## backup

Hacer backup de un servidor

El comando para un backup completo es

``` sh
pgmoneta-cli backup <server>
```

Ejemplo

``` sh
pgmoneta-cli backup primary
```

El comando para un backup incremental es

``` sh
pgmoneta-cli backup <server> <identifier>
```

donde el `identifier` es el identificador para un backup.

Ejemplo

``` sh
pgmoneta-cli backup primary 20250101120000
```

## list-backup

Listar los backups para un servidor

Comando

``` sh
pgmoneta-cli list-backup <server> [--sort asc|desc]
```

La opción `--sort` permite ordenar backups por timestamp:
- `asc` para orden ascendente (más antiguos primero)
- `desc` para orden descendente (más nuevos primero)

Ejemplo

``` sh
pgmoneta-cli list-backup primary
```

Ejemplo con ordenamiento

``` sh
pgmoneta-cli list-backup primary --sort desc
```

## restore

Restaurar un backup de un servidor

Comando

``` sh
pgmoneta-cli restore <server> [<timestamp>|oldest|newest] [[current|name=X|xid=X|lsn=X|time=X|inclusive=X|timeline=X|action=X|primary|replica],*] <directory>
```

donde

* `current` significa copiar el Write-Ahead Log (WAL), y restaurar al primer checkpoint estable
* `name=X` significa copiar el Write-Ahead Log (WAL), y restaurar a la etiqueta especificada
* `xid=X` significa copiar el Write-Ahead Log (WAL), y restaurar al XID especificado
* `time=X` significa copiar el Write-Ahead Log (WAL), y restaurar al timestamp especificado
* `lsn=X` significa copiar el Write-Ahead Log (WAL), y restaurar al Log Sequence Number (LSN) especificado
* `inclusive=X` significa que la restauración es inclusiva de la información especificada
* `timeline=X` significa que la restauración se realiza a la timeline de información especificada
* `action=X` significa qué acción debe ejecutarse después de la restauración (pause, shutdown)

[Más información](https://www.postgresql.org/docs/current/runtime-config-wal.html#RUNTIME-CONFIG-WAL-RECOVERY-TARGET)

Ejemplo

``` sh
pgmoneta-cli restore primary newest name=MyLabel,primary /tmp
```

### Selección automática de Backup

Cuando se usan destinos de recuperación (`lsn=X`, `time=X`, o `timeline=X`), pgmoneta selecciona automáticamente el backup apropiado:

```sh
# Restaurar a un LSN específico (backup seleccionado automáticamente)
pgmoneta-cli restore primary newest lsn=0/16B0938 /tmp

# Restaurar a una hora específica (backup seleccionado automáticamente)
pgmoneta-cli restore primary newest time=2025-01-15\ 10:30:00 /tmp

# Restaurar desde una timeline específica (backup seleccionado automáticamente)
pgmoneta-cli restore primary newest timeline=2 /tmp
```

## s3 restore

Descarga un backup desde S3 al directorio de backup local

Este subcomando descarga un backup desde un almacenamiento S3 y lo hace disponible localmente. El flujo es:

* Descarga `backup.sha512`, `backup.info`, y `backup.manifest` desde S3
* Verifica la integridad de `backup.info` usando el checksum SHA512
* Descarga todos los archivos de datos listados en el manifiesto, aplicando las extensiones correctas de compresión y encriptación
* Mueve atómicamente el backup descargado al directorio de backup local

Después de que `s3 restore` se complete, el backup aparece en `status details` y puede ser restaurado usando `pgmoneta-cli restore`.

Comando

``` sh
pgmoneta-cli s3 restore <server> <timestamp>
```

Ejemplo

``` sh
pgmoneta-cli s3 restore primary 20260316000957
```

## verify

Verificar un backup de un servidor

Comando

``` sh
pgmoneta-cli verify <server> <directory> [failed|all]
```

Ejemplo

``` sh
pgmoneta-cli verify primary oldest /tmp
```

## archive

Archivar un backup de un servidor

Comando

``` sh
pgmoneta-cli archive <server> [<timestamp>|oldest|newest] [[current|name=X|xid=X|lsn=X|time=X|inclusive=X|timeline=X|action=X|primary|replica],*] <directory>
```

Ejemplo

``` sh
pgmoneta-cli archive primary newest current /tmp
```

## delete

Eliminar un backup de un servidor

Comando

``` sh
pgmoneta-cli delete [--force] <server> [<timestamp>|oldest|newest]
```

Ejemplo

``` sh
pgmoneta-cli delete primary oldest
```

## retain

Retener un backup de un servidor. El backup no será eliminado por la política de retención

Comando

``` sh
pgmoneta-cli retain [--cascade] <server> [<timestamp>|oldest|newest]
```

Ejemplo

``` sh
pgmoneta-cli retain primary oldest
```

## expunge

Expulsar un backup de un servidor. El backup será eliminado por la política de retención

Comando

``` sh
pgmoneta-cli expunge [--cascade] <server> [<timestamp>|oldest|newest]
```

Ejemplo

``` sh
pgmoneta-cli expunge primary oldest
```

## encrypt

Encriptar el archivo en su lugar, eliminar archivo sin encriptar después de la encriptación exitosa.

Comando

``` sh
pgmoneta-cli encrypt <file>
```

## decrypt

Desencriptar el archivo en su lugar, eliminar archivo encriptado después de la desencriptación exitosa.

Comando

``` sh
pgmoneta-cli decrypt <file>
```

## compress

Comprimir el archivo en su lugar, eliminar archivo sin comprimir después de la compresión exitosa.

Comando

``` sh
pgmoneta-cli compress <file>
```

## decompress

Descomprimir el archivo en su lugar, eliminar archivo comprimido después de la descompresión exitosa.

Comando

``` sh
pgmoneta-cli decompress <file>
```

## info

Información sobre un backup.

Comando

``` sh
pgmoneta-cli info <server> <timestamp|oldest|newest>
```

## ping

Verificar si [**pgmoneta**][pgmoneta] está vivo

Comando

``` sh
pgmoneta-cli ping
```

Ejemplo

``` sh
pgmoneta-cli ping
```

## progress

Obtener progreso para un comando de respaldo. Requiere `progress = on` en la configuración.

Comando

``` sh
pgmoneta-cli progress <server> backup
```

Ejemplo

``` sh
pgmoneta-cli progress primary backup
```

## mode

[**pgmoneta**][pgmoneta] detecta cuando un servidor está caído. Puedes poner un servidor en línea u offline
usando el comando mode.

Comando

```
pgmoneta-cli mode <server> <online|offline>
```

Ejemplo

```
pgmoneta-cli mode primary offline
```

o

```
pgmoneta-cli mode primary online
```

[**pgmoneta**][pgmoneta] mantendrá servicios básicos ejecutándose para un servidor offline de modo que
puedas verificar un backup o hacer una restauración.

## shutdown

Apagar [**pgmoneta**][pgmoneta]

Comando

``` sh
pgmoneta-cli shutdown
```

Ejemplo

``` sh
pgmoneta-cli shutdown
```

## status

Estado de [**pgmoneta**][pgmoneta], con una opción `details`

Comando

``` sh
pgmoneta-cli status [details]
```

Ejemplo

``` sh
pgmoneta-cli status details
```

## conf

Gestionar la configuración

Comando

```sh
pgmoneta-cli conf [reload | ls | get | set]
```

Subcomando

- `reload`: Recargar configuración
- `ls` : Para imprimir las configuraciones utilizadas
- `get <config_key>` : Para obtener información sobre un valor de configuración en tiempo de ejecución
- `set <config_key> <config_value>` : Para modificar el valor de configuración en tiempo de ejecución

Ejemplo

```sh
pgmoneta-cli conf reload
pgmoneta-cli conf ls
pgmoneta-cli conf get server.primary.host
pgmoneta-cli conf set encryption aes-256-cbc
```
**conf get**

Obtener el valor de una clave de configuración en tiempo de ejecución, o la configuración completa.

- Si proporcionas un `<config_key>`, obtendrás el valor para esa clave.
  - Para claves de sección principal, puedes usar solo la clave (por ejemplo, `host`) o con la sección (por ejemplo, `pgmoneta.host`).
  - Para claves de sección de servidor, usa el nombre del servidor como sección (por ejemplo, `server.primary.host`, `server.myserver.port`).
- Si ejecutas `pgmoneta-cli conf get` sin ninguna clave, se mostrará la configuración completa.

Ejemplos

```sh
pgmoneta-cli conf get
pgmoneta-cli conf get host
pgmoneta-cli conf get pgmoneta.host
pgmoneta-cli conf get server.primary.host
pgmoneta-cli conf get server.myserver.port
```
`
**conf set**

Establecer el valor de un parámetro de configuración en tiempo de ejecución.

**Sintaxis:**
```sh
pgmoneta-cli conf set <config_key> <config_value>
```

Ejemplos

```sh
# Registro y monitoreo
pgmoneta-cli conf set log_level debug5
pgmoneta-cli conf set metrics 5001
pgmoneta-cli conf set management 5002

# Ajuste de rendimiento
pgmoneta-cli conf set workers 4
pgmoneta-cli conf set max_rate 1000000
pgmoneta-cli conf set compression zstd

# Políticas de retención
pgmoneta-cli conf set retention "14,2,6,1"
```

**Formatos de clave:**
- **Parámetros de sección principal**: `key` o `pgmoneta.key`
  - Ejemplos: `log_level`, `pgmoneta.metrics`
- **Parámetros de sección de servidor**: `server.server_name.key` solo
  - Ejemplos: `server.primary.port`, `server.primary.host`

**Notas importantes:**
- Establecer `metrics=0` o `management=0` desactiva esos servicios
- Los números de puerto inválidos pueden mostrar éxito pero causar fallos de servicio (consulta los registros del servidor)
- La configuración del servidor usa el formato `server.name.parameter` (no `name.parameter`)

**Tipos de respuesta:**
- **Éxito (Aplicado)**: Cambio de configuración aplicado a la instancia en ejecución inmediatamente
- **Éxito (Reinicio requerido)**: Cambio de configuración validado pero requiere actualización manual de archivos de configuración Y reinicio
- **Error**: Formato de clave inválido, falla de validación u otros errores

**Importante: Cambios que requieren reinicio**
Cuando un cambio de configuración requiere reinicio, el cambio solo se valida y se almacena temporalmente en memoria. Para hacer el cambio permanente:

1. **Edita manualmente el archivo de configuración** (por ejemplo, `/etc/pgmoneta/pgmoneta.conf`)
3. **Reinicia pgmoneta** usando `systemctl restart pgmoneta` o equivalente

**Advertencia:** Simplemente reiniciar pgmoneta sin actualizar los archivos de configuración **revertirá** el cambio de vuelta a la configuración basada en archivos.

**Ejemplo del proceso que requiere reinicio:**
```sh
# 1. Intentar cambiar host (requiere reinicio)
pgmoneta-cli conf set host 192.168.1.100
# Salida: Configuration change requires manual restart
#         Current value: localhost (unchanged in running instance)
#         Requested value: 192.168.1.100 (cannot be applied to live instance)

# 2. Editar manualmente /etc/pgmoneta/pgmoneta.conf
sudo nano /etc/pgmoneta/pgmoneta.conf
# Cambiar: host = localhost
# A:       host = 192.168.1.100

# 3. Reiniciar pgmoneta
sudo systemctl restart pgmoneta

# 4. Verificar el cambio
pgmoneta-cli conf get host
# Salida: 192.168.1.100
```

**Por qué se requiere edición manual de archivos:**
- `pgmoneta-cli conf set` solo valida y almacena temporalmente cambios que requieren reinicio
- Los archivos de configuración **no se actualizan automáticamente** por el comando
- En el reinicio, pgmoneta siempre lee desde los archivos de configuración en disco
- Sin actualizaciones de archivos, el reinicio revertirá a los valores originales basados en archivos

## s3 

Gestionar el almacenamiento s3 

Comando

```sh
pgmoneta-cli s3 <action> <arguments>
```

Subcomando

- `ls` : Listar todos los archivos en s3
- `delete` : Eliminar todos los archivos bajo un prefijo remoto en s3
- `restore` : Descargar un backup desde s3 al directorio de backup local

Ejemplo

```sh
pgmoneta-cli s3 ls primary
pgmoneta-cli s3 delete primary 20260302163357
pgmoneta-cli s3 restore primary 20260316000957
```

### s3 ls

Obtener la lista de archivos/objetos del servidor en el almacenamiento remoto s3

- puedes establecer el servidor o usar la sección [pgmoneta] en la configuración

Ejemplos

```sh
pgmoneta-cli s3 ls primary
pgmoneta-cli s3 ls
```

### s3 delete

Eliminar todos los archivos/objetos del servidor en almacenamiento remoto s3 bajo un prefijo dado.

- prefix es relativo a `<s3_base_dir>/<server>/backup/`

Ejemplos

```sh
pgmoneta-cli s3 delete primary 20260302163357/
pgmoneta-cli s3 delete primary wal/
```

### s3 restore

Descarga un backup desde S3 al directorio de backup local.

- Descarga y verifica la integridad de `backup.info` mediante SHA512
- Descarga todos los archivos de datos con las extensiones de compresión/encriptación correctas
- El backup queda disponible para `pgmoneta-cli restore`

Ejemplos

``` sh
pgmoneta-cli s3 restore primary 20260316000957
```

## clear

Limpiar datos/estadísticas

Comando

``` sh
pgmoneta-cli clear [prometheus]
```

Subcomando

- `prometheus`: Restablecer las estadísticas de Prometheus

Ejemplo

``` sh
pgmoneta-cli clear prometheus
```

## Completaciones de shell

Hay soporte minimal de completación de shell para `pgmoneta-cli`.

Por favor, consulta el manual para información detallada sobre cómo habilitar y usar completaciones de shell.
