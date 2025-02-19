### बैकअप (Backup)

#### बैकअप बनाना (Create backup)

हम निम्नलिखित कमांड के साथ प्राइमरी से बैकअप ले सकते हैं:

```
pgmoneta-cli backup primary
```

और आपको कुछ इस प्रकार का आउटपुट मिलेगा:

```
Header: 
  ClientVersion: 0.16.0
  Command: 1
  Output: 0
  Timestamp: 20240928065644
Outcome: 
  Status: true
  Time: 00:00:20
Request: 
  Server: primary
Response: 
  Backup: 20240928065644
  BackupSize: 8531968
  Compression: 2
  Encryption: 0
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 48799744
  Server: primary
  ServerVersion: 0.16.0
```

#### बैकअप देखना (View backups)

हम निम्नलिखित कमांड के साथ एक सर्वर के सभी बैकअप्स को सूचीबद्ध कर सकते हैं:

```
pgmoneta-cli list-backup primary
```

और आपको कुछ इस प्रकार का आउटपुट मिलेगा:

```
Header: 
  ClientVersion: 0.16.0
  Command: 2
  Output: 0
  Timestamp: 20240928065812
Outcome: 
  Status: true
  Time: 00:00:00
Request: 
  Server: primary
Response: 
  Backups: 
    - Backup: 20240928065644
      BackupSize: 8531968
      Comments: ''
      Compression: 2
      Encryption: 0
      Keep: false
      RestoreSize: 48799744
      Server: primary
      Valid: 1
      WAL: 0
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.16.0
```

#### बैकअप जानकारी (Backup information)

आप बैकअप के बारे में जानकारी सूचीबद्ध कर सकते हैं:

```
pgmoneta-cli -c pgmoneta.conf info primary newest
```

और आपको कुछ इस प्रकार का आउटपुट मिलेगा:

```
Header:
  ClientVersion: 0.16.0
  Command: info
  Output: text
  Timestamp: 20241025163541
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Backup: newest
  Server: primary
Response:
  Backup: 20241019163516
  BackupSize: 6.54MB
  CheckpointHiLSN: 0
  CheckpointLoLSN: 4F0000B8
  Comments: ''
  Compression: zstd
  Elapsed: 4
  Encryption: none
  EndHiLSN: 0
  EndLoLSN: 4F000158
  EndTimeline: 1
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  NumberOfTablespaces: 0
  RestoreSize: 45.82MB
  Server: primary
  ServerVersion: 0.16.0
  StartHiLSN: 0
  StartLoLSN: 4F000060
  StartTimeline: 1
  Tablespaces: {}
  Valid: yes
  WAL: 00000001000000000000004F
```

#### क्रॉन्टैब बनाना (Create a crontab)

आइए हम एक `crontab` बनाते हैं ताकि हर दिन बैकअप लिया जा सके:

```
crontab -e
```

और निम्नलिखित को जोड़ें:

```
0 6 * * * pgmoneta-cli backup primary
```

यह हर दिन सुबह 6 बजे बैकअप लेने के लिए है।