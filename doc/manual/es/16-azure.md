\newpage

# Azure

## Requisitos previos

En primer lugar, necesitas tener una cuenta de Azure, una cuenta de almacenamiento de Azure y un contenedor de blob.

Un contenedor organiza un conjunto de blobs, similar a un directorio en un sistema de archivos. Una cuenta de almacenamiento puede incluir un número ilimitado de contenedores, y un contenedor puede almacenar un número ilimitado de blobs.

Para crear una cuenta de almacenamiento de Azure con el portal de Azure:

1. Inicia sesión en el [portal de Azure][azure].

2. Desde el menú del portal izquierdo, selecciona Cuentas de almacenamiento para mostrar una lista de tus cuentas de almacenamiento. Si el menú del portal no es visible, haz clic en el botón de menú para activarlo.

3. En la página Cuentas de almacenamiento, selecciona Crear.

4. En la pestaña Conceptos básicos, proporciona un nombre de grupo de recursos y un nombre de cuenta de almacenamiento. Puedes mantener la configuración predeterminada de los otros campos.

5. Elige Siguiente: Avanzado.

6. En la pestaña Avanzado, puedes configurar opciones adicionales y modificar la configuración predeterminada para tu nueva cuenta de almacenamiento. Puedes mantener la configuración predeterminada.

7. Elige Siguiente: Redes.

8. En la pestaña Redes, puedes mantener la configuración predeterminada.

9. Elige Siguiente: Protección de datos.

10. En la pestaña Protección de datos, puedes mantener la configuración predeterminada.

11. Elige Siguiente: Cifrado.

12. En la pestaña Cifrado, puedes mantener la configuración predeterminada.

13. Elige Siguiente: Etiquetas.

14. Elige Siguiente: Revisar para ver todas las opciones que hiciste hasta este punto. Cuando estés listo para continuar, elige Crear.

Para crear un contenedor de blob con el portal de Azure:

1. En el panel de navegación de la cuenta de almacenamiento, desplázate a la sección `Almacenamiento de datos` (`Data storage`) y selecciona Contenedores.

2. En el panel Contenedores, selecciona el botón `+ Contenedor` (`+ Container`) para abrir el panel Nuevo contenedor.

3. En el panel Nuevo contenedor, proporciona un Nombre para tu nuevo contenedor.

4. Selecciona Crear para crear el contenedor.

Para obtener la clave compartida de la cuenta de almacenamiento de Azure que es necesaria para la configuración de pgmoneta:

1. En el panel de navegación de la cuenta de almacenamiento, desplázate a la sección `Seguridad + redes` (`Security + networking`) y selecciona Claves de acceso.

2. En key1, encuentra el valor de Clave. Selecciona el botón Copiar para copiar la clave de cuenta.

Puedes usar cualquiera de las dos claves para acceder a Azure Storage, pero en general es una buena práctica usar la primera clave y reservar el uso de la segunda clave para cuando rotes claves.

## Modificar la configuración de pgmoneta

Necesitas tener espacio de almacenamiento para 1 backup en tu computadora local.

Cambia `pgmoneta.conf` para agregar

``` ini
storage_engine = azure
azure_storage_account = the-storage-account-name
azure_container = the-container-name
azure_shared_key = the-storage-account-shared-key
azure_base_dir = directory-where-backups-will-be-stored-in
```

en la sección `[pgmoneta]`.
