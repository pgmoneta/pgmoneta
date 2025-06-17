\newpage

# [बैकअप]{lang=hi}

## [पूर्ण बैकअप बनाएं]{lang=hi}

[हम निम्नलिखित कमांड का उपयोग करके प्राइमरी से पूर्ण बैकअप ले सकते हैं:]{lang=hi}

```
pgmoneta-cli backup primary
```

[और आपको कुछ इस प्रकार का आउटपुट मिलेगा:]{lang=hi}

```
Header: 
  ClientVersion: 0.18.0
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
  ServerVersion: 0.18.0
```

## [बैकअप देखना]{lang=hi}

[हम निम्नलिखित कमांड के साथ एक सर्वर के सभी बैकअप्स को सूचीबद्ध कर सकते हैं:]{lang=hi}

```
pgmoneta-cli list-backup primary
```

[और आपको कुछ इस प्रकार का आउटपुट मिलेगा:]{lang=hi}

```
Header: 
  ClientVersion: 0.18.0
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
      Incremental: false
      Keep: false
      RestoreSize: 48799744
      Server: primary
      Valid: 1
      WAL: 0
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.18.0
```

## [वृद्धिशील बैकअप बनाएं]{lang=hi}

[हम निम्नलिखित कमांड का उपयोग करके प्राइमरी से वृद्धिशील]{lang=hi} (incremental) [बैकअप ले सकते हैं:]{lang=hi}

```
pgmoneta-cli backup primary 20240928065644
```

[और आपको निम्नलिखित आउटपुट मिलेगा:]{lang=hi}

```
Header: 
  ClientVersion: 0.18.0
  Command: 1
  Output: 0
  Timestamp: 20240928065730
Outcome: 
  Status: true
  Time: 00:00:20
Request: 
  Server: primary
Response: 
  Backup: 20240928065750
  BackupSize: 124312
  Compression: 2
  Encryption: 0
  Incremental: true
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 48799744
  Server: primary
  ServerVersion: 0.18.0
```

[वृद्धिशील बैकअप केवल]{lang=hi} [PostgreSQL 17+](https://www.postgresql.org) [में समर्थित हैं।]{lang=hi} 
[ध्यान दें कि वर्तमान में वृद्धिशील बैकअप के लिए शाखाएं ]{lang=hi} (branching) [अनुमति नहीं दी गई हैं — एक बैकअप में अधिकतम 1 वृद्धिशील बैकअप चाइल्ड हो सकता है।]{lang=hi}


## [बैकअप देखें]{lang=hi}

[हम किसी सर्वर के लिए सभी बैकअप को सूचीबद्ध करने के लिए निम्नलिखित कमांड का उपयोग कर सकते हैं:]{lang=hi}

```
pgmoneta-cli list-backup primary
```

[और आपको निम्नलिखित आउटपुट मिलेगा:]{lang=hi}

```
Header: 
  ClientVersion: 0.18.0
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
      Incremental: false
      Keep: false
      RestoreSize: 48799744
      Server: primary
      Valid: 1
      WAL: 0
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.18.0
```

## [बैकअप जानकारी]{lang=hi}

[आप बैकअप के बारे में जानकारी सूचीबद्ध कर सकते हैं:]{lang=hi}

```
pgmoneta-cli -c pgmoneta.conf info primary newest
```

[और आपको कुछ इस प्रकार का आउटपुट मिलेगा:]{lang=hi}

```
Header:
  ClientVersion: 0.18.0
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
  Incremental: false
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  NumberOfTablespaces: 0
  RestoreSize: 45.82MB
  Server: primary
  ServerVersion: 0.18.0
  StartHiLSN: 0
  StartLoLSN: 4F000060
  StartTimeline: 1
  Tablespaces: {}
  Valid: yes
  WAL: 00000001000000000000004F
```

## [क्रॉन्टैब बनाना]{lang=hi}

[आइए हम एक]{lang=hi} `crontab` [बनाते हैं ताकि हर दिन बैकअप लिया जा सके:]{lang=hi}

```
crontab -e
```

[और निम्नलिखित को जोड़ें:]{lang=hi}

```
0 6 * * * pgmoneta-cli backup primary
```

[यह हर दिन सुबह 6 बजे बैकअप लेने के लिए है।]{lang=hi}

## [बैकअप अखंडता सत्यापित करें]{lang=hi}

pgmoneta [बैकअप रूट निर्देशिका में प्रत्येक बैकअप के लिए एक]{lang=hi} SHA512 [चेकसम फ़ाइल]{lang=hi} (`backup.sha512`) [बनाता है, जिसका उपयोग फ़ाइलों की अखंडता को सत्यापित करने के लिए किया जा सकता है।]{lang=hi}

`sha512sum` [का उपयोग करना:]{lang=hi}
```
cd <path-to-specific-backup-directory>

sha512sum --check backup.sha512
```

`verification` [पैरामीटर का उपयोग यह नियंत्रित करने के लिए किया जा सकता है कि]{lang=hi} pgmoneta [कितनी बार बैकअप फ़ाइलों की अखंडता को सत्यापित करता है। आप इसे]{lang=hi} `pgmoneta.conf` [में कॉन्फ़िगर कर सकते हैं:]{lang=hi}

```
[pgmoneta]
.
.
.
verification = 3600
```
[उदाहरण के लिए]{lang=hi}, `verification = 3600` [या]{lang=hi} `verification = 1H` [सेट करने पर हर घंटे अखंडता जांच की जाएगी।]{lang=hi}
