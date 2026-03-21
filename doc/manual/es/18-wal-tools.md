\newpage

# Herramientas de Write-Ahead Log (WAL)

pgmoneta proporciona dos utilidades de línea de comandos poderosas para trabajar con archivos PostgreSQL Write-Ahead Log (WAL):

- **pgmoneta-walinfo**: Leer y mostrar información sobre archivos WAL
- **pgmoneta-walfilter**: Filtrar archivos WAL según reglas definidas por el usuario.

## pgmoneta-walinfo

`pgmoneta-walinfo` es una utilidad de línea de comandos diseñada para leer y mostrar información sobre archivos PostgreSQL Write-Ahead Log (WAL). La herramienta proporciona salida en formato sin procesar o JSON, lo que facilita el análisis de archivos WAL para depuración, auditoría o propósitos generales de información.

Además de archivos WAL estándar, `pgmoneta-walinfo` también admite:

- Archivos WAL cifrados (**.aes**)
- Archivos WAL comprimidos: **.zstd**, **.gz**, **.lz4**, y **.bz2**
- Archivos TAR que contienen archivos WAL (**.tar**)
- Directorios que contienen archivos WAL

### Uso

```bash
pgmoneta-walinfo 0.20.0
  Utilidad de línea de comandos para leer y mostrar archivos Write-Ahead Log (WAL)

Uso:
  pgmoneta-walinfo [OPTIONS] <file|directory|tar_archive>

Opciones:
  -I,  --interactive Modo interactivo con interfaz ncurses
  -c,  --config      Establece la ruta al archivo pgmoneta_walinfo.conf
  -u,  --users       Establece la ruta al archivo pgmoneta_users.conf
  -RT, --tablespaces Filtrar por espacios de tabla
  -RD, --databases   Filtrar por bases de datos
  -RT, --relations   Filtrar por relaciones
  -R,  --filter      Combinación de -RT, -RD, -RR
  -o,  --output      Archivo de salida
  -F,  --format      Formato de salida (raw, json)
  -L,  --logfile     Establece el archivo de registro
  -q,  --quiet       Sin salida, solo resultado
       --color       Usar colores (on, off)
  -r,  --rmgr        Filtrar por gestor de recursos
  -s,  --start       Filtrar por LSN de inicio
  -e,  --end         Filtrar por LSN de fin
  -x,  --xid         Filtrar por XID
  -l,  --limit       Limitar número de salidas
  -v,  --verbose     Resultado de salida
  -S,  --summary     Mostrar un resumen de conteos de registros WAL agrupados por gestor de recursos
  -V,  --version     Mostrar información de versión
  -m,  --mapping     Proporcionar archivo de asignaciones para traducción de OID
  -t,  --translate   Traducir OIDs a nombres de objetos en registros XLOG
  -?,  --help        Mostrar ayuda
```

### Formatos de salida

#### Modo interactivo

La bandera `-I` o `--interactive` inicia una interfaz de usuario basada en ncurses interactiva para examinar y analizar archivos WAL.

**Características:**

- **Navegador de archivos**: Navega directorios para seleccionar archivos WAL
- **Visualización de registros**: Ver registros WAL detalles en formato de tabla
- **Funcionalidad de búsqueda**: Buscar registros por gestor de recursos, rango de LSN, XID o descripción
- **Visualización codificada por colores**: Colores diferentes para diferentes tipos de registros y componentes
- **Navegación por teclado**: Usa teclas de flecha para navegar, Enter para seleccionar, `?` para ayuda
- **Navegación WAL limitada**: Navega registros WAL usando Inicio para saltar al primer registro, flecha arriba para el registro anterior, y flecha abajo para el siguiente, a través de todos los archivos WAL en el directorio actual.

**Uso:**

```bash
# Modo interactivo en un directorio
pgmoneta-walinfo -I /path/to/wal_directory/

# Modo interactivo en un archivo WAL único
pgmoneta-walinfo -I /path/to/000000010000000000000001
```

**Atajos de teclado:**

