\newpage

# SSH

## Requisitos previos

En primer lugar, necesitas tener un servidor remoto donde puedas almacenar tus backups.

Tomemos una instancia de EC2 como ejemplo, después de lanzar una instancia de EC2 debes agregar una nueva cuenta de usuario con acceso SSH a la instancia de EC2:

1. [Conéctate a tu instancia de Linux usando SSH.][aws_access]

2. Usa el comando adduser para agregar una nueva cuenta de usuario a una instancia de EC2 (reemplaza new_user con el nombre de la nueva cuenta).

``` sh
sudo adduser new_user --disabled-password
```

3. Cambia el contexto de seguridad a la cuenta new_user de manera que las carpetas y archivos que crees tengan los permisos correctos:

``` sh
sudo su - new_user
```

4. Crea un directorio .ssh en el directorio de inicio de new_user y usa el comando chmod para cambiar los permisos del directorio .ssh a 700:

```sh
mkdir .ssh && chmod 700 .ssh
```

5. Usa el comando touch para crear el archivo authorized_keys en el directorio .ssh y usa el comando chmod para cambiar los permisos del archivo .ssh/authorized_keys a 600:

```sh
touch .ssh/authorized_keys && chmod 600 .ssh/authorized_keys
```

6. Recupera la clave pública para el par de claves en tu computadora local:

```sh
cat ~/.ssh/id_rsa.pub
```

7. En la instancia de EC2, ejecuta el comando cat en modo de adición:

```sh
cat >> .ssh/authorized_keys
```

8. Pega la clave pública en el archivo .ssh/authorized_keys y luego presiona Enter.

9. Presiona y mantén Ctrl+d para salir de cat y volver al símbolo del sistema de la sesión de línea de comandos.

Para verificar que el nuevo usuario puede usar SSH para conectarse a la instancia de EC2, ejecuta el siguiente comando desde un símbolo del sistema de línea de comandos en tu computadora local:

```sh
ssh new_user@public_dns_name_of_EC2_instance
```

## Modificar la configuración de pgmoneta

Necesitas crear un directorio en tu servidor remoto donde se puedan almacenar los backups.

Además, tu computadora local necesita tener espacio de almacenamiento para 1 backup.

Cambia `pgmoneta.conf` para agregar

```sh
storage_engine = ssh
ssh_hostname =  your-public_dns_name_of_EC2_instance
ssh_username = new_user
ssh_base_dir = the-path-of-the-directory-where-backups-stored-in
```

bajo la sección `[pgmoneta]`.

## Opcional: Archivos de clave SSH personalizados

Por defecto, pgmoneta usa el par de claves SSH estándar ubicado en `~/.ssh/id_rsa` (clave privada) y `~/.ssh/id_rsa.pub` (clave pública).

Si necesitas usar un par de claves SSH diferente, puedes especificar rutas personalizadas:

```sh
ssh_public_key_file = /path/to/custom/public_key.pub
ssh_private_key_file = /path/to/custom/private_key
```

Ambos parámetros soportan interpolación de variables de entorno (por ejemplo, `$HOME`, `$USER`).
