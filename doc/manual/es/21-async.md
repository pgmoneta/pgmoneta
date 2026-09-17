\newpage

# Operaciones asíncronas

El cliente `pgmoneta-cli` puede iniciar determinadas operaciones de larga
duración sin esperar a que terminen en la conexión de administración. El
servidor devuelve inmediatamente un identificador de trabajo que se puede usar
para consultar la operación.

La ejecución asíncrona es opcional. Sin `--async`, los comandos conservan su
comportamiento bloqueante habitual.

## Operaciones compatibles

Los siguientes comandos admiten `--async`:

* `backup`
* `restore`
* `archive`
* `delete`

Por ejemplo:

``` sh
pgmoneta-cli --async backup primary
pgmoneta-cli --async backup primary newest
pgmoneta-cli --async restore primary newest current /tmp/restore
pgmoneta-cli --async archive primary newest current /tmp/archive
pgmoneta-cli --async delete primary oldest
```

La respuesta inicial contiene un identificador como
`s0-backup-20260826143022`. Guarda este identificador para consultar el trabajo
más tarde.

Solo puede ejecutarse una operación de repositorio por servidor a la vez. Una
operación asíncrona no evita esta restricción.

## Ciclo de vida

Un trabajo puede tener uno de los siguientes estados:

* `Starting`: la solicitud fue aceptada y la operación se está iniciando
* `Running`: la operación está en ejecución
* `Completed`: la operación finalizó correctamente
* `Failed`: la operación no finalizó correctamente

Los trabajos completados y fallidos se guardan para poder consultarlos después.
La cancelación de trabajos no está disponible.

## Consultar un trabajo

``` sh
pgmoneta-cli job <job_id>
pgmoneta-cli job status <server> <backup|restore|archive|delete>
```

El segundo comando devuelve el trabajo actual de la operación o el último
trabajo guardado cuando no hay una operación correspondiente en ejecución.

Cuando el seguimiento de progreso está habilitado para el
servidor, la respuesta de un trabajo en ejecución también contiene los campos
en vivo `ProgressState`, `Done`, `Total`, `Elapsed`, `Percentage` y `Remaining`.
Estos campos no se guardan con los registros de trabajos completados o fallidos.

## Listar trabajos

``` sh
pgmoneta-cli job list all
pgmoneta-cli job list server primary
pgmoneta-cli job list status Running
pgmoneta-cli job list status Completed
pgmoneta-cli job list status Failed
```

## Eliminar trabajos guardados

``` sh
pgmoneta-cli job remove <job_id>
pgmoneta-cli job remove all
```

No se puede eliminar un trabajo activo. Eliminar un trabajo borra su registro;
no elimina un backup ni deshace una operación completada.
