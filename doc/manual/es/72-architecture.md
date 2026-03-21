## Arquitectura

### Descripción general

[**pgmoneta**][pgmoneta] utiliza un modelo de proceso (`fork()`), donde cada proceso maneja un receptor de Write-Ahead Log (WAL) para [PostgreSQL][postgresql].

El proceso principal se define en [main.c][main_c].

La copia de seguridad se maneja en [backup.h][backup_h] ([backup.c][backup_c]).

La restauración se maneja en [restore.h][restore_h] ([restore.c][restore_c]) con el linking manejado en [link.h][link_h] ([link.c][link_c]).

La gestión de Archive se realiza en [archv.h][achv_h] ([archive.c][archive_c]) con el apoyo de restore.

Write-Ahead Log se maneja en [wal.h][wal_h] ([wal.c][wal_c]).

La información de copia de seguridad se maneja en [info.h][info_h] ([info.c][info_c]).

La retención se maneja en [retention.h][retention_h] ([retention.c][retention_c]).

La compresión se maneja en [gzip_compression.h][gzip_compression.h] ([gzip_compression.c][gzip_compression.c]),
[lz4_compression.h][lz4_compression.h] ([lz4_compression.c][lz4_compression.c]),
[zstandard_compression.h][zstandard_compression.h] ([zstandard_compression.c][zstandard_compression.c]),
y [bzip2_compression.h][bzip2_compression.h] ([bzip2_compression.c][bzip2_compression.c]).

El cifrado se maneja en [aes.h][aes.h] ([aes.c][aes.c]).

### Memoria compartida

Un segmento de memoria ([shmem.h][shmem_h]) se comparte entre todos los procesos que contienen el estado [**pgmoneta**][pgmoneta] que contiene la configuración y la lista de servidores.

La configuración de [**pgmoneta**][pgmoneta] (`struct configuration`) y la configuración de los servidores (`struct server`) se inicializan en este segmento de memoria compartida. Estas estructuras se definen en [pgmoneta.h][pgmoneta_h].

El segmento de memoria compartida se crea usando la llamada `mmap()`.

### Red y mensajes

Toda la comunicación se abstrae usando el tipo de dato `struct message` definido en [message.h][messge_h].

La lectura y escritura de mensajes se manejan en los archivos [message.h][messge_h] ([message.c][message_c]).

Las operaciones de red se definen en [network.h][network_h] ([network.c][network_c]).

### Memoria

Cada proceso utiliza un bloque de memoria fijo para su comunicación de red, que se asigna al inicio del proceso.

De esta forma, no tenemos que asignar memoria para cada mensaje de red, y lo que es más importante, liberarla después del uso.

La interfaz de memoria se define en [memory.h][memory_h] ([memory.c][memory_c]).

### Gestión

[**pgmoneta**][pgmoneta] tiene una interfaz de gestión que define las capacidades del administrador que se pueden realizar cuando se ejecuta.
Esto incluye, por ejemplo, hacer una copia de seguridad. El programa `pgmoneta-cli` se utiliza para estas operaciones ([cli.c][cli_c]).

La interfaz de gestión se define en [management.h][management_h]. La interfaz de gestión utiliza su propio protocolo que utiliza JSON como base.

**Escritura**

El cliente envía una única cadena JSON al servidor,

| Campo         | Tipo   | Descripción                     |
| :------------ | :----- | :------------------------------ |
| `compression` | uint8  | El tipo de compresión           |
| `encryption`  | uint8  | El tipo de cifrado              |
| `length`      | uint32 | La longitud del documento JSON  |
| `json`        | String | El documento JSON               |

El servidor envía una única cadena JSON al cliente,

| Campo         | Tipo   | Descripción                     |
| :------------ | :----- | :------------------------------ |
| `compression` | uint8  | El tipo de compresión           |
| `encryption`  | uint8  | El tipo de cifrado              |
| `length`      | uint32 | La longitud del documento JSON  |
| `json`        | String | El documento JSON               |

**Lectura**

El servidor envía una única cadena JSON al cliente,

| Campo         | Tipo   | Descripción                     |
| :------------ | :----- | :------------------------------ |
| `compression` | uint8  | El tipo de compresión           |
| `encryption`  | uint8  | El tipo de cifrado              |
| `length`      | uint32 | La longitud del documento JSON  |
| `json`        | String | El documento JSON               |

El cliente envía al servidor un único documento JSON,

| Campo         | Tipo   | Descripción                     |
| :------------ | :----- | :------------------------------ |
| `compression` | uint8  | El tipo de compresión           |
| `encryption`  | uint8  | El tipo de cifrado              |
| `length`      | uint32 | La longitud del documento JSON  |
| `json`        | String | El documento JSON               |

**Gestión remota**

