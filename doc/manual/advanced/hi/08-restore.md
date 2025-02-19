### पुनर्स्थापना (Restore)

#### बैकअप पुनर्स्थापित करना (Restore a backup)

हम निम्नलिखित कमांड का उपयोग करके प्राथमिक से बैकअप पुनर्स्थापित कर सकते हैं:

```
pgmoneta-cli restore primary newest current /tmp
```

जहां:

* `current` का मतलब है Write-Ahead Log (WAL) की कॉपी करना और पहले स्थिर चेकपॉइंट पर पुनर्स्थापना करना।
* `name=X` का मतलब है Write-Ahead Log (WAL) की कॉपी करना और निर्दिष्ट लेबल पर पुनर्स्थापना करना।
* `xid=X` का मतलब है Write-Ahead Log (WAL) की कॉपी करना और निर्दिष्ट XID पर पुनर्स्थापना करना।
* `time=X` का मतलब है Write-Ahead Log (WAL) की कॉपी करना और निर्दिष्ट टाइमस्टैम्प पर पुनर्स्थापना करना।
* `lsn=X` का मतलब है Write-Ahead Log (WAL) की कॉपी करना और निर्दिष्ट Log Sequence Number (LSN) पर पुनर्स्थापना करना।
* `inclusive=X` का मतलब है कि पुनर्स्थापना निर्दिष्ट जानकारी को शामिल करती है।
* `timeline=X` का मतलब है कि पुनर्स्थापना निर्दिष्ट जानकारी टाइमलाइन पर की जाती है।
* `action=X` का मतलब है पुनर्स्थापना के बाद कौन सा क्रिया निष्पादित की जानी चाहिए (pause, shutdown)।
* `primary` का मतलब है कि क्लस्टर प्राथमिक के रूप में सेटअप है।
* `replica` का मतलब है कि क्लस्टर को एक प्रतिकृति के रूप में सेटअप किया गया है।

[अधिक जानकारी](https://www.postgresql.org/docs/current/runtime-config-wal.html#RUNTIME-CONFIG-WAL-RECOVERY-TARGET)

इसके बाद, आपको इस प्रकार का आउटपुट मिलेगा:

```
Header: 
  ClientVersion: 0.16.0
  Command: 3
  Output: 0
  Timestamp: 20240928130406
Outcome: 
  Status: true
  Time: 00:00:00
Request: 
  Backup: newest
  Directory: /tmp
  Position: current
  Server: primary
Response: 
  Backup: 20240928065644
  BackupSize: 8531968
  Comments: ''
  Compression: 2
  Encryption: 0
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 48799744
  Server: primary
  ServerVersion: 0.16.0
```

यह कमांड नवीनतम बैकअप और सभी Write-Ahead Log (WAL) सेगमेंट्स को लेकर `/tmp/primary-20240928065644` डायरेक्टरी में एक अद्यतन प्रति पुनर्स्थापित करेगा।