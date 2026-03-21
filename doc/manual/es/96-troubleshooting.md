\newpage

# Resolución de problemas

## No se pudo obtener la versión del servidor

Si obtienes este `FATAL` durante el inicio, verifica tus inicios de sesión en PostgreSQL

```
psql postgres
```

y

```
psql -U repl postgres
```

Y, verifica los registros de PostgreSQL para cualquier error.

Establecer `log_level` a `DEBUG5` en `pgmoneta.conf` podría proporcionar más información sobre el error.
