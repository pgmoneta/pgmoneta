\newpage

# Configuración

## pgmoneta.conf

La configuración se carga desde la ruta especificada por la flag `-c` o desde `/etc/pgmoneta/pgmoneta.conf`.

La configuración de `pgmoneta` se divide en secciones usando los caracteres `[` y `]`.

La sección principal, llamada `[pgmoneta]`, es donde configuras las propiedades generales
de `pgmoneta`.

Otras secciones no tienen requisitos para sus nombres, así que puedes darles
nombres significativos como `[primary]` para la instancia principal de [PostgreSQL](https://www.postgresql.org).

Todas las propiedades están en el formato `clave = valor`.

Los caracteres `#` y `;` se pueden usar para comentarios; deben ser el primer carácter en la línea.
El tipo de dato `Bool` soporta los siguientes valores: `on`, `yes`, `1`, `true`, `off`, `no`, `0` y `false`.

Consulta una [configuración de ejemplo](./etc/pgmoneta.conf) para ejecutar `pgmoneta` en `localhost`.

Ten en cuenta que se requiere PostgreSQL 13+, así como tener `wal_level` en nivel `replica` o `logical`.

### pgmoneta

**General**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| host | | String | Sí | La dirección de enlace para pgmoneta |
| unix_socket_dir | | String | Sí | La ubicación del Socket de Dominio Unix |
| base_dir | | String | Sí | El directorio base para el backup |

Nota: Si `host` comienza con un `/`, representa una ruta y `pgmoneta` se conectará usando un Socket de Dominio Unix.

**Monitoreo**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| metrics | 0 | Int | No | El puerto de métricas (desactivado = 0) |
| metrics_cache_max_age | 0 | String | No | El tiempo para mantener una respuesta de Prometheus (métricas) en caché. Si este valor se especifica sin unidades, se toma como segundos. Establecer este parámetro a 0 desactiva el almacenamiento en caché. Soporta los siguientes sufijos de unidades: 'S' para segundos (por defecto), 'M' para minutos, 'H' para horas, 'D' para días y 'W' para semanas. |
| metrics_cache_max_size | 256k | String | No | La cantidad máxima de datos a mantener en caché al servir respuestas de Prometheus. Los cambios requieren reinicio. Este parámetro determina el tamaño de la memoria asignada para el caché incluso si `metrics_cache_max_age` o `metrics` están desactivados. Su valor, sin embargo, se tiene en cuenta solo si `metrics_cache_max_age` se establece en un valor distinto de cero. Soporta sufijos: 'B' (bytes), el predeterminado si se omite, 'K' o 'KB' (kilobytes), 'M' o 'MB' (megabytes), 'G' o 'GB' (gigabytes).|
| metrics_cert_file | | String | No | Archivo de certificado para TLS de métricas de Prometheus. Este archivo debe ser propiedad del usuario que ejecuta pgmoneta o root. |
| metrics_key_file | | String | No | Archivo de clave privada para TLS de métricas de Prometheus. Este archivo debe ser propiedad del usuario que ejecuta pgmoneta o root. Además, los permisos deben ser al menos `0640` si es propiedad de root o `0600` en caso contrario. |
| metrics_ca_file | | String | No | Archivo de Autoridad de Certificación (CA) para TLS de métricas de Prometheus. Este archivo debe ser propiedad del usuario que ejecuta pgmoneta o root.  |

**Gestión**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| management | 0 | Int | No | El puerto de gestión remota (desactivado = 0) |

**Compresión**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| compression | zstd | String | No | El tipo de compresión (none, gzip, client-gzip, server-gzip, zstd, client-zstd, server-zstd, lz4, client-lz4, server-lz4, bzip2, client-bzip2) |
| compression_level | 3 | Int | No | El nivel de compresión |

**Trabajadores**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| workers | 0 | Int | No | El número de trabajadores que cada proceso puede usar. Usa 0 para desactivar. El máximo es el número de CPUs |

**Espacio de trabajo**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| workspace | /tmp/pgmoneta-workspace/ | String | No | El directorio para el espacio de trabajo que el backup incremental puede usar para su trabajo |

**Almacenamiento**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| storage_engine | local | String | No | El tipo de motor de almacenamiento (local, ssh, s3, azure) |

**Encriptación**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| encryption | none | String | No | El modo de encriptación para encriptar WAL y datos<br/> `none`: Sin encriptación <br/> `aes \| aes-256 \| aes-256-cbc`: AES CBC (Cipher Block Chaining) modo con clave de 256 bits<br/> `aes-192 \| aes-192-cbc`: AES CBC modo con clave de 192 bits<br/> `aes-128 \| aes-128-cbc`: AES CBC modo con clave de 128 bits<br/> `aes-256-ctr`: AES CTR (Counter) modo con clave de 256 bits<br/> `aes-192-ctr`: AES CTR modo con clave de 192 bits<br/> `aes-128-ctr`: AES CTR modo con clave de 128 bits |

**Gestión de slots**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| create_slot | no | Bool | No | Crear un slot de replicación para todos los servidores. Los valores válidos son: yes, no |

**SSH**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| ssh_hostname | | String | Sí | Define el nombre del host del sistema remoto para la conexión |
| ssh_username | | String | Sí | Define el nombre de usuario del sistema remoto para la conexión |
| ssh_base_dir | | String | Sí | El directorio base para el backup remoto |
| ssh_ciphers | aes-256-ctr, aes-192-ctr, aes-128-ctr | String | No | Los cifrados soportados para la comunicación. `aes \| aes-256 \| aes-256-cbc`: AES CBC (Cipher Block Chaining) modo con clave de 256 bits<br/> `aes-192 \| aes-192-cbc`: AES CBC modo con clave de 192 bits<br/> `aes-128 \| aes-128-cbc`: AES CBC modo con clave de 128 bits<br/> `aes-256-ctr`: AES CTR (Counter) modo con clave de 256 bits<br/> `aes-192-ctr`: AES CTR modo con clave de 192 bits<br/> `aes-128-ctr`: AES CTR modo con clave de 128 bits. En caso contrario textualmente |
| ssh_public_key_file | `$HOME/.ssh/id_rsa.pub` | String | No | La ruta del archivo de clave pública SSH. Puede interpolar variables de entorno (por ejemplo, `$HOME`).   |
| ssh_private_key_file | `$HOME/.ssh/id_rsa` | String | No | La ruta del archivo de clave privada SSH. Puede interpolar variables de entorno (por ejemplo, `$HOME`) |

**S3**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| s3_region | | String | Sí | La región de AWS |
| s3_access_key_id | | String | Sí | El ID de clave de acceso IAM |
| s3_secret_access_key | | String | Sí | La clave de acceso secreta IAM |
| s3_bucket | | String | Sí | El nombre del bucket S3 |
| s3_base_dir | | String | Sí | El directorio base para el bucket S3 |

**Azure**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| azure_storage_account | | String | Sí | El nombre de la cuenta de almacenamiento de Azure |
| azure_container | | String | Sí | El nombre del contenedor de Azure |
| azure_shared_key | | String | Sí | La clave de la cuenta de almacenamiento de Azure |
| azure_base_dir | | String | Sí | El directorio base para el contenedor de Azure |

**Retención**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| retention | 7, - , - , - | Array | No | El tiempo de retención en días, semanas, meses, años |

**Verificación**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| verification | 0 | String | No | El tiempo entre verificación de un backup. Si este valor se especifica sin unidades,
  se toma como segundos. Establecer este parámetro a 0 desactiva la verificación. Soporta
  los siguientes sufijos de unidades: 'S' para segundos (por defecto), 'M' para minutos, 'H' para horas, 'D'
  para días y 'W' para semanas. El valor predeterminado es 0 (desactivado) |

**Registro (Logging)**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| log_type | console | String | No | El tipo de log (console, file, syslog) |
| log_level | info | String | No | El nivel de logging, cualquiera de las cadenas (insensibles a mayúsculas) `FATAL`, `ERROR`, `WARN`, `INFO` y `DEBUG` (que puede ser más específico como `DEBUG1` hasta `DEBUG5`). El nivel de depuración mayor que 5 se establecerá a `DEBUG5`. Los valores no reconocidos harán que `log_level` sea `INFO` |
| log_path | pgmoneta.log | String | No | La ubicación del archivo de log. Puede ser una cadena compatible con strftime(3). |
| log_rotation_age | 0 | String | No | El tiempo después del cual se desencadena la rotación del archivo de log. Si este valor se especifica sin unidades, se toma como segundos. Establecer este parámetro a 0 desactiva la rotación de registros basada en tiempo. Soporta los siguientes sufijos de unidades: 'S' para segundos (por defecto), 'M' para minutos, 'H' para horas, 'D' para días y 'W' para semanas. |
| log_rotation_size | 0 | String | No | El tamaño del archivo de log que desencadenará una rotación de registros. Soporta sufijos: 'B' (bytes), el predeterminado si se omite, 'K' o 'KB' (kilobytes), 'M' o 'MB' (megabytes), 'G' o 'GB' (gigabytes). Un valor de `0` (con o sin sufijo) desactiva. |
| log_line_prefix | %Y-%m-%d %H:%M:%S | String | No | Una cadena compatible con strftime(3) para usar como prefijo en cada línea de log. Debe ser entrecomillada si contiene espacios. |
| log_mode | append | String | No | Agregar o crear el archivo de log (append, create) |

**Transport Level Security**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| tls | `off` | Bool | No | Habilitar Transport Layer Security (TLS) |
| tls_cert_file | | String | No | Archivo de certificado para TLS. Este archivo debe ser propiedad del usuario que ejecuta pgmoneta o root. |
| tls_key_file | | String | No | Archivo de clave privada para TLS. Este archivo debe ser propiedad del usuario que ejecuta pgmoneta o root. Además, los permisos deben ser al menos `0640` si es propiedad de root o `0600` en caso contrario. |
| tls_ca_file | | String | No | Archivo de Autoridad de Certificación (CA) para TLS. Este archivo debe ser propiedad del usuario que ejecuta pgmoneta o root.  |
| libev | `auto` | String | No | Selecciona el backend de [libev](http://software.schmorp.de/pkg/libev.html) a usar. Opciones válidas: `auto`, `select`, `poll`, `epoll`, `iouring`, `devpoll` y `port` |

**Miscelánea (Miscellaneous)**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| max_rate | 0 | Int | No | La velocidad máxima de transferencia de backup en bytes por segundo. Usa 0 para desactivar |
| progress | off | Bool | No | Habilitar seguimiento del progreso de backup |
| blocking_timeout | 30 | String | No | El número de segundos que el proceso se bloqueará esperando una conexión. Si este valor se especifica sin unidades, se toma como segundos. Establecer este parámetro a 0 lo desactiva. Soporta los siguientes sufijos de unidades: 'S' para segundos (por defecto), 'M' para minutos, 'H' para horas, 'D' para días y 'W' para semanas. |
| keep_alive | on | Bool | No | Tener `SO_KEEPALIVE` en sockets |
| nodelay | on | Bool | No | Tener `TCP_NODELAY` en sockets |
| non_blocking | on | Bool | No | Tener `O_NONBLOCK` en sockets |
| backlog | 16 | Int | No | El backlog para `listen()`. Mínimo `16` |
| hugepage | `try` | String | No | Soporte de página grande (`off`, `try`, `on`) |
| direct_io | `off` | String | No | Soporte de Direct I/O para almacenamiento local (`off`, `auto`, `on`). Cuando está `on`, evita la caché de páginas del kernel usando O_DIRECT para una mejor predictibilidad de I/O. Cuando está `auto`, intenta O_DIRECT y retrocede a I/O en búfer si no es compatible. Solo Linux; otras plataformas siempre usan I/O en búfer. |
| pidfile | | String | No | Ruta al archivo PID. Si no se especifica, se establecerá automáticamente a `unix_socket_dir/pgmoneta.<host>.pid` donde `<host>` es el valor del parámetro `host` u `all` si `host = *`.|
| update_process_title | `verbose` | String | No | El comportamiento para actualizar el título del proceso del sistema operativo. Las configuraciones permitidas son: `never` (u `off`), no actualiza el título del proceso; `strict` para establecer el título del proceso sin reemplazar la longitud del título del proceso inicial existente; `minimal` para establecer el título del proceso a la descripción base; `verbose` (o `full`) para establecer el título del proceso a la descripción completa. Tenga en cuenta que `strict` y `minimal` se honran solo en aquellos sistemas que no proporcionan una forma nativa de establecer el título del proceso (por ejemplo, Linux). En otros sistemas, no hay diferencia entre `strict` y `minimal` y el comportamiento asumido es `minimal` incluso si se usa `strict`. `never` y `verbose` siempre se honran en todos los sistemas. En sistemas Linux, el título del proceso siempre se trunca a 255 caracteres, mientras que en sistemas que proporcionan una forma nativa de establecer el título del proceso puede ser más largo. |

### Sección de servidor

**Servidor**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| host | | String | Sí | La dirección de la instancia PostgreSQL |
| port | | Int | Sí | El puerto de la instancia PostgreSQL |
| user | | String | Sí | El nombre del usuario de replicación |
| wal_slot | | String | Sí | El slot de replicación para WAL |

El `user` especificado debe tener la opción `REPLICATION` para poder transmitir el Write-Ahead Log (WAL), y debe
tener acceso a la base de datos `postgres` para obtener los parámetros de configuración necesarios.

**Gestión de slots**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| create_slot | no | Bool | No | Crear un slot de replicación para este servidor. Los valores válidos son: yes, no |

**Seguimiento**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| follow | | String | No | Hacer failover a este servidor si el servidor de seguimiento falla |

**Retención**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| retention | | Array | No | La retención para el servidor en días, semanas, meses, años |

**WAL shipping**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| wal_shipping | | String | No | El directorio de envío de WAL |

**Hot standby**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| hot_standby | | String | No | Directorio hot standby. Directorio único o directorios separados por comas hasta 8 (por ejemplo, /path/to/hot/standby1,/path/to/hot/standby2) |
| hot_standby_overrides | | String | No | Archivos a reemplazar en el directorio hot standby. Si se especifican múltiples hot standby entonces esta configuración se separa por \| |
| hot_standby_tablespaces | | String | No | Asignaciones de espacio de tabla para hot standby. La sintaxis es [from -> to,?]+. Si se especifican múltiples hot standby entonces esta configuración se separa por \| |

**Trabajadores**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| workers | -1 | Int | No | El número de trabajadores (workers) que cada proceso puede usar para su trabajo. Usa 0 para desactivar, -1 significa usar la configuración global. El máximo es el número de CPUs |

**Transport Level Security**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| tls_cert_file | | String | No | Archivo de certificado para TLS. Este archivo debe ser propiedad del usuario que ejecuta pgmoneta o root. |
| tls_key_file | | String | No | Archivo de clave privada para TLS. Este archivo debe ser propiedad del usuario que ejecuta pgmoneta o root. Además, los permisos deben ser al menos `0640` si es propiedad de root o `0600` en caso contrario. |
| tls_ca_file | | String | No | Archivo de Autoridad de Certificación (CA) para TLS. Este archivo debe ser propiedad del usuario que ejecuta pgmoneta o root.  |

**Miscelánea (Miscellaneos)**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| max_rate | -1 | Int | No | La velocidad máxima de transferencia de backup en bytes por segundo. Usa 0 para desactivar, -1 significa usar la configuración global |
| progress | -1 | Int | No | Habilitar seguimiento del progreso de backup. Usa 1 para habilitar, 0 para desactivar, -1 significa usar la configuración global |

**Extra**

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| extra | | String | No | El directorio fuente para recuperación en el lado del servidor (los detalles están en la sección extra)|

La configuración `extra` se establece en la sección del servidor. No es requerida, pero si configuras este parámetro, cuando realices un backup usando el CLI `pgmoneta-cli -c pgmoneta.conf backup primary`, también copiará todos los archivos especificados en el lado del servidor y los devolverá al lado del cliente.

Esta característica `extra` requiere que el lado del servidor instale la extensión [pgmoneta_ext](https://github.com/pgmoneta/pgmoneta_ext) y también haga que el usuario `repl` sea un `SUPERUSER` (esto mejorará en el futuro). Actualmente, esta característica está disponible solo para el rol `SUPERUSER`.

Puedes configurar `pgmoneta_ext` siguiendo el [README](https://github.com/pgmoneta/pgmoneta_ext/blob/main/README.md) para instalar fácilmente la extensión. También hay instrucciones más detalladas disponibles en la documentación [DEVELOPERS](https://github.com/pgmoneta/pgmoneta_ext/blob/main/doc/DEVELOPERS.md).

El formato para el parámetro `extra` es una ruta a un archivo o directorio. Puedes incluir más de un archivo o directorio separados por comas. El formato es el siguiente:

```ini
extra = /tmp/myfile1, /tmp/myfile2, /tmp/mydir1, /tmp/mydir2
```

## pgmoneta_users.conf

La configuración `pgmoneta_users` define los usuarios conocidos del sistema. Este archivo se crea y se gestiona a través
de la herramienta `pgmoneta-admin`.

La configuración se carga desde la ruta especificada por la bandera `-u` o desde `/etc/pgmoneta/pgmoneta_users.conf`.

## pgmoneta_admins.conf

La configuración `pgmoneta_admins` define los administradores conocidos del sistema. Este archivo se crea y se gestiona a través
de la herramienta `pgmoneta-admin`.

La configuración se carga desde la ruta especificada por la bandera `-A` o desde `/etc/pgmoneta/pgmoneta_admins.conf`.

Si pgmoneta tiene tanto Transport Layer Security (TLS) como `management` habilitados, entonces `pgmoneta-cli` puede
conectar con TLS usando los archivos `~/.pgmoneta/pgmoneta.key` (debe tener permiso 0600),
`~/.pgmoneta/pgmoneta.crt` y `~/.pgmoneta/root.crt`.

## pgmoneta_cli.conf

La configuración `pgmoneta_cli` define valores predeterminados para el cliente `pgmoneta-cli`. Se carga desde la ruta pasada con `-c` o desde `/etc/pgmoneta/pgmoneta_cli.conf` si no se proporciona `-c`. Las banderas de línea de comandos reemplazan valores en este archivo.

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| host |  | String | No | Host de gestión para conectar. Si se omite, `unix_socket_dir` puede usarse para una conexión socket Unix local. |
| port | 0 | Int | No | Puerto de gestión para conectar. Requerido para conexiones TCP remotas a menos que se use un socket Unix. |
| unix_socket_dir |  | String | No | Directorio que contiene el Socket de Dominio Unix de pgmoneta. Habilita la gestión local sin host/port. Puede interpolar variables de entorno (por ejemplo, `$HOME`). |
| compression | none | String | No | Compresión de protocolo de cable (`none`, `gzip`, `zstd`, `lz4`, `bzip2`). Se aplica solo al tráfico CLI<->servidor. |
| encryption | none | String | No | Encriptación de protocolo de cable (`none`, `aes256`, `aes192`, `aes128`). Se aplica solo al tráfico CLI<->servidor. |
| output | text | String | No | Formato de salida predeterminado del CLI (`text`, `json`, `raw`). |
| log_type | console | String | No | Tipo de registro para el CLI (`console`, `file`, `syslog`). |
| log_level | info | String | No | Nivel de registro (`fatal`, `error`, `warn`, `info`, `debug`/`debug1`-`debug5`). |
| log_path | pgmoneta-cli.log | String | No | Ruta del archivo de registro cuando `log_type = file`. Puede interpolar variables de entorno (por ejemplo, `$HOME`). |
| log_mode | append | String | No | Modo de archivo de registro (`append`, `create`). |
| log_rotation_age | 0 | String | No | Rotación basada en tiempo. `0` desactiva. Soporta sufijos `S`, `M`, `H`, `D`, `W` (segundos por defecto). |
| log_rotation_size | 0 | String | No | Rotación basada en tamaño. `0` desactiva. Soporta `B` (por defecto), `K/KB`, `M/MB`, `G/GB`. |
| log_line_prefix | %Y-%m-%d %H:%M:%S | String | No | Prefijo formato strftime(3) para líneas de registro. |


## Directorio de configuración

Puedes especificar un directorio para todos los archivos de configuración usando la bandera `-D` (o `--directory`).
Alternativamente, puedes establecer la variable de entorno `PGMONETA_CONFIG_DIR` para definir el directorio de configuración.

**Comportamiento:**
- Cuando se establece la flag de directorio (`-D`), pgmoneta buscará todos los archivos de configuración en el directorio especificado.
- Si no se encuentra un archivo requerido en el directorio especificado, pgmoneta lo buscará en su ubicación predeterminada (por ejemplo, `/etc/pgmoneta/pgmoneta.conf`).
- Si no se encuentra el archivo en ninguna ubicación:
  - Si el archivo es obligatorio, pgmoneta registrará un error y fallará al iniciar.
  - Si el archivo es opcional, pgmoneta registrará una advertencia y continuará sin él.
- Todos los intentos de búsqueda de archivos y archivos faltantes se registran para solucionar problemas.

**Reglas de precedencia:**
- Las flags de archivo individual (como `-c`, `-u`, `-A`, etc.) siempre tienen precedencia sobre la flag de directorio y la variable de entorno para sus archivos respectivos.
- La flag de directorio (`-D`) tiene precedencia sobre la variable de entorno (`PGMONETA_CONFIG_DIR`).
- Si ni la flag de directorio ni las flags de archivo individual se establecen, pgmoneta usa las ubicaciones predeterminadas para todos los archivos de configuración.

**Usando la variable de entorno:**
1. Establece la variable de entorno antes de iniciar pgmoneta:
```
export PGMONETA_CONFIG_DIR=/path/to/config_dir
pgmoneta -d
```
2. Si se establecen tanto la variable de entorno como la flag `-D`, la flag tiene precedencia.

**Ejemplo:**
```
pgmoneta -D /custom/config/dir -d
```
o
```
export PGMONETA_CONFIG_DIR=/custom/config/dir
pgmoneta -d
```

Consulta los logs para detalles sobre qué archivos de configuración se cargaron y desde qué ubicaciones.
