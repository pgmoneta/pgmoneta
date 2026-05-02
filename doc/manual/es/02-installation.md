\newpage

# Instalación

## Rocky Linux 9.x / 10.x

Puedes descargar la distribución [Rocky Linux](https://www.rockylinux.org/) desde su sitio web

```
https://rockylinux.org/download
```

La instalación y configuración están fuera del alcance de esta guía.

Idealmente, usarías cuentas de usuario dedicadas para ejecutar [**PostgreSQL**][postgresql] y [**pgmoneta**][pgmoneta]

```
useradd postgres
usermod -a -G wheel postgres
useradd pgmoneta
usermod -a -G wheel pgmoneta
```

Agrega un directorio de configuración para [**pgmoneta**][pgmoneta]

```
mkdir /etc/pgmoneta
chown -R pgmoneta:pgmoneta /etc/pgmoneta
```

y abrimos los puertos en el firewall que necesitaremos

```
firewall-cmd --permanent --zone=public --add-port=5001/tcp
firewall-cmd --permanent --zone=public --add-port=5002/tcp
```

## PostgreSQL 17

Instalaremos PostgreSQL 17 desde el [repositorio YUM][yum] oficial con los binarios comunitarios,

**x86_64**

```
dnf -qy module disable postgresql
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
```

**aarch64**

```
dnf -qy module disable postgresql
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-aarch64/pgdg-redhat-repo-latest.noarch.rpm
```

e instala mediante:

```
dnf install -y postgresql17 postgresql17-server postgresql17-contrib
```

Primero, actualizaremos `~/.bashrc` con

```
cat >> ~/.bashrc
export PGHOST=/tmp
export PATH=/usr/pgsql-17/bin/:$PATH
```

luego presiona Ctrl-d para guardar, y

```
source ~/.bashrc
```

para recargar el entorno de Bash.

Luego podemos hacer la inicialización de PostgreSQL

```
mkdir DB
initdb -k DB
```

y actualizar la configuración - para una máquina con 8 GB de memoria.

**postgresql.conf**
```
listen_addresses = '*'
port = 5432
max_connections = 100
unix_socket_directories = '/tmp'
password_encryption = scram-sha-256
shared_buffers = 2GB
huge_pages = try
max_prepared_transactions = 100
work_mem = 16MB
dynamic_shared_memory_type = posix
wal_level = replica
wal_log_hints = on
max_wal_size = 16GB
min_wal_size = 2GB
log_destination = 'stderr'
logging_collector = on
log_directory = 'log'
log_filename = 'postgresql.log'
log_rotation_age = 0
log_rotation_size = 0
log_truncate_on_rotation = on
log_line_prefix = '%p [%m] [%x] '
log_timezone = UTC
datestyle = 'iso, mdy'
timezone = UTC
lc_messages = 'en_US.UTF-8'
lc_monetary = 'en_US.UTF-8'
lc_numeric = 'en_US.UTF-8'
lc_time = 'en_US.UTF-8'
```

**pg_hba.conf**
```
local   all           all                   trust
host    postgres      repl   127.0.0.1/32   scram-sha-256
host    postgres      repl   ::1/128        scram-sha-256
host    replication   repl   127.0.0.1/32   scram-sha-256
host    replication   repl   ::1/128        scram-sha-256
```

Por favor, verifica con otras fuentes para crear una configuración apropiada para tu entorno local.

Ahora estamos listos para iniciar PostgreSQL

```
pg_ctl -D DB -l /tmp/ start
```

Conéctate, agrega el usuario de replicación y crea el slot de WAL (Write-Ahead Log) que necesitas para [**pgmoneta**][pgmoneta]

```
psql postgres
CREATE ROLE repl WITH LOGIN REPLICATION PASSWORD 'repl';
SELECT pg_create_physical_replication_slot('repl', true, false);
\q
```

### Soporte de Transport Level Security

Crea la clave del cliente
```
openssl ecparam -name prime256v1 -genkey -noout -out client.key
```

Crea la solicitud del cliente - recuerda que el `CN` debe tener el nombre del usuario de replicación
```
openssl req -new -sha256 -key client.key -out client.csr -subj "/CN=repl"
```

Genera el certificado del cliente
```
openssl x509 -req -in client.csr -CA root.crt -CAkey server.key -CAcreateserial -out client.crt -days 3650 -sha256
```

Puedes probar tu configuración copiando los archivos al directorio predeterminado del cliente de PostgreSQL, por ejemplo:

```
mkdir ~/.postgresql
cp client.crt ~/.postgresql/postgresql.crt
cp client.key ~/.postgresql/postgresql.key
cp root.crt ~/.postgresql/ca.crt
chmod 0600 ~/.postgresql/postgresql.crt ~/.postgresql/postgresql.key ~/.postgresql/ca.crt
```

y luego prueba con el comando `psql`.

En `pg_hba.conf` cambia

```
hostssl    all           all           0.0.0.0/0          scram-sha-256
```

a

```
hostssl    all           all           0.0.0.0/0          scram-sha-256 clientcert=verify-ca
```

Más información

* [Secure TCP/IP Connections with SSL](https://www.postgresql.org/docs/12/ssl-tcp.html)
* [The pg_hba.conf File](https://www.postgresql.org/docs/17/auth-pg-hba-conf.html)

## pgmoneta

Instalaremos [**pgmoneta**][pgmoneta] desde el [repositorio YUM][yum] oficial también,

```
dnf install -y pgmoneta
```

Primero, necesitaremos crear una clave de seguridad maestra para la instalación de [**pgmoneta**][pgmoneta], mediante

```
pgmoneta-admin -g master-key
```

Por defecto, esto solicitará una clave de forma interactiva. Alternativamente, se puede proporcionar una clave usando el argumento de línea de comandos `--password` o la variable de entorno `PGMONETA_PASSWORD`. Ten en cuenta que pasar la clave usando la línea de comandos podría no ser seguro.

Luego crearemos la configuración para [**pgmoneta**][pgmoneta],

```
cat > /etc/pgmoneta/pgmoneta.conf
[pgmoneta]
host = *
metrics = 5001
management = 0

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

y termina con Ctrl-d para guardar el archivo.

Luego, crearemos la configuración del usuario,

```
pgmoneta-admin -f /etc/pgmoneta/pgmoneta_users.conf -U repl -P repl user add
```

Creemos el directorio base e iniciemos [**pgmoneta**][pgmoneta] ahora, mediante

```
mkdir backup
pgmoneta -d
```
