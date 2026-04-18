\newpage

# Generador de configuración

`pgmoneta-config` es una herramienta de línea de comandos independiente para generar y gestionar
archivos de configuración de pgmoneta. Funciona directamente sobre el archivo de configuración
sin necesitar una instancia de pgmoneta en ejecución.

## Uso

``` sh
pgmoneta-config
  Utilidad de configuración para pgmoneta

Uso:
  pgmoneta-config [ -o FILE ] [ COMMAND ]

Opciones:
  -o, --output FILE   Establece la ruta del archivo de salida para el comando init (predeterminado: ./pgmoneta.conf)
  -q, --quiet         Genera opciones predeterminadas sin preguntas (para init)
  -F, --force         Forzar sobreescritura si el archivo de salida ya existe
  -V, --version       Mostrar información de versión
  -?, --help          Mostrar ayuda

Comandos:
  init                              Genera un archivo pgmoneta.conf de forma interactiva
  get  <file> <section> <key>       Obtiene un valor de configuración
  set  <file> <section> <key> <val> Establece un valor de configuración
  del  <file> <section> [key]       Elimina una sección o una clave
  ls   <file> [section]             Lista secciones o claves
```

## init

Genera un nuevo archivo de configuración `pgmoneta.conf` de forma interactiva. Se solicitarán al
usuario los valores necesarios, como el host, el directorio base, la compresión, el tipo de registro
y los datos de conexión al servidor PostgreSQL.

Comando

``` sh
pgmoneta-config init
```

### Modo silencioso

Use la opción `-q` para generar una plantilla con los valores predeterminados sin necesidad de responder preguntas.

``` sh
pgmoneta-config -q init
```

### Ruta de salida

Use `-o` para especificar la ruta del archivo de salida.

``` sh
pgmoneta-config -o /etc/pgmoneta/pgmoneta.conf init
```

### Forzar sobreescritura

Si el archivo de salida ya existe, use `-F` para forzar su sobreescritura.

``` sh
pgmoneta-config -q -F -o /etc/pgmoneta/pgmoneta.conf init
```

### Archivo generado

El archivo de configuración generado incluye una sección `[pgmoneta]` con los parámetros
principales y una o más secciones de servidor con los datos de conexión a PostgreSQL.

Ejemplo de salida

``` ini
[pgmoneta]
host = *
metrics = 5001

base_dir = /home/pgmoneta/backup

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

## get

Obtiene un valor de configuración de un archivo.

Comando

``` sh
pgmoneta-config get <file> <section> <key>
```

Ejemplo

``` sh
pgmoneta-config get pgmoneta.conf pgmoneta compression
```

## set

Establece un valor de configuración en el archivo indicado. Si la sección o la clave no existen,
las crea. Conserva los comentarios y el formato originales del archivo.

Comando

``` sh
pgmoneta-config set <file> <section> <key> <value>
```

Ejemplo

``` sh
pgmoneta-config set pgmoneta.conf pgmoneta compression lz4
```

Para agregar una nueva sección de servidor

``` sh
pgmoneta-config set pgmoneta.conf replica host 192.168.1.10
pgmoneta-config set pgmoneta.conf replica port 5432
pgmoneta-config set pgmoneta.conf replica user repl
```

## del

Elimina una clave específica de una sección, o elimina una sección completa.

Comando

``` sh
pgmoneta-config del <file> <section> [key]
```

Eliminar una clave

``` sh
pgmoneta-config del pgmoneta.conf pgmoneta compression
```

Eliminar una sección completa

``` sh
pgmoneta-config del pgmoneta.conf replica
```

## ls

Enumera todas las secciones de un archivo de configuración o todas las claves de una sección determinada.

Comando

``` sh
pgmoneta-config ls <file> [section]
```

Listar todas las secciones

``` sh
pgmoneta-config ls pgmoneta.conf
```

Listar claves en una sección

``` sh
pgmoneta-config ls pgmoneta.conf pgmoneta
```

## Características de seguridad

### Escrituras atómicas

Todas las operaciones de escritura emplean sustitución atómica de archivos. Los cambios se
escriben primero en un archivo temporal, se sincronizan con el disco mediante `fsync` y,
a continuación, el archivo temporal se renombra atómicamente sobre el destino.

### Permisos de archivo

Los archivos de configuración generados o modificados se crean con permisos `0600` para
proteger las credenciales de acceso.

### Comparación con pgmoneta-cli conf

`pgmoneta-cli conf set/get` administra la configuración en tiempo de ejecución de un proceso
pgmoneta activo a través del socket de administración.

`pgmoneta-config set/get` gestiona el archivo de configuración en disco sin necesidad de que
el proceso esté en ejecución. Resulta útil para la configuración inicial, el aprovisionamiento
automatizado y la edición fuera de línea.
