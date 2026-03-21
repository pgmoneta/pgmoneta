## APIs de servidor

### Resumen general

`pgmoneta` ofrece una colección de APIs diseñadas para interactuar con un cluster PostgreSQL. El acceso a algunos de estas APIs no está abierto a todos los usuarios por defecto; en su lugar, el rol que se conecta debe poseer ciertos permisos o roles predefinidos de PostgreSQL. Estos roles aseguran que solo usuarios con el nivel apropriado de autorización puedan invocar operaciones sensibles, manteniendo tanto la seguridad como el control de acceso apropiado dentro del cluster.

El documento se enfocará principalmente en funcionalidades y cosas sobre las que debes tener cuidado. Puede ofrecer algunos ejemplos de cómo usar las APIs.

### Dependencias

Las APIs de servidor dependen solamente de la instalación y configuración de `pgmoneta`. Esto requiere que [instales](https://github.com/pgmoneta/pgmoneta/blob/main/doc/manual/en/02-installation.md) el software y que crees un rol, proporciónale autenticación a este rol para acceder al cluster y configúralo con un servidor en las configuraciones de `pgmoneta`.

Para simplificar esta configuración y asegurar que todos los prerequisitos se cumplan, recomendamos seguir la [guía de inicio rápido](https://github.com/pgmoneta/pgmoneta/blob/main/doc/manual/en/03-quickstart.md) oficial.


### Permisos adicionales

Algunos server API requieren que el usuario de la conexión tenga permisos adicionales (ver en sección Server Functions). Para otorgar un rol predefinido específico (digamos <predefined_rolename>) a un usuario (digamos 'repl'), ejecuta el siguiente comando SQL:

```sh
GRANT <predefined_rolename> TO repl;
```

Puedes revisar la lista de todos los roles predefinidos disponibles en PostgreSQL 17 [aquí](https://www.postgresql.org/docs/17/predefined-roles.html).

### APIs

**pgmoneta_server_info**

Recupera información trivial sobre un servidor PostgreSQL configurado como:
- Versión de PostgreSQL del servidor
- estado de checksums (habilitado/deshabilitado)
- tipo de servidor (primary/non-primary)
- wal level, wal size, segment size, block size configurados
- estado de wal summarize (habilitado/deshabilitado)

Permiso:
- Predeterminado (todas las versiones de PostgreSQL)

**pgmoneta_server_valid**

Verifica si el servidor es válido validando si las propiedades dadas no están inicializadas:
- version
- wal size
- segment size
- block size

Permiso:
- Predeterminado (todas las versiones de PostgreSQL)

**pgmoneta_server_is_online**

Verifica si el servidor está activo/online para hacer conexiones. La estructura del servidor `pgmoneta` contiene un campo: `online`. Esta API funciona como un getter para ese campo. Esta función se usa en event loops de procesos de background para asegurar que el bg process se ejecuta mientras el servidor esté online.

Permiso:
- Predeterminado (todas las versiones de PostgreSQL)

**pgmoneta_server_set_online**

Haz un servidor específico como activo/online. Establece el campo `online` del struct de servidor a ya sea `true`/`false`.

Permiso:
- Predeterminado (todas las versiones de PostgreSQL)

**pgmoneta_server_verify_connection**

Verifica si una conexión a servidor es posible simplemente conectándose al `<host>:<port>` configurado del servidor y desconectándose justo después de conectarse.

Permiso: 
- Predeterminado (todas las versiones de PostgreSQL)

**pgmoneta_server_read_binary_file**

Solicita un chunk binario del archivo de relación desde el servidor. Toma como argumento:
- ruta al archivo relacional (archivo de datos)
- offset (posición inicial)
- length (el tamaño de bytes a ser recuperados)

Bajo el capó, esta API llama a la función admin `pg_read_binary_file(text, bigint, bigint, boolean)` que retorna los datos en formato `bytea`. La API también es responsable de serializar el `bytea` entrante a binario.

Permisos: 
- pg_read_server_files | SUPERUSER (PostgreSQL +11)
- SUPERUSER (de otra forma)

Nota: el usuario de la conexión debe ser otorgado el permiso `EXECUTE` en la función `pg_read_binary_file(text, bigint, bigint, boolean)`

**pgmoneta_server_checkpoint**

Fuerza un checkpoint en el cluster. Bajo el capó, primero llama al comando `CHECKPOINT;` y luego recupera el checkpoint LSN ejecutando `SELECT checkpoint_lsn, timeline_id FROM pg_control_checkpoint();`. Útil mientras se realizan backups/incremental backups.

Permisos:
- pg_checkpoint | SUPERUSER (PostgreSQL 15+)
- SUPERUSER (PostgreSQL 13/14)

**pgmoneta_server_file_stat**

Recupera metadatos de un archivo de datos como tamaño, tiempo de modificación, tiempo de creación etc. La estructura exacta de metadatos es la siguiente:

```c
struct file_stats
{
   size_t size;
   bool is_dir;
   struct tm timetamps[4];
};
```

* `size`: El tamaño del archivo al momento de la solicitud
* `is_dir`: si el archivo es un directorio (Nota: no diferencia entre un archivo regular y un symlink)
* `timestamps`: Un array (longitud 4) de información de timestamp relacionada al archivo
    * `access time`: tiempo en que el archivo fue accedido por última vez
    * `modification time`: tiempo en que el contenido del archivo fue cambiado por última vez
    * `changed time`: tiempo en que los perms/ownership de un archivo fueron cambiados por última vez
    * `creation time`: tiempo cuando el archivo fue creado

Los timestamps son representados en el formato `%Y-%m-%d %H:%M:%S` por el servidor

Permisos:
- Predeterminado (todas las versiones de PostgreSQL)

Nota: el usuario de la conexión debe ser otorgado el permiso `EXECUTE` en la función `pg_stat_file(text, boolean)`

**pgmoneta_server_backup_start**

Acepta una etiqueta de backup como entrada e invoca `do_pg_backup_start()` internamente, esto marca el inicio de un backup. Retorna un LSN que representa el punto de inicio del backup.

Permisos:
- Predeterminado (para todos los PostgreSQL 14+)

Nota: el usuario de la conexión debe ser otorgado el permiso `EXECUTE` en la función `pg_backup_start(text, boolean)` para PostgreSQL versión 15+ y `pg_start_backup(text, boolean, boolean)` para PostgreSQL versión < 15.

**pgmoneta_server_backup_stop**

Invoca `do_pg_backup_stop()` internamente, esto marca el fin de un backup, que fue previamente iniciado. Retorna un LSN que representa el punto final del backup y también el contenido de un archivo de `backup_label`.

Contenido del archivo de label (retornado por esta API):
- START WAL LOCATION
- CHECKPOINT LOCATION
- BACKUP METHOD
- BACKUP FROM
- LABEL
- START TIMELINE
- START TIME

Permisos:
- Predeterminado (para todos los PostgreSQL 14+)

Nota: el usuario de la conexión debe ser otorgado el permiso `EXECUTE` en la función `pg_backup_stop(boolean)` para PostgreSQL versión 15+ y `pg_stop_backup(boolean, boolean)` para PostgreSQL versión < 15.
