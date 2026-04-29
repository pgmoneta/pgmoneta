\newpage

# Inicio rápido

Asegúrate de que [**pgmoneta**][pgmoneta] esté instalado y en tu ruta usando `pgmoneta -?`. Deberías ver

``` console
pgmoneta 0.21.1
  Backup / restore solution for PostgreSQL

Usage:
  pgmoneta [ -c CONFIG_FILE ] [ -u USERS_FILE ] [ -A ADMINS_FILE ] [ -D DIRECTORY ] [ -d ]

Options:
  -c, --config CONFIG_FILE  Set the path to the pgmoneta.conf file
  -u, --users USERS_FILE    Set the path to the pgmoneta_users.conf file
  -A, --admins ADMINS_FILE  Set the path to the pgmoneta_admins.conf file
  -D, --directory DIRECTORY Set the directory containing all configuration files
                            Can also be set via PGMONETA_CONFIGURATION_PATH environment variable
  -d, --daemon              Run as a daemon
  -V, --version             Display version information
  -?, --help                Display help

pgmoneta: https://pgmoneta.github.io/
Report bugs: https://github.com/pgmoneta/pgmoneta/issues
```

Si encuentras problemas siguiendo los pasos anteriores, puedes consultar el capítulo **Instalación** para ver cómo instalar o compilar pgmoneta en tu sistema.

## Configuración

Creemos un archivo de configuración simple llamado `pgmoneta.conf` con el contenido

``` ini
[pgmoneta]
host = *
metrics = 5001

base_dir = /home/pgmoneta

compression = zstd

retention = 7

log_type = file
log_level = info
log_path = /tmp/pgmoneta.log

unix_socket_dir = /tmp/

[primary]
host = localhost
port = 5432
user = repl
wal_slot = repl
```

En nuestra sección principal llamada `[pgmoneta]` configuramos [**pgmoneta**][pgmoneta] para escuchar en todas las direcciones de red. Habilitaremos métricas de Prometheus en el puerto 5001 y los backups estarán en el directorio `/home/pgmoneta`. Todos los backups se comprimen con zstd y se mantienen durante 7 días. El registro se realizará en nivel `info` y se guardará en un archivo llamado `/tmp/pgmoneta.log`. Finalmente especificamos la ubicación de `unix_socket_dir` usado para operaciones de gestión y la ruta de las herramientas de línea de comandos de PostgreSQL.

A continuación creamos una sección llamada `[primary]` que contiene la información sobre nuestra instancia de [PostgreSQL][postgresql]. En este caso se está ejecutando en `localhost` en el puerto `5432` y utilizaremos la cuenta de usuario `repl` para conectar, y el slot de Write-Ahead también se llamará `repl`.

El usuario `repl` debe tener el rol `REPLICATION` y acceso a la base de datos `postgres`, así por ejemplo

``` sh
CREATE ROLE repl WITH LOGIN REPLICATION PASSWORD 'secretpassword';
```

y en `pg_hba.conf`

``` ini
local   postgres       repl                     scram-sha-256
host    postgres       repl    127.0.0.1/32     scram-sha-256
host    postgres       repl    ::1/128          scram-sha-256
host    replication    repl    127.0.0.1/32     scram-sha-256
host    replication    repl    ::1/128          scram-sha-256
```

El tipo de autenticación debe basarse en el valor `password_encryption` de `postgresql.conf`.

Luego crea un slot de replicación física que se usará para la transmisión de WAL, como:

``` sh
SELECT pg_create_physical_replication_slot('repl', true, false);
```

Alternativamente, configura la creación automática de slots agregando `create_slot = yes` a `[pgmoneta]` o la sección correspondiente del servidor.

Necesitaremos un almacén de usuarios para la cuenta `repl`. Los siguientes comandos agregarán una clave maestra y la contraseña `repl`. La clave maestra debe tener más de 8 caracteres.

``` sh
pgmoneta-admin master-key
pgmoneta-admin -f pgmoneta_users.conf user add
```

Para uso en scripts, puedes proporcionar la clave maestra y la contraseña del usuario usando la variable de entorno `PGMONETA_PASSWORD`.

Ahora estamos listos para ejecutar [**pgmoneta**][pgmoneta].

Consulta el capítulo **Configuración** para todas las opciones de configuración.

## Ejecución

Ejecutaremos [**pgmoneta**][pgmoneta] usando el comando

``` sh
pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
```

Si esto no da un error, entonces estamos listos para hacer backups.

[**pgmoneta**][pgmoneta] se detiene presionando Ctrl-C (`^C`) en la consola donde lo iniciaste, o enviando la señal `SIGTERM` al proceso usando `kill <pid>`.

## Administración en tiempo de ejecución

[**pgmoneta**][pgmoneta] tiene una herramienta de administración en tiempo de ejecución llamada `pgmoneta-cli`.

Puedes ver los comandos que soporta usando `pgmoneta-cli -?` que te dará

