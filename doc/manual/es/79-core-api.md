## APIs principales

[**pgmoneta**][pgmoneta] ofrece estructuras de datos y APIs para ayudarte a escribir código más seguro y
permitirte desarrollar funcionalidades más avanzadas. Actualmente, ofrecemos adaptive radix tree (ART),
deque y JSON, que están todos basados en un sistema universal de tipos de valor que te ayuda a gestionar la memoria fácilmente.

El documento se enfocará principalmente en decisiones de diseño, funcionalidades y cosas que debes tener cuidado. Puede
ofrecer algunos ejemplos de cómo usar las APIs.

### Value
El struct `value` y sus APIs están definidas e implementadas en [value.h][value_h] y [value.c][value_c].

El struct `value` encapsula los datos subyacentes y gestiona su memoria según el tipo especificado por los usuarios.
En algunos casos los datos se almacenan inline, otras veces almacena un pointer al memory real.
La mayoría del tiempo el struct `value` es transparente para los usuarios. El caso de uso más común sería que el usuario ponga los datos en alguna
estructura de datos como un deque. El deque envolverá internamente los datos en un objeto value. Cuando el usuario lee los datos,
el deque desenvuelve el value y retorna los datos internos. Una excepción aquí es cuando trabajas con iterators,
el iterator retornará el wrapper value directamente, el cual te dice el tipo de los datos del value.
Esto te permite almacenar diferentes tipos de datos de valor en una estructura de datos sin preocuparte por perder la información de tipo
de los datos al iterar sobre la estructura.

Cuando liberas el deque, el deque automáticamente libera todos los datos almacenados dentro. En otras palabras, nunca necesitarás iterar
sobre el deque y liberar manualmente todos los datos almacenados explícitamente.

El struct `value` también puede imprimir los datos envueltos de acuerdo a su tipo. Esto es conveniente para debugging y construir
salida -- puesto que `deque`, `ART` y `JSON` también son value types, y sus datos internos están todos envueltos en `value`,
su contenido puede ser fácilmente impreso.

**Tipos**

Soportamos los siguientes tipos de valor:

| tipo            | enum de tipo     | comportamiento de liberación<br/> (sin-op si se deja en blanco)                                 |
|:----------------|:-----------------|:------------------------------------------------------------------------|
| `none`          | `ValueNone`      |                                                                         |
| `int8_t`        | `ValueInt8`      |                                                                         |
| `uint8_t`       | `ValueUInt8`     |                                                                         |
| `int16_t`       | `ValueInt16`     |                                                                         |
| `uint16_t`      | `ValueUInt16`    |                                                                         |
| `int32_t`       | `ValueInt32`     |                                                                         |
| `uint32_t`      | `ValueUInt32`    |                                                                         |
| `int64_t`       | `ValueInt64`     |                                                                         |
| `uint64_t`      | `ValueUInt64`    |                                                                         |
| `char`          | `ValueChar`      |                                                                         |
| `bool`          | `ValueBool`      |                                                                         |
| `char*`         | `ValueString`    | `free()`                                                                |
| `char*`         | `ValueStringRef` |                                                                         |
| `float`         | `ValueFloat`     |                                                                         |
| `double`        | `ValueDouble`    |                                                                         |
| `char*`         | `ValueBASE64`    | `free()`                                                                |
| `char*`         | `ValueBASE64Ref` |                                                                         |
| `struct json*`  | `ValueJSON`      | `pgmoneta_json_destroy()`, esto destruirá recursivamente datos internos  |
| `struct json*`  | `ValueJSONRef`   |                                                                         |
| `struct deque*` | `ValueDeque`     | `pgmoneta_deque_destroy()`, esto destruirá recursivamente datos internos |
| `struct deque*` | `ValueDequeRef`  |                                                                         |
| `struct art*`   | `ValueART`       | `pgmoneta_art_destroy()`, esto destruirá recursivamente datos internos   |
| `struct art*`   | `ValueARTRef`    |                                                                         |
| `void*`         | `ValueRef`       |                                                                         |
| `void*`         | `ValueMem`       | `free()`                                                                |

