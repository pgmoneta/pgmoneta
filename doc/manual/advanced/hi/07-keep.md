\newpage

# [बैकअप रखना]{lang=hi}

## [बैकअप्स की सूची]{lang=hi}

[पहले, हम अपने वर्तमान बैकअप्स की सूची निम्नलिखित कमांड का उपयोग करके देख सकते हैं:]{lang=hi}

```
pgmoneta-cli list-backup primary
```

[आपको इस प्रकार का आउटपुट मिलेगा:]{lang=hi}

```
Header: 
  ClientVersion: 0.17.3
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
  ServerVersion: 0.17.3
```

[जैसा कि आप देख सकते हैं, बैकअप]{lang=hi} `20241012091219` [में]{lang=hi} `Keep` [फ्लैग]{lang=hi} `false` [है।]{lang=hi}

## [बैकअप को रखना]{lang=hi}

[अब, यदि आप बैकअप को रखना चाहते हैं, ताकि वह रिटेंशन पॉलिसी द्वारा डिलीट न हो, तो आप निम्नलिखित कमांड का उपयोग कर सकते हैं:]{lang=hi}

```
pgmoneta-cli retain primary 20241012091219
```

[और आपको इस प्रकार का आउटपुट मिलेगा:]{lang=hi}

```
Header: 
  ClientVersion: 0.17.3
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
  ServerVersion: 0.17.3
  Valid: yes
```

[अब आप देख सकते हैं कि बैकअप का]{lang=hi} `Keep` [फ्लैग]{lang=hi} `true` [हो गया है।]{lang=hi}

[ध्यान दें कि]{lang=hi} `Keep` [वर्तमान में केवल पूर्ण बैकअप पर कार्य करता है। हम भविष्य के संस्करणों में वृद्धिशील बैकअप को बनाए रखने का समर्थन करेंगे।]{lang=hi}

## [बैकअप का वर्णन करना]{lang=hi}

[अब, आप अपने बैकअप में विवरण जोड़ना चाह सकते हैं। जैसा कि आप देख सकते हैं:]{lang=hi}

```
Header: 
  ClientVersion: 0.17.3
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
  ServerVersion: 0.17.3
  Valid: yes
```

[यहां एक]{lang=hi} `Comments` [फील्ड है जहाँ आप विवरण जोड़ सकते हैं।]{lang=hi}

[आप कमांड का उपयोग कर सकते हैं:]{lang=hi}

```
pgmoneta-cli annotate primary 20241012091219 add Type "Main fall backup"
```

[जिससे आपको निम्नलिखित आउटपुट मिलेगा:]{lang=hi}

```
Header: 
  ClientVersion: 0.17.3
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
  ServerVersion: 0.17.3
  StartHiLSN: 0
  StartLoLSN: 33554472
  StartTimeline: 1
  Tablespaces: 
  Valid: yes
  WAL: 000000010000000000000002
```

[जैसा कि आप देख सकते हैं,]{lang=hi} `Comments` [फील्ड में]{lang=hi} `Type` [कुंजी के साथ विवरण है।]{lang=hi}

`annotate` [कमांड में]{lang=hi} `add`, `update`, [और]{lang=hi} `remove` [कमांड्स होते हैं, जो]{lang=hi} `Comments` [फील्ड को संशोधित करने के लिए उपयोग किए जाते हैं।]{lang=hi}

## [बैकअप को फिर से रिटेंशन में डालना]{lang=hi}

[जब आपको किसी बैकअप की अब आवश्यकता न हो, तो आप उसे पुनः रिटेंशन में डाल सकते हैं:]{lang=hi}

```
pgmoneta-cli expunge primary 20241012091219
```

[जिससे आपको निम्नलिखित आउटपुट मिलेगा:]{lang=hi}

```
Header: 
  ClientVersion: 0.17.3
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
  ServerVersion: 0.17.3
  Valid: yes
```

[अब,]{lang=hi} `Keep` [फ्लैग फिर से]{lang=hi} `false` [हो गया है।]{lang=hi}
