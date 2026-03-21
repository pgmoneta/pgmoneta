\newpage

# Prometheus / Grafana

[**pgmoneta**][pgmoneta] soporta métricas de [Prometheus][prometheus].

Habilitamos las métricas de [Prometheus][prometheus] en la configuración original estableciendo

```
metrics = 5001
```

en `pgmoneta.conf`.

## Acceder a las métricas de Prometheus

Ahora puedes acceder a las métricas a través de

```
http://localhost:5001/metrics
```

## Métricas

Las siguientes métricas están disponibles.
**pgmoneta_state**

Proporciona el estado operativo del servicio de backup de pgmoneta en sí, indicando si está en ejecución (1) o detenido/fallido (0).

| Valor | Descripción |
| :---- | :---------- |
| 1 | En ejecución |
| 0 | Detenido o encontró un error fatal durante el inicio/tiempo de ejecución |

**pgmoneta_version**

Expone la versión del servicio pgmoneta en ejecución a través de etiquetas.

| Atributo | Descripción |
| :-------- | :---------- |
| version | La cadena de versión semántica del pgmoneta en ejecución (por ejemplo, "0.20.0"). |

**pgmoneta_logging_info**

Cuenta el número total de mensajes de registro de nivel informativo (INFO) producidos por pgmoneta desde su último inicio.

**pgmoneta_logging_warn**

Acumula el recuento total de mensajes de advertencia (WARN level) registrados por pgmoneta, posiblemente indicando problemas recuperables.

**pgmoneta_logging_error**

Cuenta el número total de mensajes de error (ERROR level) de pgmoneta, a menudo señalando problemas que necesitan investigación.

**pgmoneta_logging_fatal**

Registra el recuento total de errores fatales (FATAL level) encontrados por pgmoneta, generalmente indicando terminación del servicio.

**pgmoneta_retention_days**

Muestra la política de retención global en días para los backups de pgmoneta.

| Valor | Descripción |
| :---- | :---------- |
| 0 | Sin retención basada en días configurada |

**pgmoneta_retention_weeks**

Muestra la política de retención global en semanas para los backups de pgmoneta.

| Valor | Descripción |
| :---- | :---------- |
| 0 | Sin retención basada en semanas configurada |

**pgmoneta_retention_months**

Muestra la política de retención global en meses para los backups de pgmoneta.

| Valor | Descripción |
| :---- | :---------- |
| 0 | Sin retención basada en meses configurada |

**pgmoneta_retention_years**

Muestra la política de retención global en años para los backups de pgmoneta.

| Valor | Descripción |
| :---- | :---------- |
| 0 | Sin retención basada en años configurada |

**pgmoneta_retention_server**

Muestra la política de retención para un servidor específico por tipo de parámetro (días, semanas, meses, años).

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| parameter | El tipo de parámetro de retención (días, semanas, meses, años). |

**pgmoneta_compression**

Indica el método de compresión utilizado para backups (0=ninguno, 1=gzip, 2=zstd, 3=lz4, 4=bzip2).

| Valor | Descripción |
| :---- | :---------- |
| 0 | Sin compresión |
| 1 | Compresión gzip |
| 2 | Compresión zstd |
| 3 | Compresión lz4 |
| 4 | Compresión bzip2 |

**pgmoneta_used_space**

Reporta el espacio en disco total en bytes actualmente usado por pgmoneta para todos los backups y datos.

**pgmoneta_free_space**

Reporta el espacio en disco libre en bytes disponible para pgmoneta para almacenar backups.

**pgmoneta_total_space**

Reporta el espacio en disco total en bytes disponible para pgmoneta (usado + libre).

**pgmoneta_wal_shipping**

Indica si WAL shipping está habilitado para un servidor (1=habilitado, 0=deshabilitado).

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_wal_shipping_used_space**

Reporta el espacio en disco en bytes usado para archivos WAL shipping para un servidor específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_wal_shipping_free_space**

Reporta el espacio en disco libre en bytes disponible para WAL shipping para un servidor específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_wal_shipping_total_space**

Reporta el espacio en disco total en bytes asignado para WAL shipping para un servidor específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_workspace**

