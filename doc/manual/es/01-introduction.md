\newpage

# Introducción

[**pgmoneta**][pgmoneta] es una solución de backup y restauración para [PostgreSQL][postgresql].

En un mundo ideal, no sería necesario realizar backups ni planificar recuperación de datos ante desastres, pero así no funciona el mundo real.

Posibles escenarios que pueden ocurrir:

* Corrupción de datos
* Fallo del sistema
* Error humano
* Desastre natural

En estos casos, el administrador de base de datos debe restaurar el sistema al estado correcto de recuperación.

Dos factores clave son:

* Recovery Point Objective (RPO): El período máximo objetivo durante el cual se pueden perder datos de un servicio de TI debido a un incidente grave
* Recovery Time Objective (RTO): La duración objetivo y el nivel de servicio dentro del cual un proceso de negocio debe ser restaurado después de un desastre (o interrupción) para evitar consecuencias inaceptables asociadas con una ruptura en la continuidad del negocio

Lo ideal es que ambos valores estén lo más cercanos a cero posible: un RPO de 0 significa que no se perderán datos, y un RTO de 0 significa que el sistema se recupera de inmediato. Sin embargo, esto es más fácil de decir que de hacer.

[**pgmoneta**][pgmoneta] se enfoca en ofrecer funcionalidades que permitan a los sistemas de base de datos acercarse lo más posible a estos objetivos, de modo que se pueda implementar alta disponibilidad del 99.99% o más, y monitorearla mediante herramientas estándar.

[**pgmoneta**][pgmoneta] lleva el nombre de la Diosa Romana de la Memoria.

## Funcionalidades

* Backup completo
* Restauración
* Compresión (gzip, zstd, lz4, bzip2)
* Soporte de cifrado AES
* Soporte de enlaces simbólicos
* Soporte de WAL shipping
* Hot standby
* Soporte de Prometheus
* Administración remota
* Detección sin conexión
* Soporte de Transport Layer Security (TLS) v1.2+
* Modo daemon
* Bóveda de usuarios

[**pgmoneta**][pgmoneta] tiene un servidor [Model Context Protocol](https://modelcontextprotocol.io/)
llamado [pgmoneta_mcp](https://github.com/pgmoneta/pgmoneta_mcp).

## Plataformas

Las plataformas soportadas son:

* [Fedora][fedora] 39+
* [RHEL][rhel] 9
* [RockyLinux][rocky] 9
* [FreeBSD][freebsd]
* [OpenBSD][openbsd]

## Migración

### De 0.20.x a 0.21.0

#### Configuración del límite de velocidad de backup

La configuración del límite de velocidad para backups ha sido consolidada.

Este es un **cambio incompatible** para los archivos de configuración existentes.

`backup_max_rate` y `network_max_rate` ya no son claves válidas y han sido
reemplazadas por una única clave `max_rate`.

`max_rate` se configura en **bytes por segundo**.

**Acción requerida:**

1. Actualizar `pgmoneta.conf` y reemplazar las claves antiguas:
   - `backup_max_rate`
   - `network_max_rate`
2. Establecer `max_rate` en su lugar (global y/o por servidor).
3. Recargar o reiniciar pgmoneta.

Ejemplo:
```conf
max_rate = 1000000
```

#### Cifrado de la bóveda

La derivación de clave para el cifrado del archivo de bóveda ha sido actualizada a
`PKCS5_PBKDF2_HMAC` (SHA-256, sal aleatoria de 16 bytes, 600,000 iteraciones).

Este es un **cambio incompatible**. Los archivos de bóveda existentes cifrados con el
método anterior no pueden ser descifrados por la versión 0.21.0.

**Acción requerida:**

1. Detener pgmoneta
2. Eliminar los archivos de usuario existentes:
   - `pgmoneta_users.conf`
   - `pgmoneta_admins.conf`
   - Archivo de usuarios de la bóveda (si aplica)
3. Eliminar la clave maestra existente:
```
   rm ~/.pgmoneta/master.key
```
4. Regenerar la clave maestra:
```
   pgmoneta-admin master-key
```
5. Volver a agregar todos los usuarios:
```
   pgmoneta-admin user add -f <archivo_usuarios>
```
6. Reiniciar pgmoneta