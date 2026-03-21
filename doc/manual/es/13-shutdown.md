\newpage

# Apagar

Puedes probar el estado de [**pgmoneta**][pgmoneta] y apagar ya sea localmente o desde una máquina remota.

## Ping

Puedes verificar si [**pgmoneta**][pgmoneta] está en ejecución usando:

```
pgmoneta-cli ping
```

## mode

[**pgmoneta**][pgmoneta] detecta cuando un servidor está caído. Puedes poner un servidor en línea u offline
usando el comando mode:

```
pgmoneta-cli mode primary offline
```

o

```
pgmoneta-cli mode primary online
```

[**pgmoneta**][pgmoneta] mantendrá servicios básicos en ejecución para un servidor offline de manera que
puedas verificar un backup o hacer una restauración.

## shutdown

Puedes apagar [**pgmoneta**][pgmoneta] con:

```
pgmoneta-cli shutdown
```