La funcionalidad de gestión remota utiliza el mismo protocolo que el método de gestión estándar.

Sin embargo, antes de enviar el paquete de gestión, el cliente debe autenticarse usando SCRAM-SHA-256 using el
mismo formato de mensaje que utiliza PostgreSQL, como StartupMessage, AuthenticationSASL, AuthenticationSASLContinue,
AuthenticationSASLFinal y AuthenticationOk. El mensaje SSLRequest es soportado.

La interfaz de gestión remota se define en [remote.h][remote_h] ([remote.c][remote_c]).

### Uso de libev

[libev][libev] se utiliza para manejar interacciones de red, que se "activa" en un evento `EV_READ`.

Cada proceso tiene su propio bucle de eventos, de modo que el proceso solo recibe notificaciones cuando los datos relacionados solo con ese proceso están listos. El bucle principal maneja los "servicios" de todo el sistema como verificaciones de tiempo de inactividad y así sucesivamente.

### Señales

El proceso principal de [**pgmoneta**][pgmoneta] soporta las siguientes señales `SIGTERM`, `SIGINT` y `SIGALRM` como mecanismo para apagar. El `SIGABRT` se usa para solicitar un volcado de núcleo (`abort()`).

La señal `SIGHUP` activará una recarga de la configuración.

No debería ser necesario usar `SIGKILL` para [**pgmoneta**][pgmoneta]. Por favor, considera usar `SIGABRT` en su lugar, y comparte el volcado de núcleo y los registros de depuración con la comunidad [**pgmoneta**][pgmoneta].

### Recarga

La señal `SIGHUP` activará una recarga de la configuración.

Sin embargo, algunos parámetros de configuración requieren un reinicio completo de [**pgmoneta**][pgmoneta] para tomar efecto. Estos son

* `hugepage`
* `direct_io`
* `libev`
* `log_path`
* `log_type`
* `unix_socket_dir`
* `pidfile`

La configuración también se puede recargar usando `pgmoneta-cli -c pgmoneta.conf conf reload`. El comando solo se soporta sobre la interfaz local, por lo que no funciona remotamente.

La señal `SIGHUP` activará una recarga completa de la configuración. Cuando se recibe `SIGHUP`, [**pgmoneta**](https://github.com/pgmoneta/pgmoneta) releerá la configuración desde los archivos de configuración en el disco y aplicará los cambios que se puedan manejar en tiempo de ejecución. Esta es la forma estándar de aplicar cambios realizados a los archivos de configuración.

En contraste, la señal `SIGUSR1` activará una recarga de servicio, pero **no** releerá los archivos de configuración. En su lugar, `SIGUSR1` reinicia servicios específicos (métricas y gestión) usando la configuración actual en memoria. Esta señal se activa automáticamente por `pgmoneta-cli conf set` al aplicar cambios de configuración en tiempo de ejecución que no requieren un reinicio completo. Los cambios realizados a los archivos de configuración **no** se recogerán cuando se use `SIGUSR1`; solo se utilizará la configuración ya cargada en memoria.

**Servicios afectados por SIGUSR1:**
- **Servicio de métricas**: Se reinicia cuando el puerto de `metrics` cambia o se habilita/deshabilita
- **Servicio de gestión**: Se reinicia cuando el puerto de `management` cambia o se habilita/deshabilita

### Prometheus

pgmoneta tiene soporte para [Prometheus][prometheus] cuando se especifica el puerto de `metrics`.

El módulo sirve dos endpoints

* `/` - Descripción general de la funcionalidad (`text/html`)
* `/metrics` - Las métricas (`text/plain`)

Todas las demás URL resultarán en una respuesta 403.

El endpoint de métricas soporta `Transfer-Encoding: chunked` para dar cuenta de una gran cantidad de datos.

La implementación se realiza en [prometheus.h][prometheus_h] y
[prometheus.c][prometheus_c].

### Registro

Implementación de registro simple basada en un bloqueo `atomic_schar`.

La implementación se realiza en [logging.h][logging_h] y [logging.c][logging_c].

| Nivel | Descripción |
| :------- | :------ |
| TRACE | Información para desarrolladores incluyendo valores de variables |
| DEBUG | Información de nivel superior para desarrolladores - típicamente sobre control de flujo y el valor de variables clave |
| INFO | Un comando de usuario fue exitoso o información general sobre la salud del sistema |
| WARN | Un comando de usuario no se completó correctamente, por lo que se necesita atención |
| ERROR | Algo inesperado sucedió - intenta dar información para ayudar a identificar el problema |
| FATAL | No podemos recuperarnos - mostramos tanta información como podemos sobre el problema y `exit(1)` |

### Protocolo

Las interacciones del protocolo se pueden depurar usando [Wireshark][wireshark] o [pgprtdbg][pgprtdbg].