Reporta el espacio en disco en bytes usado por el directorio workspace para un servidor específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_workspace_free_space**

Reporta el espacio en disco libre en bytes disponible en el workspace para un servidor específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_workspace_total_space**

Reporta el espacio en disco total en bytes disponible en el workspace para un servidor específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_hot_standby**

Reporta el espacio en disco en bytes usado para funcionalidad de hot standby para un servidor específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_hot_standby_free_space**

Reporta el espacio en disco libre en bytes disponible para hot standby para un servidor específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_hot_standby_total_space**

Reporta el espacio en disco total en bytes asignado para hot standby para un servidor específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_server_timeline**

Muestra el número de línea de tiempo actual en el que opera un servidor PostgreSQL.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_server_parent_tli**

Muestra el identificador de línea de tiempo padre para una línea de tiempo específica en un servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| tli | El identificador de línea de tiempo. |

**pgmoneta_server_timeline_switchpos**

Muestra la posición de cambio WAL para una línea de tiempo en un servidor (mostrada en hexadecimal).

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| tli | El identificador de línea de tiempo. |
| walpos | La posición WAL en formato hexadecimal. |

**pgmoneta_server_workers**

Reporta el número de procesos de trabajador actualmente activos para un servidor específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_server_online**

Indica si el servidor PostgreSQL está en línea y es accesible (1=en línea, 0=fuera de línea).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: Servidor está en línea y accesible, 0: Servidor está fuera de línea o no es accesible |

**pgmoneta_server_primary**

Indica si el servidor PostgreSQL opera como primario (1) o standby (0).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: Servidor es primario (no en recuperación), 0: Servidor es standby/réplica (en recuperación) |

**pgmoneta_server_valid**

Indica si la configuración del servidor y la conexión son válidas (1=válida, 0=inválida).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: Configuración del servidor es válida, 0: Configuración del servidor tiene problemas |

**pgmoneta_wal_streaming**

Indica si WAL streaming está actualmente activo para un servidor (1=activo, 0=inactivo).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: WAL streaming está activo, 0: WAL streaming no está activo |

**pgmoneta_server_operation_count**

Reporta el recuento total de operaciones de cliente exitosas realizadas en un servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_server_failed_operation_count**

Reporta el recuento total de operaciones de cliente fallidas intentadas en un servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_server_last_operation_time**

Reporta la marca de tiempo de la operación de cliente más reciente exitosa en un servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_server_last_failed_operation_time**

Reporta la marca de tiempo de la operación de cliente más reciente fallida en un servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_server_checksums**

Indica si las sumas de verificación de datos están habilitadas en el servidor PostgreSQL (1=habilitada, 0=deshabilitada).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: Las sumas de verificación de datos están habilitadas, 0: Las sumas de verificación de datos están deshabilitadas |

**pgmoneta_server_summarize_wal**

Indica si la sumarización de WAL está habilitada en el servidor PostgreSQL (1=habilitada, 0=deshabilitada).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: La sumarización de WAL está habilitada, 0: La sumarización de WAL está deshabilitada |

**pgmoneta_server_extensions_detected**

Reporta el número total de extensiones de PostgreSQL detectadas en el servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_server_extension**

Proporciona información sobre extensiones de PostgreSQL instaladas en el servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| extension | El nombre de la extensión instalada. |
| version | La versión de la extensión instalada. |
| comment | Una descripción de lo que hace la extensión. |

**pgmoneta_extension_pgmoneta_ext**

Reporta el estado de la extensión pgmoneta en el servidor PostgreSQL.

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: La extensión está instalada y disponible, 0: La extensión no está instalada |
| version | La versión de la extensión pgmoneta, o "not_installed" si no está presente. | |

**pgmoneta_backup_oldest**

Muestra la etiqueta/marca de tiempo del backup válido más antiguo para un servidor, o 0 si no existen backups válidos.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_backup_newest**

Muestra la etiqueta/marca de tiempo del backup válido más reciente para un servidor, o 0 si no existen backups válidos.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_backup_valid**

