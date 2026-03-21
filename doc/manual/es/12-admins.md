\newpage

# Acceso de administración

Puedes acceder a [**pgmoneta**][pgmoneta] desde una máquina remota si habilitas el acceso.

## Configuración

Primero, necesitas habilitar el acceso remoto agregando

```
management = 5002
```

en `pgmoneta.conf` en la sección `[pgmoneta]`.

## Administradores

A continuación, necesitarás agregar uno o más administradores en `pgmoneta_admins.conf` a través de_

```
pgmoneta-admin -f /etc/pgmoneta/pgmoneta_admins.conf user add
```

por ejemplo con un nombre de usuario de `admin` y `secretpassword` como contraseña.

## Reiniciar pgmoneta

Tienes que reiniciar [**pgmoneta**][pgmoneta] para que los cambios tengan efecto.

## Conectar a pgmoneta

Luego usarás la herramienta `pgmoneta-cli` para acceder a [**pgmoneta**][pgmoneta] con

```
pgmoneta-cli -h myhost -p 5002 -U admin status
```

para ejecutar el comando `status` después de haber ingresado la contraseña.

## Soporte de Transport Level Security

Puedes asegurar la interfaz a nivel de administración utilizando Transport Level Security (TLS).

Se hace configurando las siguientes opciones:

```
[pgmoneta]
tls_cert_file=/path/to/server.crt
tls_key_file=/path/to/server.key
tls_ca_file=/path/to/root.crt

...
```

en `pgmoneta.conf`.

La configuración del lado del cliente debe ir en `~/.pgmoneta/` con los siguientes archivos

```
~/.pgmoneta/pgmoneta.key
~/.pgmoneta/pgmoneta.crt
~/.pgmoneta/root.crt
```

Deben tener permiso 0600.