- `Teclas de flecha`: Navegar registros y menús
- `Enter`: Seleccionar archivo u opción
- `?`: Mostrar ayuda
- `q`: Salir de la aplicación
- `s`: Iniciar búsqueda
- `n`: Siguiente resultado de búsqueda
- `p`: Resultado de búsqueda anterior
- `l`: Cargar archivo diferente del directorio actual
- `home`: Ir al primer registro en el archivo WAL
- `end`: Ir al último registro en el archivo WAL

#### Formato de salida sin procesar

En formato `raw`, el predeterminado, la salida está estructurada de la siguiente manera:

```
Resource Manager | Start LSN | End LSN | rec len | tot len | xid | description (data and backup)
```

- **Resource Manager**: El nombre del gestor de recursos que maneja el registro de registro.
- **Start LSN**: El número de secuencia de registro (LSN) de inicio.
- **End LSN**: El número de secuencia de registro (LSN) de fin.
- **rec len**: La longitud del registro WAL.
- **tot len**: La longitud total del registro WAL, incluido el encabezado.
- **xid**: El ID de transacción asociado con el registro.
- **description (data and backup)**: Una descripción detallada de la operación, junto con cualquier información de bloque de copia de seguridad relacionada.

Cada parte de la salida está codificada por colores:

- **Rojo**: Información de encabezado (gestor de recursos, longitud de registro, ID de transacción, etc.).
- **Verde**: Descripción del registro WAL.
- **Azul**: Referencias de bloque de copia de seguridad o datos adicionales.

Este formato facilita la distinción visual de diferentes partes del archivo WAL para un análisis rápido.

### Ejemplos

1. **Modo interactivo en un directorio WAL:**

```bash
pgmoneta-walinfo -I /path/to/wal_directory/
```

2. **Modo interactivo en archivo WAL:**

```bash
pgmoneta-walinfo -I /path/to/walfile
```

3. **Ver detalles del archivo WAL en formato JSON:**

```bash
pgmoneta-walinfo -F json /path/to/walfile
```

4. **Ver detalles del archivo WAL con OIDs traducidos a nombres de objetos usando conexión de base de datos:**

```bash
pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -u /path/to/pgmoneta_user.conf /path/to/walfile
```

5. **Ver detalles del archivo WAL con OIDs traducidos usando archivo de asignación:**

```bash
pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -m /path/to/mapping.json /path/to/walfile
```

6. **Mostrar resumen de conteos de registros WAL por gestor de recursos:**

```bash
pgmoneta-walinfo -S /path/to/walfile
```

7. **Filtrar registros por gestor de recursos y limitar salida:**

```bash
pgmoneta-walinfo -r Heap -l 10 /path/to/walfile
```

8. **Analizar un directorio que contiene archivos WAL:**

```bash
pgmoneta-walinfo /path/to/wal_directory/
```

9. **Analizar archivos WAL de un archivo TAR:**

```bash
pgmoneta-walinfo /path/to/wal_backup.tar.gz
```

### Traducción de OID

`pgmoneta-walinfo` admite traducir OIDs en registros WAL a nombres de objetos legibles por humanos de dos formas:

#### Método 1: Conexión de base de datos

Si proporcionas un archivo `pgmoneta_user.conf`, la herramienta se conectará al clúster de base de datos y obtendrá nombres de objetos directamente:

```bash
pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -u /path/to/pgmoneta_user.conf /path/to/walfile
```

#### Método 2: Archivo de asignación

Si proporcionas un archivo de asignación que contiene OIDs y nombres de objetos correspondientes:

```bash
pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -m /path/to/mapping.json /path/to/walfile
```

**Ejemplo de archivo mapping.json:**

```json
{
    "tablespaces": [
        {"pg_default": "1663"},
        {"my_tablespace": "16399"}
    ],
    "databases": [
        {"mydb": "16733"},
        {"postgres": "5"}
    ],
    "relations": [
        {"public.test_table": "16734"},
        {"public.users": "16735"}
    ]
}
```

Puedes generar los datos de asignación usando estas consultas SQL:

```sql
SELECT spcname, oid FROM pg_tablespace;
SELECT datname, oid FROM pg_database;
SELECT nspname || '.' || relname, c.oid FROM pg_class c JOIN pg_namespace n ON c.relnamespace = n.oid;
```

**Notas:**
- Usa la bandera `-t` para habilitar la traducción
- Si se proporcionan tanto `pgmoneta_users.conf` como `mappings.json`, el archivo de asignación tiene precedencia
- Los OIDs no encontrados en el servidor/asignación se mostrarán tal cual