Quizás hayas notado que algunos tipos tienen tipos `Ref` correspondientes. Esto es especialmente útil cuando intentas compartir
datos entre múltiples estructuras de datos -- solo una de ellas debería estar a cargo de liberar el value. Las demás solo deberían
tomar el tipo de referencia correspondiente para evitar double free.

Hay casos donde intentas poner un pointer en la estructura de datos principal, pero no es ninguno de los tipos predefinidos.
En tales casos, ofrecemos algunas opciones:

* Si quieres liberar la memoria apuntada tú mismo, o no necesita ser liberada, usa `ValueRef`.
* Si solo necesitas invocar un simple `free()`, usa `ValueMem`.
* Si necesitas personalizar cómo destruir el value, ofrecemos APIs que te permiten configurar el comportamiento tú mismo, lo cual será ilustrado abajo.

Ten en cuenta que el sistema no fuerza ningún tipo de borrow checks o validación de lifetime. Sigue siendo responsabilidad del programador
usar el sistema correctamente y asegurar seguridad de memoria. Pero esperamos que el sistema haga la carga un poco más ligera.

**APIs**

**pgmoneta_value_create**

Crea un value para envolver tus datos. Internamente el value usa un `uintptr_t` para mantener tus datos en su lugar o usarlo para representar
un pointer, así que simplemente castea tus datos a `uintptr_t` antes de pasarlo a la función (una excepción es cuando intentas
 poner float o double, lo cual requiere trabajo extra, ver pgmoneta_value_from_float/pgmoneta_value_from_double para detalles).
Para `ValueString` o `ValueBASE64`, el value **hace una copia** de tus datos string. Así que si tu string está malloced en heap,
aún necesitas liberarlo puesto que lo que el value mantiene es una copia.

```
pgmoneta_value_create(ValueString, (uintptr_t)str, &val);
// libera el string si está almacenado en heap
free(str);
```

**pgmoneta_value_create_with_config**

Crea un value wrapper con tipo `ValueRef` y callbacks personalizados de destroy y to-string. Si quieres dejar un callback como
default, establece el campo a NULL.

Normalmente no necesitas crear un value tú mismo, pero indirectamente lo invocarás cuando intentas poner datos en un deque o ART
con configuración personalizada.

La definición del callback es
```
typedef void (*data_destroy_cb)(uintptr_t data);
typedef char* (*data_to_string_cb)(uintptr_t data, int32_t format, char* tag, int indent);
```

**pgmoneta_value_to_string**

Esto invoca el callback to-string interno e imprime el contenido del dato envuelto. Normalmente no necesitas llamar esta función
tú mismo, puesto que las estructuras de datos anidadas la invocarán por ti en cada value almacenado.

Para tipos de estructuras de datos principal, como `deque`, `ART` o `JSON`, hay múltiples tipos de formato soportados:
* `FORMAT_JSON`: Esto imprime el dato envuelto en formato JSON
* `FORMAT_TEXT`: Esto imprime el dato envuelto en formato YAML-like
* `FORMAT_JSON_COMPACT`: Esto imprime el dato envuelto en formato JSON, con todos los espacios en blanco omitidos

Ten en cuenta que el formato también puede afectar tipos primitivos. Por ejemplo, un string será encerrado por `"` en formato JSON, mientras que en
formato TEXT, será impreso tal cual.

Para `ValueMem` y `ValueRef`, el pointer a la memoria será impreso.

**pgmoneta_value_data**

Función de lector para desenvolver el dato del wrapper value. Esto es especialmente útil cuando obtuviste el value con wrapper desde el iterator.

**pgmoneta_value_type**

Función de lector para obtener el tipo del wrapper value. La función retorna `ValueNone` si la entrada es NULL.

