## Compilar pgmoneta

### Descripción general

[**pgmoneta**][pgmoneta] se puede construir usando CMake, donde el sistema de compilación detectará el compilador y aplicará las banderas apropiadas para debugging y testing.

El sistema de compilación principal se define en [CMakeLists.txt][cmake_txt]. Las flags para Sanitizers se agregan en opciones de compilación en [src/CMakeLists.txt][src/cmake_txt]

### Compilar

Instala las dependencias con

```sh
dnf install git gcc clang clang-analyzer clang-tools-extra cmake make libev libev-devel openssl openssl-devel systemd systemd-devel zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel libssh libssh-devel python3-docutils libatomic bzip2 bzip2-devel libarchive libarchive-devel libasan libasan-static
```

Para construir [**pgmoneta**][pgmoneta] en modo de release:

```
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
```

o en modo de debug:

```
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug ..
make
```

El compilador también se puede especificar, por ejemplo
```
cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug ..
# o
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug .
```

El sistema de compilación detectará automáticamente la versión del compilador y habilitará las flags apropiadas basadas en el soporte.

### Compilar la documentación

La documentación de [**pgmoneta**][pgmoneta] requiere

* [pandoc](https://pandoc.org/)
* [texlive](https://www.tug.org/texlive/)

```sh
dnf install pandoc texlive-scheme-basic \
            'tex(footnote.sty)' 'tex(footnotebackref.sty)' \
            'tex(pagecolor.sty)' 'tex(hardwrap.sty)' \
            'tex(mdframed.sty)' 'tex(sourcesanspro.sty)' \
            'tex(ly1enc.def)' 'tex(sourcecodepro.sty)' \
            'tex(titling.sty)' 'tex(csquotes.sty)' \
            'tex(zref-abspage.sty)' 'tex(needspace.sty)'
```

También necesitarás la plantilla `Eisvogel` que puedes instalar a través de

```sh
wget https://github.com/Wandmalfarbe/pandoc-latex-template/releases/download/v3.4.0/Eisvogel-3.4.0.tar.gz
tar -xzf Eisvogel-3.4.0.tar.gz
mkdir -p ~/.local/share/pandoc/templates
mv Eisvogel-3.4.0/eisvogel.latex ~/.local/share/pandoc/templates/
```

donde `$HOME` es tu directorio de inicio.

### Generar guía de API

Este proceso es opcional. Si decides no generar los archivos API HTML, puedes optar por no descargar estas dependencias, y el proceso automáticamente omitirá la generación.

Descargar dependencias

``` sh
dnf install graphviz doxygen
```

Estos paquetes serán detectados durante `cmake` y se construirán como parte de la compilación principal.

### Política y directrices para usar IA

Nuestro objetivo en el proyecto pgmoneta es desarrollar un excelente sistema de software. Esto requiere atención a los detalles en cada cambio que integramos. El tiempo y la atención de los mantenedores son muy limitados, por lo que es importante que los cambios que nos pidas revisar representen tu mejor trabajo.

Se te anima a usar herramientas que te ayuden a escribir buen código, incluidas herramientas de IA. Sin embargo, como se mencionó anteriormente, siempre necesitas entender y explicar los cambios que propones hacer, ya sea que hayas usado un LLM como parte de tu proceso para producirlos o no. La respuesta a "¿Por qué realizaste el cambio X?" nunca debe ser "No estoy seguro. La IA lo hizo."

No envíes un PR generado por IA que no hayas entendido y probado personalmente, ya que esto desperdicia el tiempo de los mantenedores. Los PR que parecen violar esta directriz serán cerrados sin revisarlos. Usar IA como asistente y ayuda para crear una base o esquelot para tu código.

* No omitas familiarizarte con la parte de la base de código en la que estás trabajando. Esto te permitirá escribir mejores indicaciones y validar su resultado si usas un LLM. Los asistentes de código pueden ser una herramienta de búsqueda/descubrimiento útil en este proceso, pero no confíes en las afirmaciones que hacen sobre cómo funciona pgmoneta. Los LLM a menudo se equivocan, incluso sobre detalles que se responden claramente en la documentación de pgmoneta y el código circundante
* No simplemente pidas a un LLM que agregue comentarios de código, ya que probablemente producirá un montón de texto que explique innecesariamente lo que ya está claro en el código. Si usas un LLM para generar comentarios, sé muy específico en tu solicitud, demanda concisión y edita cuidadosamente el resultado.

**Usar IA para comunicación**

Como se mencionó anteriormente, se espera que los colaboradores de pgmoneta se comuniquen con intención, para evitar perder tiempo de los mantenedores con escritura larga y descuidada. Preferimos fuertemente la comunicación clara y concisa sobre puntos que realmente requieren discusión sobre comentarios largos generados por IA.

Cuando usas un LLM para escribir un mensaje para ti, sigue siendo tu responsabilidad leer todo y asegurarte de que tenga sentido para ti y represente tus ideas de manera concisa. Una buena regla general es que si no puedes leer cuidadosamente alguna respuesta que generaste usando IA, nadie más querra leerla tampoco.

Aquí hay algunas directrices concretas para usar LLM como parte de tus flujos de trabajo de comunicación.

* Al escribir una descripción de solicitud de pull, no incluyas nada que sea obvio mirando tus cambios directamente (por ejemplo, archivos cambiados, funciones actualizadas, etc.). En su lugar, enfócate en el por qué detrás de tus cambios. No pidas a un LLM que genere una descripción de PR en tu nombre basado en tus cambios de código, ya que simplemente regurgitará la información que ya está allí.
* De manera similar, cuando respondas a un comentario de un pull request, explica tu razonamiento. No uses un LLM para redescribir lo que ya se puede ver desde el código.
* Verifica que todo lo que escribas sea exacto, ya sea que un LLM haya generado alguna parte de él o no. Los mantenedores de pgmoneta no podrán revisar tus contribuciones si tergiversas tu trabajo (por ejemplo, describir incorrectamente tus cambios de código, su efecto o tu proceso de prueba).
* Completa la template de descripción de PR, tal vez con capturas de pantalla y la self-review checklist. No simplemente sobrescribas la plantilla con la respuesta de una IA.
* La claridad y la brevedad son mucho más importantes que la gramática perfecta, por lo que no deberías sentirte obligado a reescribir todo usando IA. Si le pides a una IA que limpie tu estilo de escritura, asegúrate de que no lo haga más largo en el proceso.
* Citar una respuesta de IA suele ser menos útil que vincular a fuentes relevantes, como código fuente, documentación o estándares web. Si necesitas citar una respuesta de IA en una conversación de pgmoneta, coloca la respuesta en un bloque de comillas de pgmoneta, para distinguir la salida de IA de tus propias ideas.

### Sanitizer

Antes de hacer build de pgmoneta con sanitizers, asegúrate de tener los paquetes requeridos instalados:

* `libasan` - Biblioteca runtime de AddressSanitizer
* `libasan-static` - Versión estática de la biblioteca de runtime de AddressSanitizer

En sistemas Red Hat/Fedora:
```
sudo dnf install libasan libasan-static
```

Los nombres de paquetes y versiones pueden variar según tu distribución y versión del compilador.

### Flags de Sanitizer

**AddressSanitizer (ASAN)**

Address Sanitizer es un detector de errores de memoria que ayuda a encontrar use-after-free, desbordamientos de búfer de heap/stack/global, use-after-return, errores de orden de inicialización y fugas de memoria.

**UndefinedBehaviorSanitizer (UBSAN)**

UndefinedBehaviorSanitizer es un detector rápido de comportamiento indefinido que puede encontrar varios tipos de comportamiento indefinido durante la ejecución del programa, como desbordamiento de enteros, desreferencia de punteros nulos, y más.

**Flags comunes**

* `-fno-omit-frame-pointer` - Proporciona mejores seguimientos del stack en informes de errores
* `-Wall -Wextra` - Habilita advertencias adicionales del compilador

**Soporte de GCC**

* `-fsanitize=address` - Habilita Address Sanitizer (GCC 4.8+)
* `-fsanitize=undefined` - Habilita Undefined Behavior Sanitizer (GCC 4.9+)
* `-fno-sanitize=alignment` - Desactiva la verificación de alineación (GCC 5.1+)
* `-fno-sanitize-recover=all` - Hace que todos los sanitizers se detengan en error (GCC 5.1+)
* `-fsanitize=float-divide-by-zero` - Detecta división por cero de punto flotante (GCC 5.1+)
* `-fsanitize=float-cast-overflow` - Detecta desbordamientos de fundición de punto flotante (GCC 5.1+)
* `-fsanitize-recover=address` - Permite que el programa continúe la ejecución después de detectar un error (GCC 6.0+)
* `-fsanitize-address-use-after-scope` - Detecta errores de use-after-scope (GCC 7.0+)

**Soporte de Clang**

* `-fsanitize=address` - Habilita Address Sanitizer (Clang 3.2+)
* `-fno-sanitize=null` - Desactiva la verificación de desreferencia de puntero nulo (Clang 3.2+)
* `-fno-sanitize=alignment` - Desactiva la verificación de alineación (Clang 3.2+)
* `-fsanitize=undefined` - Habilita Undefined Behavior Sanitizer (Clang 3.3+)
* `-fsanitize=float-divide-by-zero` - Detecta división por cero de punto flotante (Clang 3.3+)
* `-fsanitize=float-cast-overflow` - Detecta desbordamientos de fundición de punto flotante (Clang 3.3+)
* `-fno-sanitize-recover=all` - Hace que todos los sanitizers se detengan en error (Clang 3.6+)
* `-fsanitize-recover=address` - Permite que el programa continúe la ejecución después de detectar un error (Clang 3.8+)
* `-fsanitize-address-use-after-scope` - Detecta errores de use-after-scope (Clang 3.9+)

### Opciones adicionales de Sanitizer

Los desarrolladores pueden agregar flags de sanitizer adicionales a través de variables de entorno. Algunas opciones útiles incluyen:

**Opciones ASAN**

* `ASAN_OPTIONS=detect_leaks=1` - Habilita detección de fugas de memoria
* `ASAN_OPTIONS=halt_on_error=0` - Continúa la ejecución después de errores
* `ASAN_OPTIONS=detect_stack_use_after_return=1` - Habilita detección de stack use-after-return
* `ASAN_OPTIONS=check_initialization_order=1` - Detecta problemas de orden de inicialización
* `ASAN_OPTIONS=strict_string_checks=1` - Habilita verificación rigurosa de funciones de string
* `ASAN_OPTIONS=detect_invalid_pointer_pairs=2` - Validación mejorada de pares de punteros
* `ASAN_OPTIONS=print_stats=1` - Imprime estadísticas sobre memoria asignada
* `ASAN_OPTIONS=verbosity=1` - Aumenta la verbosidad de los logs

**Opciones UBSAN**

* `UBSAN_OPTIONS=print_stacktrace=1` - Hace print de stack traces para errores
* `UBSAN_OPTIONS=halt_on_error=1` - Detiene la ejecución en el primer error
* `UBSAN_OPTIONS=silence_unsigned_overflow=1` - Silencia reportes de desbordamiento de enteros sin signo

### Compilar con Sanitizers

Para construir [**pgmoneta**][pgmoneta] con sanitizers:

```
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

El compilador también se puede especificar
```
cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug ..
# o
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug .
```

El sistema de compilación detectará automáticamente la versión del compilador y habilitará las flags de sanitizer apropiadas.

### Ejecutar con Sanitizers

Cuando ejecutas [**pgmoneta**][pgmoneta] compilado con sanitizers, todos los errores se reportarán a stderr.

Para obtener reportes más detallados, puedes establecer variables de entorno adicionales:

```
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:detect_stack_use_after_return=1 ./pgmoneta
```

Puedes combinar opciones ASAN y UBSAN:

```
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ./pgmoneta
```

### Opciones avanzadas de Sanitizer no incluidas por defecto

Los desarrolladores pueden querer experimentar con flags de sanitizer adicionales no habilitadas por defecto:

* `-fsanitize=memory` - Habilita MemorySanitizer (MSan) para detectar lecturas no inicializadas (Tenga en cuenta que esto no se puede usar con ASan)
* `-fsanitize=integer` - Solo verificar operaciones de enteros (subconjunto de UBSan)
* `-fsanitize=bounds` - Verificación de límites de matriz (subconjunto de UBSan)
* `-fsanitize-memory-track-origins` - Rastrea orígenes de valores no inicializados (con MSan)
* `-fsanitize-memory-use-after-dtor` - Detecta errores de use-after-destroy (con MSan)
* `-fno-common` - Evita que las variables se fusionen en bloques comunes, ayudando a identificar problemas de acceso a variables

Tenga en cuenta que algunos sanitizers son incompatibles entre sí. Por ejemplo, no puedes usar ASan y MSan juntos.