``` console
pgmoneta-cli 0.21.1
  Command line utility for pgmoneta

Usage:
  pgmoneta-cli [ -c CONFIG_FILE ] [ COMMAND ]

Options:
  -c, --config CONFIG_FILE                       Set the path to the pgmoneta_cli.conf file
  -h, --host HOST                                Set the host name
  -p, --port PORT                                Set the port number
  -U, --user USERNAME                            Set the user name
  -P, --password PASSWORD                        Set the password
  -L, --logfile FILE                             Set the log file
  -v, --verbose                                  Output text string of result
  -V, --version                                  Display version information
  -F, --format text|json|raw                     Set the output format
  -C, --compress none|gz|zstd|lz4|bz2            Compress the wire protocol
  -E, --encrypt none|aes|aes256|aes192|aes128    Encrypt the wire protocol
  -s, --sort asc|desc                            Sort result (for list-backup)
      --cascade                                  Cascade a retain/expunge backup
  -?, --help                                     Display help

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
  decompress               Decompress a file using configured method
  decrypt                  Decrypt a file using master-key
  delete                   Delete a backup from a server
  encrypt                  Encrypt a file using master-key
  expunge                  Expunge a backup from a server
  info                     Information about a backup
  list-backup              List the backups for a server
  mode                     Switch the mode for a server
  ping                     Check if pgmoneta is alive
  restore                  Restore a backup from a server
  retain                   Retain a backup from a server
  shutdown                 Shutdown pgmoneta
  status [details]         Status of pgmoneta, with optional details
  verify                   Verify a backup from a server

pgmoneta: https://pgmoneta.github.io/
Report bugs: https://github.com/pgmoneta/pgmoneta/issues
```

Esta herramienta se puede usar en la máquina que ejecuta [**pgmoneta**][pgmoneta] para hacer un backup, por ejemplo:

``` sh
pgmoneta-cli -c pgmoneta.conf backup primary
```

Una restauración sería

``` sh
pgmoneta-cli -c pgmoneta.conf restore primary <timestamp> /path/to/restore
```

Para apagar pgmoneta usarías

``` sh
pgmoneta-cli -c pgmoneta.conf shutdown
```

Verifica el resultado de las operaciones comprobando el código de salida, por ejemplo:

``` sh
echo $?
```

o usando la flag `-v`.

Si pgmoneta tiene tanto Transport Layer Security (TLS) como `management` habilitados, entonces `pgmoneta-cli` puede conectar con TLS usando los archivos `~/.pgmoneta/pgmoneta.key` (debe tener permiso 0600), `~/.pgmoneta/pgmoneta.crt` y `~/.pgmoneta/root.crt`.

## Administración

[**pgmoneta**][pgmoneta] tiene una herramienta de administración llamada `pgmoneta-admin`, que se utiliza para controlar el registro de usuarios con [**pgmoneta**][pgmoneta].

Puedes ver los comandos que soporta usando `pgmoneta-admin -?` que te dará

``` console
pgmoneta-admin 0.21.1
  Administration utility for pgmoneta

Usage:
  pgmoneta-admin [ -f FILE ] [ COMMAND ]

Options:
  -f, --file FILE         Set the path to a user file
  -U, --user USER         Set the user name
  -P, --password PASSWORD Set the password for the user
  -g, --generate          Generate a password
  -l, --length            Password length
  -V, --version           Display version information
  -?, --help              Display help

Commands:
  master-key              Create or update the master key
  user <subcommand>       Manage a specific user, where <subcommand> can be
                          - add  to add a new user
                          - del  to remove an existing user
                          - edit to change the password for an existing user
                          - ls   to list all available users
```

Para establecer la clave maestra para todos los usuarios puedes usar

``` sh
pgmoneta-admin -g master-key
```

La clave maestra debe tener al menos 8 caracteres.

Luego usa los otros comandos para agregar, actualizar, eliminar u listar los nombres de usuario actuales, por ejemplo:

``` sh
pgmoneta-admin -f pgmoneta_users.conf user add
```

## Próximos pasos

Los próximos pasos para mejorar la configuración de pgmoneta podrían ser

* Leer el manual
* Actualizar `pgmoneta.conf` con la configuración requerida para tu sistema
* Habilitar Transport Layer Security v1.2+ (TLS) para acceso de administrador

Consulta [Configuración][configuration] para más información sobre estos temas.

## Cierre

La comunidad de [pgmoneta](https://github.com/pgmoneta/pgmoneta) espera que encuentres
el proyecto interesante.

Siéntete libre de

* [Hacer una pregunta](https://github.com/pgmoneta/pgmoneta/discussions)
* [Reportar un problema](https://github.com/pgmoneta/pgmoneta/issues)
* [Enviar una solicitud de una feature](https://github.com/pgmoneta/pgmoneta/issues)
* [Escribir y enviar una contribución de código](https://github.com/pgmoneta/pgmoneta/pulls)

¡Todas las contribuciones son bienvenidas!

Por favor, consulta nuestras políticas de [Código de conducta](../CODE_OF_CONDUCT.md) para interactuar en nuestra
comunidad.

Considera darle al proyecto una [estrella](https://github.com/pgmoneta/pgmoneta/stargazers) en
[GitHub](https://github.com/pgmoneta/pgmoneta/) si te resulta útil. Y siéntete libre de seguir
el proyecto en [X](https://x.com/pgmoneta/) también.
