\newpage

# Manteniendo backups

## Listar backups

Primero, podemos listar nuestros backups actuales usando

```
pgmoneta-cli list-backup primary
```

obtendrás el siguiente output:

```
Header:
  ClientVersion: 0.22.0
  Command: list-backup
  Output: text
  Timestamp: 20241018092853
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Server: primary
Response:
  Backups:
    - Backup: 20241012091219
      BackupSize: 6.11MB
      Comments: ''
      Compression: zstd
      Encryption: none
      Keep: false
      RestoreSize: 39.13MB
      Server: primary
      Valid: yes
      WAL: 0
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.22.0
```

Como puedes ver, el backup `20241012091219` tiene una flag `Keep` marcada como `false`.

## Mantener un backup

Ahora, para mantener el backup lo que significa que no será eliminado por la política de retención
puedes emitir el siguiente comando,

```
pgmoneta-cli retain primary 20241012091219
```

obtendrás el siguiente output:

```
Header:
  ClientVersion: 0.22.0
  Command: retain
  Output: text
  Timestamp: 20241018094129
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Backup: 20241012091219
  Server: primary
Response:
  Backups:
    - 20241012091219
  Cascade: false
  Comments: ''
  Compression: none
  Encryption: none
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.22.0
  Valid: yes
```

y puedes ver que el backup tiene una flag `Keep` marcada como `true`.

## Describir un backup

Ahora, es posible que desees agregar una descripción a tu backup, y como puedes ver

```
Header:
  ClientVersion: 0.22.0
  Command: retain
  Output: text
  Timestamp: 20241018094129
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Backup: 20241012091219
  Server: primary
Response:
  Backups:
    - 20241012091219
  Cascade: false  
  Comments: ''
  Compression: none
  Encryption: none
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.22.0
  Valid: yes
```

hay un campo `Comments` para hacer eso.

Puedes usar el comando,

```
pgmoneta-cli annotate primary 20241012091219 add Type "Main fall backup"
```

que dará

```
Header:
  ClientVersion: 0.22.0
  Command: annotate
  Output: text
  Timestamp: 20241018095906
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Action: add
  Backup: 20241012091219
  Comment: Main fall backup
  Key: Type
  Server: primary
Response:
  Backup: 20241012091219
  BackupSize: 6.11MB
  CheckpointHiLSN: 0
  CheckpointLoLSN: 33554560
  Comments: Type|Main fall backup
  Compression: zstd
  Elapsed: 1
  Encryption: none
  EndHiLSN: 0
  EndLoLSN: 33554776
  EndTimeline: 1
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  NumberOfTablespaces: 0
  RestoreSize: 39.13MB
  Server: primary
  ServerVersion: 0.22.0
  StartHiLSN: 0
  StartLoLSN: 33554472
  StartTimeline: 1
  Tablespaces:

  Valid: yes
  WAL: 000000010000000000000002
```

Como puedes ver el campo `Comments` con la clave `Type`.

El comando `annotate` tiene subcomandos `add`, `update` y `remove` para modificar el campo `Comments`.

## Volver a poner un backup en retención

Cuando ya no necesites un backup puedes volverlo a poner en retención con,

```
pgmoneta-cli expunge primary 20241012091219
```

dará,

```
Header:
  ClientVersion: 0.22.0
  Command: expunge
  Output: text
  Timestamp: 20241018101839
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Backup: 20241012091219
  Server: primary
Response:
  Backup: 20241012091219
  Comments: Type|Main fall backup
  Compression: none
  Encryption: none
  Keep: false
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.22.0
  Valid: yes
```

y ahora, la flag `Keep` vuelve a estar en `false`.

## Modo Cascade
Puedes mantener/expurgar toda la cadena de backups incrementales usando la opción `--cascade`. 
Esto mantendrá/expurgará todos los backups en la línea hasta el backup completo raíz.

Digamos que tienes una cadena de backups incrementales: `20250625055547 (incremental)` -> `20250625055528 (incremental)` -> `20250625055517(full)`.
Ejecutar `pgmoneta-cli retain --cascade primary 20250625055547` también mantendrá el backup `20250625055528` y el backup `20250625055517`.

Esto dará
```
Header:
  ClientVersion: 0.22.0
  Command: retain
  Compression: none
  Encryption: none
  Output: text
  Timestamp: 20250625055654
Outcome:
  Status: true
  Time: 00:00:0.1032
Request:
  Backup: newest
  Cascade: true
  Server: primary
Response:
  BackupSize: 0.00B
  Backups:
    - 20250625055547
    - 20250625055528
    - 20250625055517
  BiggestFileSize: 0.00B
  Cascade: true
  Comments: ''
  Compression: none
  Delta: 0.00B
  Encryption: none
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 0.00B
  Server: primary
  ServerVersion: 0.22.0
  Valid: yes
  WAL: 0.00B
```