Reporta el número total de backups válidos/saludables disponibles para un servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_backup_invalid**

Reporta el número total de backups inválidos/corruptos para un servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_backup**

Indica si un backup específico es válido (1) o inválido (0).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: Backup es válido y usable, 0: Backup es inválido o corrupto |
| label | El identificador/marca de tiempo del backup. | |

**pgmoneta_backup_version**

Muestra la información de versión de PostgreSQL para un backup específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |
| major | El número de versión principal de PostgreSQL. |
| minor | El número de versión menor de PostgreSQL. |

**pgmoneta_backup_total_elapsed_time**

Reporta el tiempo total en segundos empleado en completar una operación de backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_basebackup_elapsed_time**

Reporta el tiempo en segundos empleado en la fase de base backup de una operación de backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_manifest_elapsed_time**

Reporta el tiempo en segundos empleado en procesamiento de manifest durante una operación de backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_compression_zstd_elapsed_time**

Reporta el tiempo en segundos empleado en compresión zstd durante una operación de backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_compression_gzip_elapsed_time**

Reporta el tiempo en segundos empleado en compresión gzip durante una operación de backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_compression_bzip2_elapsed_time**

Reporta el tiempo en segundos empleado en compresión bzip2 durante una operación de backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_compression_lz4_elapsed_time**

Reporta el tiempo en segundos empleado en compresión lz4 durante una operación de backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_encryption_elapsed_time**

Reporta el tiempo en segundos empleado en encriptación durante una operación de backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_linking_elapsed_time**

Reporta el tiempo en segundos empleado en operaciones de hard linking durante un backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_remote_ssh_elapsed_time**

Reporta el tiempo en segundos empleado en operaciones de almacenamiento remoto SSH durante un backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_remote_s3_elapsed_time**

Reporta el tiempo en segundos empleado en operaciones de almacenamiento remoto S3 durante un backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_remote_azure_elapsed_time**

Reporta el tiempo en segundos empleado en operaciones de almacenamiento remoto Azure durante un backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_start_timeline**

Muestra el número de línea de tiempo al inicio de una operación de backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_end_timeline**

Muestra el número de línea de tiempo al final de una operación de backup.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_start_walpos**

Muestra la posición WAL donde comenzó el backup (mostrada en hexadecimal).

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |
| walpos | La posición WAL en formato hexadecimal. |

**pgmoneta_backup_checkpoint_walpos**

Muestra la posición WAL de checkpoint para un backup (mostrada en hexadecimal).

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |
| walpos | La posición WAL de checkpoint en formato hexadecimal. |

**pgmoneta_backup_end_walpos**

Muestra la posición WAL donde terminó el backup (mostrada en hexadecimal).

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |
| walpos | La posición WAL final en formato hexadecimal. |

**pgmoneta_restore_newest_size**

Reporta el tamaño en bytes de la operación de restauración más reciente para un servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_backup_newest_size**

Reporta el tamaño en bytes del backup más reciente para un servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_restore_size**

Reporta el tamaño total en bytes de una operación de restauración específica.

**pgmoneta_restore_size_increment**

Reporta el tamaño incremental en bytes para una operación de restauración específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup/restauración. |

**pgmoneta_backup_size**

Reporta el tamaño total en bytes de un backup específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_compression_ratio**

Muestra la relación de compresión lograda para un backup específico (tamaño comprimido / tamaño original).

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_throughput**

Reporta el rendimiento general del backup en MB/s para una operación de backup específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_basebackup_mbs**

Reporta el rendimiento del base backup en MB/s para una operación de backup específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_manifest_mbs**

Reporta el rendimiento del procesamiento de manifest en MB/s para una operación de backup específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_compression_zstd_mbs**

Reporta el rendimiento de compresión zstd en MB/s para una operación de backup específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_compression_gzip_mbs**

Reporta el rendimiento de compresión gzip en MB/s para una operación de backup específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_compression_bzip2_mbs**

Reporta el rendimiento de compresión bzip2 en MB/s para una operación de backup específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_compression_lz4_mbs**

