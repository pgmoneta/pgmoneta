\newpage

# [पुनर्स्थापना]{lang=hi}

## [बैकअप पुनर्स्थापित करना]{lang=hi}

[हम निम्नलिखित कमांड का उपयोग करके प्राथमिक से बैकअप पुनर्स्थापित कर सकते हैं:]{lang=hi}

```
pgmoneta-cli restore primary newest current /tmp
```

[जहां:]{lang=hi}

* `current` [का मतलब है]{lang=hi} Write-Ahead Log (WAL) [की कॉपी करना और पहले स्थिर चेकपॉइंट पर पुनर्स्थापना करना।]{lang=hi}
* `name=X` [का मतलब है]{lang=hi} Write-Ahead Log (WAL) [की कॉपी करना और निर्दिष्ट लेबल पर पुनर्स्थापना करना।]{lang=hi}
* `xid=X` [का मतलब है]{lang=hi} Write-Ahead Log (WAL) [की कॉपी करना और निर्दिष्ट]{lang=hi} XID [पर पुनर्स्थापना करना।]{lang=hi}
* `time=X` [का मतलब है]{lang=hi} Write-Ahead Log (WAL) [की कॉपी करना और निर्दिष्ट टाइमस्टैम्प पर पुनर्स्थापना करना।]{lang=hi}
* `lsn=X` [का मतलब है]{lang=hi} Write-Ahead Log (WAL) [की कॉपी करना और निर्दिष्ट]{lang=hi} Log Sequence Number (LSN) [पर पुनर्स्थापना करना।]{lang=hi}
* `inclusive=X` [का मतलब है कि पुनर्स्थापना निर्दिष्ट जानकारी को शामिल करती है।]{lang=hi}
* `timeline=X` [का मतलब है कि पुनर्स्थापना निर्दिष्ट जानकारी टाइमलाइन पर की जाती है।]{lang=hi}
* `action=X` [का मतलब है पुनर्स्थापना के बाद कौन सा क्रिया निष्पादित की जानी चाहिए ।]{lang=hi}(pause, shutdown)
* `primary` [का मतलब है कि क्लस्टर प्राथमिक के रूप में सेटअप है।]{lang=hi}
* `replica` [का मतलब है कि क्लस्टर को एक प्रतिकृति के रूप में सेटअप किया गया है।]{lang=hi}

[More information](https://www.postgresql.org/docs/current/runtime-config-wal.html#RUNTIME-CONFIG-WAL-RECOVERY-TARGET)

[इसके बाद, आपको इस प्रकार का आउटपुट मिलेगा:]{lang=hi}

```
Header: 
  ClientVersion: 0.18.1
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
  ServerVersion: 0.18.1
```

[यह कमांड नवीनतम बैकअप और सभी]{lang=hi} Write-Ahead Log (WAL) [सेगमेंट्स को लेकर]{lang=hi} `/tmp/primary-20240928065644` [डायरेक्टरी में एक अद्यतन प्रति पुनर्स्थापित करेगा।]{lang=hi}