**pgmoneta_value_destroy**

Destruye un value, esto invoca el callback de destroy para destruir el dato envuelto.

**pgmoneta_value_to_float/pgmoneta_value_to_double**

Usa la función correspondiente para castear los datos crudos a float o double que habías envuelto dentro del value.

Float y double types se almacenan en su lugar dentro del campo de datos `uintptr_t`. Pero puesto que C no puede castear automáticamente
un `uintptr_t` a float o double correctamente, -- no interpreta la representación de bits tal cual -- tenemos que
recurrir a algo de magia union para forzar el casteo.

**pgmoneta_value_from_float/pgmoneta_value_from_double**

Por la misma razón mencionada arriba, usa la función correspondiente para castear float o double que intentas
poner dentro del wrapper value a datos crudos.
```
pgmoneta_value_create(ValueFloat, pgmoneta_value_from_float(float_val), &val);
```

**pgmoneta_value_to_ref**

Retorna el tipo de referencia correspondiente. Input `ValueJSON` te dará `ValueJSONRef`. Para tipos in-place como
`ValueInt8`, o si el tipo ya es el tipo de referencia, el mismo tipo será retornado.

### Deque

El deque está definido e implementado en [deque.h][deque_h] y [deque.c][deque_c].
El deque está construido sobre el sistema value, así que puede automáticamente destruir los items internos cuando se destruye.

Puedes especificar un tag opcional para cada nodo deque, así que puedes de cierta forma usarlo como un key-value map. Sin embargo, desde la
introducción de ART y json, este no es el uso recomendado anymore.

**APIs**

**pgmoneta_deque_create**

Crea un deque. Si thread safe está establecido, un global read/write lock será adquirido antes de que intenttes escribir al deque o leerlo.
El deque debería aún ser usado con cautela incluso con thread safe habilitado -- no guarda contra el value que has leído.
Así que si habías almacenado un pointer, deque no protegerá la memoria apuntada de ser modificada por otro thread.

**pgmoneta_deque_add**

Agregar un value a la cola del deque. Necesitas castear el value a `uintptr_t` puesto que crea un wrapper value por debajo.
De nuevo, para float y double necesitas usar la función de casteo de tipo correspondiente (`pgmoneta_value_from_float` / `pgmoneta_value_from_double`).
La función adquiere write lock si thread safe está habilitado.

La complejidad de tiempo para agregar un nodo es O(1).

**pgmoneta_deque_add_with_config**

Agregar datos con tipo `ValueRef` y callbacks personalizados to-string/destroy al deque. La función adquiere write lock
si thread safe está habilitado.
```
static void
rfile_destroy_cb(uintptr_t data)
{
   pgmoneta_rfile_destroy((struct rfile*) data);
}

static void
add_rfile()
{
struct deque* sources;
struct rfile* latest_source;
struct value_config rfile_config = {.destroy_data = rfile_destroy_cb, .to_string = NULL};
...

pgmoneta_deque_add_with_config(sources, NULL, (uintptr_t)latest_source, &rfile_config);
}
```

**pgmoneta_deque_poll**

Recupera value y remueve el nodo de la cabeza del deque. Si el nodo tiene tag, puedes opcionalmente leerlo. La función
transfiere la propiedad del value, así que serás responsable de liberar el value si fue copiado en el nodo cuando lo pusiste.
La función adquiere read lock si thread safe está habilitado.

La complejidad de tiempo para hacer poll a un nodo es O(1).
```
pgmoneta_deque_add(deque, "Hello", (uintptr_t)"world", ValueString);
char* tag = NULL;
char* value = (char*)pgmoneta_deque_poll(deque, &tag);

printf("%s, %s!\n", tag, value) // "Hello, world!"

// recuerda liberarlos!
free(tag);
free(value);
```

