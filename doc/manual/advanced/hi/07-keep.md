\newpage

### बैकअप रखना (Keeping backups)

#### बैकअप्स की सूची (List backups)

पहले, हम अपने वर्तमान बैकअप्स की सूची निम्नलिखित कमांड का उपयोग करके देख सकते हैं:

```
pgmoneta-cli list-backup primary
```

आपको इस प्रकार का आउटपुट मिलेगा:

```
Header: 
  ClientVersion: 0.16.0
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
  ServerVersion: 0.16.0
```

जैसा कि आप देख सकते हैं, बैकअप `20241012091219` में `Keep` फ्लैग `false` है।

#### बैकअप को रखना (Keep a backup)

अब, यदि आप बैकअप को रखना चाहते हैं, ताकि वह रिटेंशन पॉलिसी द्वारा डिलीट न हो, तो आप निम्नलिखित कमांड का उपयोग कर सकते हैं:

```
pgmoneta-cli retain primary 20241012091219
```

और आपको इस प्रकार का आउटपुट मिलेगा:

```
Header: 
  ClientVersion: 0.16.0
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
  Backup: 20241012091219
  Comments: ''
  Compression: none
  Encryption: none
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.16.0
  Valid: yes
```

अब आप देख सकते हैं कि बैकअप का `Keep` फ्लैग `true` हो गया है।

#### बैकअप का वर्णन करना (Describe a backup)

अब, आप अपने बैकअप में विवरण जोड़ना चाह सकते हैं। जैसा कि आप देख सकते हैं:

```
Header: 
  ClientVersion: 0.16.0
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
  Backup: 20241012091219
  Comments: ''
  Compression: none
  Encryption: none
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.16.0
  Valid: yes
```

यहां एक `Comments` फील्ड है जहाँ आप विवरण जोड़ सकते हैं।

आप कमांड का उपयोग कर सकते हैं:

```
pgmoneta-cli annotate primary 20241012091219 add Type "Main fall backup"
```

जिससे आपको निम्नलिखित आउटपुट मिलेगा:

```
Header: 
  ClientVersion: 0.16.0
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
  ServerVersion: 0.16.0
  StartHiLSN: 0
  StartLoLSN: 33554472
  StartTimeline: 1
  Tablespaces: 
  Valid: yes
  WAL: 000000010000000000000002
```

जैसा कि आप देख सकते हैं, `Comments` फील्ड में `Type` कुंजी के साथ विवरण है।

`annotate` कमांड में `add`, `update`, और `remove` कमांड्स होते हैं, जो `Comments` फील्ड को संशोधित करने के लिए उपयोग किए जाते हैं।

#### बैकअप को फिर से रिटेंशन में डालना (Put a backup back into retention)

जब आपको किसी बैकअप की अब आवश्यकता न हो, तो आप उसे पुनः रिटेंशन में डाल सकते हैं:

```
pgmoneta-cli expunge primary 20241012091219
```

जिससे आपको निम्नलिखित आउटपुट मिलेगा:

```
Header: 
  ClientVersion: 0.16.0
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
  ServerVersion: 0.16.0
  Valid: yes
```

अब, `Keep` फ्लैग फिर से `false` हो गया है।