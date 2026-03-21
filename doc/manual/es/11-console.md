\newpage

# Consola web

La consola web de pgmoneta es una interfaz HTTP ligera para monitorear métricas de PostgreSQL
en tiempo real. Muestra métricas organizadas por categoría, con opciones de filtrado
y múltiples modos de vista para ayudarte a profundizar en los datos que necesitas.

## Habilitar la consola

Agrega un puerto de consola a `pgmoneta.conf`:

```ini
[pgmoneta]
host = 127.0.0.1
metrics = 5002
console = 5003
```

La consola requiere que el endpoint de métricas esté habilitado. Inicia pgmoneta:

```sh
pgmoneta -c /etc/pgmoneta/pgmoneta.conf
```

## Abrir la consola

Navega a tu endpoint de consola:

```
http://localhost:5003/
```

## Flujo de consola y páginas

### 1. Página de inicio (descripción general)

Cuando cargas por primera vez la consola, ves la página de inicio con:

- Encabezado de servicio mostrando el estado del servicio pgmoneta (En ejecución o No disponible)
- Versión de pgmoneta
- Desplegable selector de categoría para elegir qué grupo de métricas ver
- Selector de vista (modo Simple o Avanzado)
- Desplegable filtro de servidor para elegir qué servidores PostgreSQL mostrar
- Tabla de métricas mostrando la categoría seleccionada
- Botón Actualizar para recargar métricas y estado
- Botón alternar de tema (Oscuro/Claro)

### 2. Página de inicio vista simple

La vista simple predeterminada muestra:

- Nombre de métrica (columna 1)
- Valor (columna 2)
- Columnas de etiqueta (columnas adicionales), donde cada clave de etiqueta aparece como su propia columna

![Página de inicio de consola web en vista simple](../images/console_home_simple.png)

### 3. Página de inicio vista avanzada

Cambia a vista avanzada para ver:

- Nombre de métrica (columna 1)
- Tipo (gauge, counter, histogram, etc.) (columna 2)
- Valor (columna 3)
- Etiquetas (columna 4), una columna separada por comas, por ejemplo `database=mydb, name=primary`

![Página de inicio de consola web en vista avanzada](../images/console_home_advanced.png)

### 4. Organización por categorías

Las métricas se organizan automáticamente en categorías basadas en prefijos compartidos:

- `pgmoneta_retention_*` como una categoría
- `pgmoneta_backup_*` como otra categoría
- y así sucesivamente

Usa el selector de Categoría para cambiar entre categorías. La página muestra todas
las métricas en la categoría seleccionada.

### 5. Filtro de servidor

El desplegable Filtro de servidor:

- Muestra todos los servidores PostgreSQL configurados
- Permite selección múltiple (marca y desmarca cada servidor)
- Actualiza la tabla de métricas para mostrar filas solo de servidores seleccionados

![Página de inicio de consola web con filtro de servidor](../images/console_home_server_filter.png)

### 6. Botón Actualizar

Haz clic en Actualizar en el encabezado (junto a la marca de tiempo Actualizado) para recargar todas
las métricas y el estado del servicio.

## Endpoints de API

- `/` para la consola principal (página de inicio)
- `/api` para un endpoint JSON con todas las métricas (útil para scripting)

## Alternar tema

Haz clic en el botón de tema (icono luna/sol) en la esquina superior derecha para cambiar entre:

- Modo claro (predeterminado, fondo blanco)
- Modo oscuro (fondo oscuro)

La preferencia de tema se guarda en el almacenamiento local de tu navegador.

## Valores de estado del servicio

El encabezado muestra:

- En ejecución cuando el servicio de administración de pgmoneta es accesible
- No disponible cuando el servicio de administración de pgmoneta no es accesible

## Solución de problemas

- Sin métricas mostradas:
  Asegúrate de que el puerto `metrics` esté habilitado en `pgmoneta.conf` y verifica que el
  endpoint de Prometheus sea accesible en el host configurado.

- El servicio muestra No disponible:
  Verifica que `unix_socket_dir` sea escribible y que la ruta del directorio configurada
  sea accesible al proceso de pgmoneta.

- No se puede acceder a la consola:
  Verifica que el puerto de consola no esté bloqueado por el firewall y que el host/puerto de consola
  en `pgmoneta.conf` coincida con tu URL.