```
// si no te importa el tag
pgmoneta_deque_add(deque, "Hello", (uintptr_t)"world", ValueString);
char* value = (char*)pgmoneta_deque_poll(deque, NULL);

printf("%s!\n", value) // "world!"

// recuerda liberarlo!
free(value);
```

**pgmoneta_deque_poll_last**

Recupera value y remueve el nodo de la cola del deque. Si el nodo tiene tag, puedes opcionalmente leerlo. La función
transfiere la propiedad del value, así que serás responsable de liberar el value si fue copiado en el nodo cuando lo pusiste.
La función adquiere read lock si thread safe está habilitado.

La complejidad de tiempo para hacer poll a un nodo es O(1).
```
pgmoneta_deque_add(deque, "Hello", (uintptr_t)"world", ValueString);
char* tag = NULL;
char* value = (char*)pgmoneta_deque_poll_last(deque, &tag);

printf("%s, %s!\n", tag, value) // "Hello, world!"

// recuerda liberarlos!
free(tag);
free(value);
```

```
// si no te importa el tag
pgmoneta_deque_add(deque, "Hello", (uintptr_t)"world", ValueString);
char* value = (char*)pgmoneta_deque_poll_last(deque, NULL);

printf("%s!\n", value) // "world!"

// recuerda liberarlo!
free(value);
```

**pgmoneta_deque_peek**

Recupera value sin remover el nodo de la cabeza del deque. La función adquiere read lock
si thread safe está habilitado.

La complejidad de tiempo para hacer peek a un nodo es O(1).

**pgmoneta_deque_peek_last**

Recupera value sin remover el nodo de la cola del deque. La función adquiere read lock
si thread safe está habilitado.

La complejidad de tiempo para hacer peek a un nodo es O(1).

**pgmoneta_deque_iterator_create**

Crea un deque iterator, ten en cuenta que el iterator **NO** es thread safe

**pgmoneta_deque_iterator_destroy**

Destruye un deque iterator

**pgmoneta_deque_iterator_next**

Avanza el iterator al siguiente value. Necesitarás llamar esto antes de leer el primer item.
La función es un no-op si alcanza el final y retornará false.

**pgmoneta_deque_iterator_has_next**

Verifica si el iterator tiene siguiente value sin avanzarlo.

**pgmoneta_deque_iterator_remove**

Remueve el nodo actual al cual el iterator está apuntando. Entonces el iterator retrocederá al nodo anterior.

Por ejemplo, para un deque `a -> b -> c`, después de remover el nodo `b`, el iterator apuntará a `a`,
entonces llamar `pgmoneta_deque_iterator_next` avanzará el iterator a `c`. Si el nodo `a` es removido en su lugar,
el iterator apuntará al internal dummy head node.

```
// remover nodos sin un tag
pgmoneta_deque_iterator_create(deque, &iter);
while (pgmoneta_deque_iterator_next(iter)) {
    if (iter->tag == NULL) {
        pgmoneta_deque_iterator_remove(iter);
    }
    else {
        printf("%s: %s\n", iter->tag, (char*)pgmoneta_value_data(iter->value));
    }
}
pgmoneta_deque_iterator_destroy(iter);
```

**pgmoneta_deque_size**

Obtiene el tamaño actual del deque, la función adquiere el read lock

**pgmoneta_deque_empty**

Verifica si el deque está vacío

**pgmoneta_deque_to_string**

Convierte el deque a string del formato especificado.

**pgmoneta_deque_list**

Registra el contenido del deque en logs. Esto solo funciona en nivel de log TRACE.

**pgmoneta_deque_sort**

Merge sort el deque. La complejidad de tiempo es O(log(n)).

**pgmoneta_deque_get**

Obtiene el dato con un tag específico desde el deque.

La complejidad de tiempo para obtener un nodo es O(n).

**pgmoneta_deque_exists**

Verifica si un tag existe en el deque.

**pgmoneta_deque_remove**

Remueve todos los nodos en el deque que tienen el tag dado.