Reporta el rendimiento de compresión lz4 en MB/s para una operación de backup específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_encryption_mbs**

Reporta el rendimiento de encriptación en MB/s para una operación de backup específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_linking_mbs**

Reporta el rendimiento de hard linking en MB/s para una operación de backup específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_remote_ssh_mbs**

Reporta el rendimiento de almacenamiento remoto SSH en MB/s para una operación de backup específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_remote_s3_mbs**

Reporta el rendimiento de almacenamiento remoto S3 en MB/s para una operación de backup específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_remote_azure_mbs**

Reporta el rendimiento de almacenamiento remoto Azure en MB/s para una operación de backup específica.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| label | El identificador/marca de tiempo del backup. |

**pgmoneta_backup_retain**

Indica si un backup debe ser retenido (1) o es elegible para eliminación (0).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: Backup debe ser retenido, 0: Backup es elegible para eliminación |
| label | El identificador/marca de tiempo del backup. | |

**pgmoneta_backup_total_size**

Reporta el tamaño total en bytes de todos los backups para un servidor específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_wal_total_size**

Reporta el tamaño total en bytes de todos los archivos WAL para un servidor específico.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_total_size**

Reporta el tamaño total en bytes usado por un servidor (backups + WAL + otros datos).

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |

**pgmoneta_active_backup**

Indica si una operación de backup está actualmente en progreso para un servidor (1=activa, 0=inactiva).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: Backup está actualmente en ejecución, 0: Ningún backup está en ejecución |

**pgmoneta_active_restore**

Indica si una operación de restauración está actualmente en progreso para un servidor (1=activa, 0=inactiva).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: Restauración está actualmente en ejecución, 0: Ninguna restauración está en ejecución |

**pgmoneta_active_archive**

Indica si una operación de archivado está actualmente en progreso para un servidor (1=activa, 0=inactiva).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: Operación de archivado está en ejecución, 0: Ninguna operación de archivado está en ejecución |

**pgmoneta_active_delete**

Indica si una operación de eliminación está actualmente en progreso para un servidor (1=activa, 0=inactiva).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: Operación de eliminación está en ejecución, 0: Ninguna operación de eliminación está en ejecución |

**pgmoneta_active_retention**

Indica si una operación de limpieza de retención está actualmente en progreso para un servidor (1=activa, 0=inactiva).

| Atributo | Descripción | Valores |
| :-------- | :---------- | :----- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. | 1: Limpieza de retención está en ejecución, 0: Ninguna limpieza de retención está en ejecución |

**pgmoneta_current_wal_file**

Muestra el nombre de archivo WAL actual siendo transmitido o procesado para un servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| file | El nombre de archivo WAL actual. |

**pgmoneta_current_wal_lsn**

Muestra el Log Sequence Number (LSN) WAL actual para un servidor.

| Atributo | Descripción |
| :-------- | :---------- |
| name | El nombre/identificador configurado para el servidor PostgreSQL. |
| lsn | El LSN WAL actual en formato hexadecimal. |


## Soporte de Transport Level Security

Para agregar soporte TLS para métricas de Prometheus, primero necesitamos un certificado autofirmado.
1. Genera clave CA y certificado
```bash
openssl genrsa -out ca.key 2048
openssl req -x509 -new -nodes -key ca.key -sha256 -days 3650 -out ca.crt -subj "/CN=My Local CA"
```

2. Genera clave del servidor y CSR
```bash
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr -subj "/CN=localhost"
```

3. Crea un archivo de configuración para Subject Alternative Name
```bash
cat > server.ext << EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
subjectAltName = @alt_names

[alt_names]
DNS.1 = localhost
IP.1 = 127.0.0.1
EOF
```

4. Firma el certificado del servidor con nuestra CA
```bash
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt -days 3650 -sha256 -extfile server.ext
```

5. Genera clave del cliente y certificado
```bash
openssl genrsa -out client.key 2048
openssl req -new -key client.key -out client.csr -subj "/CN=Client Certificate"
openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out client.crt -days 3650 -sha256
```

