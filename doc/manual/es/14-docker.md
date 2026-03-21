\newpage

# Docker

Puedes ejecutar [**pgmoneta**][pgmoneta] usando Docker en lugar de compilarlo manualmente.

## Requisitos previos

* [**Docker**][docker] o [**Podman**][podman] deben estar instalados en el servidor donde PostgreSQL se está ejecutando.
* Asegúrate de que PostgreSQL esté configurado para permitir conexiones externas.

## La imagen

**Habilitar acceso externo a PostgreSQL**

Modifica el archivo `postgresql.conf` del servidor PostgreSQL local para permitir conexiones desde el exterior:
```ini
listen_addresses = '*'
```

Actualiza `pg_hba.conf` para permitir conexiones remotas:
```ini
host    all    all    0.0.0.0/0    scram-sha-256
```

Luego, reinicia PostgreSQL para que los cambios tengan efecto:
```sh
sudo systemctl restart postgresql
```

**Clonar el repositorio**
```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
```

**Crear la imagen de Docker**

Hay dos Dockerfiles disponibles:
1. **Imagen basada en Alpine**

**Usando Docker**
```sh
docker build -t pgmoneta:latest -f ./contrib/docker/Dockerfile.alpine .
```

**Usando Podman**

```sh
podman build -t pgmoneta:latest -f ./contrib/docker/Dockerfile.alpine .
```

**Imagen basada en Rocky Linux 9**

**Usando Docker**
```sh
docker build -t pgmoneta:latest -f ./contrib/docker/Dockerfile.rocky9 .
```

**Usando Podman**

```sh
podman build -t pgmoneta:latest -f ./contrib/docker/Dockerfile.rocky9 .
```

**Ejecutar pgmoneta como un contenedor de Docker**

Una vez que la imagen fue construida, ejecuta el contenedor usando:

Usando Docker

```sh
docker run -d --name pgmoneta -p 5001:5001 pgmoneta:latest
```

Usando Podman

```sh
podman run -d --name pgmoneta --network host pgmoneta:latest
```

**Verificar el contenedor**

Verifica si el contenedor se está ejecutando:

Usando Docker

```sh
docker ps | grep pgmoneta
```

Usando Podman

```sh
podman ps | grep pgmoneta
```

Verifica los logs para cualquier error:

Usando Docker

```sh
docker logs pgmoneta
```

Usando Podman

```sh
podman logs pgmoneta
```

También puedes inspeccionar las métricas expuestas en:
```
http://localhost:5001/metrics
```

Puedes detener el contenedor usando

Usando Docker

```sh
docker stop pgmoneta
```

Usando Podman

```sh
podman stop pgmoneta
```

Puedes ejecutar un comando en el contenedor y ejecutar comandos cli como

```sh
docker exec -it pgmoneta /bin/bash
#o usando podman
podman exec -it pgmoneta /bin/bash

cd /etc/pgmoneta
/usr/local/bin/pgmoneta-cli -c pgmoneta.conf shutdown
```

Puedes acceder a los tres binarios en `/usr/local/bin`