**pgmoneta_deque_clear**

Remueve todos los nodos en el deque.

**pgmoneta_deque_set_thread_safe**

Establece el deque para ser thread safe.

### Adaptive Radix Tree (ART)

ART comparte ideas similares como trie. Pero es muy eficiente en espacio adoptando técnicas como
adaptive node size, path compression y lazy expansion. La complejidad de tiempo de insertar, deletear o buscar una key
en un ART es siempre O(k) donde k es la longitud de la key. Y puesto que la mayoría del tiempo nuestro tipo de key es string, ART puede
ser usado como **un ideal key-value map** con mucho menos overhead de espacio que hashmap.

ART está definido e implementado en [art.h][art_h] y [art.c][art_c].

**APIs**

**pgmoneta_art_create**

Crea un adaptive radix tree

**pgmoneta_art_insert**

Inserta un par key-value en el ART. De igual forma, el árbol ART encapsula los datos en value internamente. Así que necesitas castear el value a
`uintptr_t`. Si la key ya existe, el value anterior será destruido y reemplazado por el nuevo value.

**pgmoneta_art_insert_with_config**

Inserta un par key-value con una configuración personalizada. La idea y uso es idéntico a `pgmoneta_deque_add_with_config`.

**pgmoneta_art_contains_key**

Verifica si una key existe en ART.

**pgmoneta_art_search**

Busca un value dentro del ART por su key. El ART desenvuelve el value y retorna los datos crudos. Si la key no se encuentra, retorna 0.
Así que si necesitas decir si retorna un valor cero o la key no existe, usa `pgmoneta_art_contains_key`.

**pgmoneta_art_search_typed**

Busca un value dentro del ART por su key. El ART desenvuelve el value y retorna los datos crudos.
También retorna el tipo de value a través del parámetro de salida `type`.
Si la key no se encuentra, retorna 0, y el tipo se establece a `ValueNone`. Así que también puedes usarlo para decir si un value existe.

**pgmoneta_art_delete**

Deleta una key del ART. Ten en cuenta que la función retorna success (es decir, 0) incluso si la key no existe.

**pgmoneta_art_clear**

Remueve todos los pares key-value en el árbol ART.

**pgmoneta_art_to_string**

Convierte un ART a string. La función usa una función interna de iterator que itera el árbol usando DFS.
Así que a diferencia del iterator, esto recorre e imprime keys en orden lexicográfico.

**pgmoneta_art_destroy**

Destruye un ART.

**pgmoneta_art_iterator_create**

Crea un ART iterator, el iterator itera el árbol usando BFS, lo cual significa que no recorre las keys en orden lexicográfico.

**pgmoneta_art_iterator_destroy**

Destruye un ART iterator. Esto destruirá recursivamente todos sus entradas key-value.

**pgmoneta_art_iterator_remove**

Remueve el par key-value al cual el iterator apunta. Ten en cuenta que actualmente la función solo invoca `pgmoneta_art_delete()`
con la key actual. Puesto que no hay mecanismo de rebalance en ART, no debería afectar la iteración subsecuente. Pero aún así
úsalo con cautela, puesto que esto no ha sido exhaustivamente testeado.

**pgmoneta_art_iterator_next**

Avanza un ART iterator. Necesitas llamar esta función antes de inspeccionar la primera entrada.
Si no hay más entradas, la función es un no-op y retornará false.

**pgmoneta_art_iterator_has_next**

Verifica si el iterator tiene siguiente value sin avanzarlo.

```
pgmoneta_art_iterator_create(t, &iter);
while (pgmoneta_art_iterator_next(iter)) {
    printf("%s: %s\n", iter->key, (char*)pgmoneta_value_data(iter->value));
}
pgmoneta_art_iterator_destroy(iter);
```

### JSON

JSON es esencialmente construido sobre deque y ART. Encuentra su definición e implementación en
[json.h][json_h] y [json.c][json_c].

