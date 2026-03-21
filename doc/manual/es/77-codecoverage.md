## Cobertura de código

### Detección automática de cobertura de código

Si tanto `gcov` como `gcovr` están instalados en tu sistema **y el compilador está configurado en GCC** (independientemente del tipo de compilación), la cobertura de código se habilitará automáticamente durante el proceso de compilación. Los scripts de compilación detectarán estas herramientas y establecerán las banderas apropiadas. Si falta cualquiera de las herramientas, o si el compilador no es GCC, la cobertura de código se omitirá y un mensaje indicará que no se encontraron las herramientas de cobertura o el compilador no es compatible.

### Generar informes de cobertura

Después de compilar el proyecto con cobertura habilitada y ejecutar tu conjunto de pruebas, los informes de cobertura se generarán automáticamente en el directorio `build/coverage` si `gcov` y `gcovr` están disponibles.

Los siguientes comandos se utilizan para generar los informes (ejecutados automáticamente por los scripts de prueba):

```sh
# Asegúrate de que el directorio de cobertura existe
mkdir -p ./coverage

gcovr -r ../src --object-directory . --html --html-details -o ./coverage/index.html
gcovr -r ../src --object-directory . > ./coverage/summary.txt
```

**Importante:**
Estos comandos deben ejecutarse desde dentro del directorio `build`.

- El informe HTML estará disponible en `build/coverage/index.html`
- Un informe de resumen de texto estará disponible en `build/coverage/summary.txt`

Si deseas generar estos informes manualmente después de ejecutar tus propias pruebas, simplemente ejecuta los comandos anteriores desde tu directorio `build`.

> **Nota:** Si el directorio `coverage` no existe, créalo primero usando `mkdir -p ./coverage` antes de ejecutar los comandos de cobertura.
>
> **Importante:** `gcovr` solo funciona con compilaciones de GCC. Si compilas el proyecto con Clang, los informes de cobertura no se generarán con `gcovr`.

### Notas

- Asegúrate de tener tanto `gcov` como `gcovr` instalados antes de compilar el proyecto para habilitar la cobertura.
- Si no se encuentran las herramientas de cobertura, o el compilador no es GCC, la generación de cobertura se omitirá automáticamente y se mostrará un mensaje.
- Siempre puedes volver a ejecutar los comandos de cobertura manualmente si es necesario.

---
