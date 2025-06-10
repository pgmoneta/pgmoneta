\newpage

# Shutdown

You can test the status of [**pgmoneta**](pgmoneta) and shutdown either locally or from a remote machine.

## ping

You can check if [**pgmoneta**](pgmoneta) is running by

```
pgmoneta-cli ping
```

and check the output.

## mode

[**pgmoneta**](pgmoneta) detects when a server is down. You can bring a server online or offline
using the mode command, like

```
pgmoneta-cli mode primary offline
```

or

```
pgmoneta-cli mode primary online
```

[**pgmoneta**](pgmoneta) will keep basic services running for an offline server such that
you can verify a backup or do a restore.

## shutdown

You can shutdown [**pgmoneta**](pgmoneta) by

```
pgmoneta-cli shutdown
```

and check the output.
