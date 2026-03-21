\newpage

# Retención

La política de retención decide por cuánto tiempo se debe mantener un backup.

## Configuración de retención

La configuración se realiza en la sección principal de configuración, o por servidor con

| Propiedad | Predeterminado | Unidad | Requerido | Descripción |
| :------- | :------ | :--- | :------- | :---------- |
| retention | 7, - , - , - | Array | No | El tiempo de retención en días, semanas, meses, años |

lo que significa que por defecto los backups se mantienen durante 7 días.

Definir una política de retención es muy importante porque define cómo podrás restaurar tu sistema
desde los backups.

La clave es decidir cuál es tu política, por ejemplo

```
7, 4, 12, 5
```

mantendrá backups durante 7 días, un backup cada lunes durante 4 semanas, un backup cada mes, y backups durante 5 años.

Hay muchas formas de dejar un parámetro sin especificar. Para parámetros finales, puedes simplemente omitirlos.
Y para parámetros intermedios, puedes usar marcadores de posición. Actualmente, los marcadores de posición permitidos son: `-`, `X`, `x`, `0`
o espacios en blanco (espacios o tabulaciones).

Si deseas restaurar desde el último backup más el Write-Ahead Log (WAL), entonces la política predeterminada de [**pgmoneta**][pgmoneta] quizá sea suficiente.

Ten en cuenta que si un backup tiene un backup incremental hijo que depende de él, sus datos se consolidarán en su hijo antes de ser eliminado.

La regla de validación actual es:

1. Retención días >= 1
2. Si se especifica retención de meses, entonces 1 <= semanas <= 4, en caso contrario semanas >= 1
3. Si se especifica retención de años, entonces 1 <= meses <= 12, en caso contrario meses >= 1
4. Retención años >= 1

Ten en cuenta que la regla anterior solo verifica parámetros especificados, excepto por días, que siempre debe especificarse.

La verificación de retención se ejecuta cada 5 minutos, y eliminará un backup por ejecución.

Puedes cambiar esto a cada 30 minutos con

```
retention_interval = 1800
```

bajo la configuración `[pgmoneta]`.

## Eliminar un backup

```
pgmoneta-cli -c pgmoneta.conf delete [--force] primary oldest
```

eliminará el backup más antiguo en `[primary]`.

Cuando se usa `--force`, el backup se elimina inmediatamente, ignorando la política de retención configurada.

Ten en cuenta que si el backup tiene un backup incremental hijo que depende de él,
sus datos se consolidarán en su hijo antes de ser eliminado.

## Write-Ahead Log shipping

Para usar WAL shipping, simplemente agrega

```
wal_shipping = your/local/wal/shipping/directory
```

a la sección del servidor correspondiente en `pgmoneta.conf`. [**pgmoneta**][pgmoneta] creará el directorio si no existe,
y enviará una copia de los segmentos WAL en el subdirectorio `your/local/wal/shipping/directory/server_name/wal`.