Ten en cuenta que este documento no cubrirá las APIs de parsing iterativo, puesto que esas aún son experimentales.

**APIs**

**pgmoneta_json_create**

Crea un objeto JSON. Ten en cuenta que el json podría ser un array (`JSONArray`) o pares key-value (`JSONItem`). No especificamos el tipo JSON
en la creación. El objeto json decidirá por sí mismo basándose en la invocación de API subsecuente.

**pgmoneta_json_destroy**

Destruye un objeto JSON

**pgmoneta_json_put**

Pone un par key-value en el objeto json. Esta función invoca `pgmoneta_art_insert` por debajo así que anulará
el valor antiguo si la key ya existe. También cuando es invocado por primera vez, la función establece el objeto JSON a `JSONItem`,
el cual rechazará `pgmoneta_json_append` de entonces en adelante. Ten en cuenta que a diferencia de ART, JSON solo toma ciertos tipos de value. Ver [introducción JSON](https://www.json.org/json-en.html)
para detalles.

**pgmoneta_json_append**

Añade una entrada de value al objeto json. Cuando es invocado por primera vez, la función establece el objeto JSON a `JSONArray`, el cual
rechazará `pgmoneta_json_put` de entonces en adelante.

**pgmoneta_json_remove**

Remueve una key y destruye el value asociado dentro del json item. Si la key no existe o el objeto json
es un array, la función será un no-op. Si el JSON item se vuelve vacío después de la remoción, retrocederá al estado indefinido,
y puedes convertirlo en un array añadiendo entradas a él.

**pgmoneta_json_clear**

Para `JSONArray`, la función remueve todas las entradas. Para `JSONItem`, la función remueve todos los pares key-value.
El objeto JSON retrocederá al estado indefinido.

**pgmoneta_json_get**

Obtiene y desenvuelve los datos del value desde un JSON item. Si el objeto JSON es un array, la función retorna 0.

**pgmoneta_json_get_typed**

Obtiene y desenvuelve los datos del value desde un JSON item, también retorna el tipo de value a través del parámetro de salida `type`.
Si el objeto JSON es un array, la función retorna 0. Si la key no se encuentra, la función establece `type` a `ValueNone`.
Así que también puedes usarlo para verificar si una key existe.

**pgmoneta_json_contains_key**

Verifica si el JSON item contiene una key específica. Siempre retorna false si el objeto es un array.

**pgmoneta_json_array_length**

Obtiene la longitud de un JSON array

**pgmoneta_json_iterator_create**

Crea un JSON iterator. Para JSON array, crea un internal deque iterator. Para JSON item,
crea un internal ART iterator. Puedes leer el value o la entrada del array desde el campo `value`. Y el campo `key`
se ignora cuando el objeto es un array.

**pgmoneta_json_iterator_next**

Avanza a la siguiente entrada o pares key-value. Necesitas llamar esto antes de acceder a la primera entrada o kv pair.

**pgmoneta_json_iterator_has_next**

Verifica si el objeto tiene la siguiente entrada o par key-value.

**pgmoneta_json_iterator_destroy**

Destruye el JSON iterator.

**pgmoneta_json_parse_string**

Parsea una string JSON en un objeto JSON.

**pgmoneta_json_clone**

Clona un objeto JSON. Esto funciona convirtiendo el objeto a string y parseándolo
de vuelta a otro objeto. Así que el tipo de value podría ser un poco diferente. Por ejemplo,
un value `int8` será parseado en un value `int64`.

**pgmoneta_json_to_string**

Convierte el objeto JSON a string.

**pgmoneta_json_print**

Un convenient wrapper para imprimir rápidamente el objeto JSON.

**pgmoneta_json_read_file**

Lee el archivo JSON y lo parsea en el objeto JSON.

**pgmoneta_json_write_file**

Convierte el JSON a string y lo escribe en un archivo JSON.