6. Crea archivo PKCS#12 (Opcional, necesario para importar en navegador)
```bash
openssl pkcs12 -export -out client.p12 -inkey client.key -in client.crt -certfile ca.crt -passout pass:<your_password>
```

Edita `pgmoneta.conf` para agregar las siguientes claves bajo la sección pgmoneta:
```
[pgmoneta]
.
.
.
metrics_cert_file=<path_to_server_cert_file>
metrics_key_file=<path_to_server_key_file>
metrics_ca_file=<path_to_ca_file>
```

Ahora puedes acceder a las métricas en `https://localhost:5001` usando curl de la siguiente manera:
```
curl -v -L "https://localhost:5001" --cacert <path_to_ca_file> --cert <path_to_client_cert_file> --key <path_to_client_key_file>
```

(Opcional) Si deseas acceder a la página a través del navegador:
- Primero instala los certificados en tu sistema
    - Para Fedora:
    ```
    # Crea directorio si no existe
    sudo mkdir -p /etc/pki/ca-trust/source/anchors/

    # Copia certificado CA al almacén de confianza
    sudo cp ca.crt /etc/pki/ca-trust/source/anchors/

    # Actualiza el almacén de confianza CA
    sudo update-ca-trust extract
    ```

    - Para Ubuntu:
    ```
    # Copia el certificado CA al almacén de certificados del sistema
    sudo cp ca.crt /usr/local/share/ca-certificates/

    # Actualiza el almacén de certificados CA
    sudo update-ca-certificates
    ```

    - Para MacOS:
        - Abre Keychain Access e importa el archivo de certificado
        - Establece el certificado en "Always Trust"

- Para navegadores como Firefox
    - Ve a Menú → Preferencias → Privacidad y Seguridad
    - Desplázate hacia abajo a la sección "Certificados" y haz clic en "Ver Certificados"
    - Ve a la pestaña "Autoridades" y haz clic en "Importar"
    - Selecciona tu archivo `ca.crt`
    - Marca "Confiar en esta CA para identificar sitios web" y haz clic en OK
    - Ve a la pestaña "Tus Certificados"
    - Haz clic en "Importar" y selecciona tu archivo `client.p12`
    - Ingresa la contraseña que estableciste al crear el archivo PKCS#12

- Para navegadores como Chrome/Chromium
    - Para certificados de cliente, ve a Configuración → Privacidad y seguridad → Seguridad → Administrar certificados
    - Haz clic en "Importar" y selecciona tu archivo `client.p12`
    - Ingresa la contraseña que estableciste al crear

Ahora puedes acceder a métricas en `https://localhost:5001`

## Grafana

Habilita el endpoint agregando

```yml
scrape_configs:
  - job_name: 'pgmoneta'
    metrics_path: '/metrics'
    static_configs:
      - targets: ['localhost:5001']
```

a la configuración de Grafana.

Entonces el servicio Prometheus consultará tus métricas de [**pgmoneta**][pgmoneta] cada 15 segundos y las empaquetará como datos de series de tiempo. Puedes consultar tus métricas de [**pgmoneta**][pgmoneta] y ver sus cambios conforme pasa el tiempo en la página web de Prometheus (puerto predeterminado es `9090`).

![](../images/prometheus_console.jpg)

![](../images/prometheus_graph.jpg)

### Importar un dashboard de Grafana

Aunque Prometheus proporciona capacidad de consultar y monitorear métricas, no podemos personalizar gráficos para cada métrica y proporcionar una vista unificada.

Como resultado, usamos Grafana para ayudarnos a administrar todos los gráficos juntos. En primer lugar, debemos instalar Grafana en la computadora donde necesitas monitorear métricas de [**pgmoneta**][pgmoneta]. Puedes navegar por la página web de Grafana con puerto predeterminado `3000`, usuario predeterminado `admin` y contraseña predeterminada `admin`. Entonces puedes crear una fuente de datos Prometheus de [**pgmoneta**][pgmoneta].

![](../images/grafana_datasource.jpg)

Finalmente puedes crear un dashboard importando `contrib/grafana/dashboard.json` y monitorear métricas sobre [**pgmoneta**][pgmoneta].

![](../images/grafana_dashboard.jpg)