## pgmoneta-walfilter

`pgmoneta-walfilter` es una utilidad de línea de comandos que lee archivos PostgreSQL Write-Ahead Log (WAL) de un directorio de origen, los filtra según reglas definidas por el usuario, recalcula sumas de verificación CRC y escribe los archivos WAL filtrados en un directorio de destino.

### Reglas de filtrado

La herramienta admite dos tipos de reglas de filtrado:

1. **Filtrado de ID de transacción (XID)**: Filtrar XIDs específicos
   - Especifica una lista de XIDs para eliminar del flujo WAL
   - Todos los registros asociados con estos XIDs serán filtrados

2. **Filtrado basado en operaciones**: Filtrar operaciones específicas de base de datos
   - `DELETE`: Elimina todas las operaciones DELETE y sus transacciones asociadas del flujo WAL

### Uso

```bash
pgmoneta-walfilter <yaml_config_file>
```

### Configuración

La herramienta usa un archivo de configuración YAML para especificar directorios de origen y destino y otras configuraciones.

#### Estructura de archivo de configuración

```yaml
source_dir: /path/to/source/backup/directory
target_dir: /path/to/target/directory
configuration_file: /etc/pgmoneta/pgmoneta_walfilter.conf
rules:                             # Opcional: reglas de filtrado
  - xids:                          # Filtrar por IDs de transacción
    - 752
    - 753
  - operations:                    # Filtrar por operaciones
    - DELETE
```

#### Parámetros de configuración

| Parámetro | Tipo | Requerido | Descripción |
|-----------|------|----------|-------------|
| `source_dir` | String | Sí | Directorio de origen que contiene el archivo de copia de seguridad y WAL |
| `target_dir` | String | Sí | Directorio de destino donde se escribirán los archivos WAL filtrados |
| `configuration_file` | String | No | Ruta al archivo pgmoneta_walfilter.conf |
| `rules` | Array | No | Reglas de filtrado a aplicar a archivos WAL |
| `rules.xids` | Array de enteros | No | Lista de IDs de transacción (XIDs) a filtrar |
| `rules.operations` | Array de strings | No | Lista de operaciones a filtrar |

### Cómo funciona

1. **Leer configuración**: Analiza el archivo de configuración YAML
2. **Cargar archivos WAL**: Lee todos los archivos WAL del directorio de origen
3. **Aplicar filtros**: Aplica las reglas de filtrado especificadas:
   - Filtra registros que coinciden con operaciones especificadas (p. ej., DELETE)
   - Filtra registros con IDs de transacción especificados (XIDs)
   - Convierte registros filtrados a operaciones NOOP
4. **Recalcular CRCs**: Actualiza sumas de verificación para registros modificados
5. **Escribir salida**: Guarda archivos WAL filtrados en el directorio de destino

### Ejemplos

#### Uso básico

Crea un archivo de configuración `config.yaml`:

```yaml
source_dir: /path/to/source/directory
target_dir: /path/to/target/directory
configuration_file: /etc/pgmoneta/pgmoneta_walfilter.conf
```

Ejecuta la herramienta:

```bash
pgmoneta-walfilter config.yaml
```

#### Ejemplo de filtrado

Crea un archivo de configuración con reglas de filtrado:

```yaml
source_dir: /path/to/source/directory
target_dir: /path/to/target/directory
configuration_file: /etc/pgmoneta/pgmoneta_walfilter.conf
rules:
  - xids:
    - 752
    - 753
  - operations:
    - DELETE
```

Esta configuración:
- Filtrará todas las operaciones DELETE y sus transacciones asociadas
- Filtrará todos los registros con IDs de transacción 752 y 753

Ejecuta la herramienta:

```bash
pgmoneta-walfilter filter_config.yaml
```

**Archivos de registro:**

La herramienta usa la configuración de registro de `pgmoneta_walfilter.conf`. Consulta el archivo de registro especificado en la configuración para obtener mensajes de error detallados e información de procesamiento.

### Información adicional

Para obtener información más detallada sobre las API internas y documentación del desarrollador, consulta la [Guía del desarrollador WAL](78-wal.md).
