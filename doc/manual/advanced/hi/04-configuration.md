\newpage

# [कॉन्फ़िगरेशन]{lang=hi}

## pgmoneta.conf

[कॉन्फ़िगरेशन या तो]{lang=hi} `-c` [ध्वज द्वारा निर्दिष्ट पथ से या]{lang=hi} `/etc/pgmoneta/pgmoneta.conf` [से लोड होती है।]{lang=hi}

`pgmoneta` [की कॉन्फ़िगरेशन को]{lang=hi} `[` [और]{lang=hi} `]` [अक्षरों का उपयोग करके खंडों में विभाजित किया गया है।]{lang=hi}

[मुख्य खंड, जिसे]{lang=hi} `[pgmoneta]` [कहा जाता है, वह है जहाँ आप]{lang=hi} `pgmoneta` [की समग्र गुणों को कॉन्फ़िगर करते हैं।]{lang=hi}

[अन्य खंडों का नामकरण कोई विशेष आवश्यकता नहीं है, इसलिए आप उन्हें अर्थपूर्ण नाम दे सकते हैं, जैसे]{lang=hi} `[primary]` [मुख्य]{lang=hi} [उदाहरण के लिए।]{lang=hi} [PostgreSQL](https://www.postgresql.org)
 
[सभी गुण]{lang=hi} `key = value` [प्रारूप में होते हैं।]{lang=hi}

`#` [और]{lang=hi} `;` [का उपयोग टिप्पणियों के लिए किया जा सकता है; इन्हें पंक्ति के पहले अक्षर के रूप में होना चाहिए।]{lang=hi}
`Bool` [डेटा प्रकार निम्नलिखित मानों को समर्थित करता है:]{lang=hi} `on`, `yes`, `1`, `true`, `off`, `no`, `0` [और]{lang=hi} `false`[।]{lang=hi}

`pgmoneta` [को]{lang=hi} `localhost` [पर चलाने के लिए एक]{lang=hi} [sample](./etc/pgmoneta.conf) [कॉन्फ़िगरेशन देखें।]{lang=hi}

[ध्यान दें,]{lang=hi} PostgreSQL 13+ [आवश्यक है, साथ ही]{lang=hi} `wal_level` [को]{lang=hi} `replica` [या]{lang=hi} `logical` [स्तर पर सेट करना आवश्यक है।]{lang=hi}

### pgmoneta

#### [सामान्य]{lang=hi}

| [गुण]{lang=hi}        | [डिफ़ॉल्ट]{lang=hi} | [इकाई]{lang=hi}   | [आवश्यक]{lang=hi} | [विवरण]{lang=hi}   |
|------------|----------|--------|--------|----------|
| host       |          | String | [हाँ]{lang=hi}     | pgmoneta [के लिए बाइंड पता]{lang=hi} |
| unix_socket_dir |      | String | [हाँ]{lang=hi}     | Unix [डोमेन सॉकेट स्थान]{lang=hi} |
| base_dir   |          | String | [हाँ]{lang=hi}     | [बैकअप के लिए बेस निर्देशिका]{lang=hi} |

[ध्यान दें, यदि]{lang=hi} `host` `/` [से शुरू होता है, तो यह एक पथ को दर्शाता है और]{lang=hi} `pgmoneta` Unix [डोमेन सॉकेट का उपयोग करके कनेक्ट होगा।]{lang=hi}

#### [निगरानी]{lang=hi}

| [गुण]{lang=hi}        |      [डिफ़ॉल्ट]{lang=hi}| [इकाई]{lang=hi}   | [आवश्यक]{lang=hi} | [विवरण]{lang=hi}   |
|------------|----------|--------|--------|----------|
| metrics    | 0        | Int    | [नहीं]{lang=hi}    | [मीट्रिक्स पोर्ट (अक्षम = 0)]{lang=hi} |
| metrics_cache_max_age | 0 | String | [नहीं]{lang=hi}    | Prometheus (metrics) [प्रतिक्रिया को कैश में रखने के लिए सेकंड की संख्या। यदि शून्य पर सेट किया जाता है, तो कैशिंग अक्षम हो जाएगी। इसे एक स्ट्रिंग के रूप में सेट किया जा सकता है, जैसे]{lang=hi} `2m` [जो 2 मिनट का संकेत देता है]{lang=hi} |
| metrics_cache_max_size | 256k | String | [नहीं]{lang=hi}    | Prometheus [प्रतिक्रियाओं को सर्व करते समय कैश में रखने के लिए अधिकतम डेटा आकार। बदलाव के लिए पुनः आरंभ की आवश्यकता होती है। यह पैरामीटर तब उपयोग किया जाएगा जब]{lang=hi} `metrics_cache_max_age` [या]{lang=hi} `metrics` [अक्षम हों। इसके मान को केवल तब ध्यान में रखा जाएगा जब]{lang=hi} `metrics_cache_max_age` [शून्य से बड़ा हो। समर्थन करता है प्रत्यय:]{lang=hi} 'B' [(बाइट्स), यदि प्रत्यय न हो तो डिफ़ॉल्ट,]{lang=hi} 'K' [या]{lang=hi} 'KB' [(किलोबाइट्स),]{lang=hi} 'M' [या]{lang=hi} 'MB' [(मेगाबाइट्स),]{lang=hi} 'G' [या]{lang=hi} 'GB' [(गिगाबाइट्स)।]{lang=hi}|

#### [प्रबंधन]{lang=hi}

| [गुण]{lang=hi}        | [डिफ़ॉल्ट]{lang=hi} | [इकाई]{lang=hi}   | [आवश्यक]{lang=hi} | [विवरण]{lang=hi}   |
|------------|----------|--------|--------|----------|
| management | 0        | Int    | [नहीं]{lang=hi}    | [दूरस्थ प्रबंधन पोर्ट (अक्षम = 0)]{lang=hi} |

#### [संपीड़न]{lang=hi}

| [गुण]{lang=hi}        | [डिफ़ॉल्ट]{lang=hi} | [इकाई]{lang=hi}   | [आवश्यक]{lang=hi} | [विवरण]{lang=hi}   |
|------------|----------|--------|--------|----------|
| compression | zstd     | String | [नहीं]{lang=hi}    | [संपीड़न प्रकार]{lang=hi} (none, gzip, client-gzip, server-gzip, zstd, client-zstd, server-zstd, lz4, client-lz4, server-lz4, bzip2, client-bzip2) |
| compression_level | 3  | Int    | [नहीं]{lang=hi}    | [संपीड़न स्तर]{lang=hi} |

#### [श्रमिक]{lang=hi}

| [गुण]{lang=hi}        | [डिफ़ॉल्ट]{lang=hi} | [इकाई]{lang=hi}   | [आवश्यक]{lang=hi} | [विवरण]{lang=hi}   |
|------------|----------|--------|--------|----------|
| workers    | 0        | Int    | [नहीं]{lang=hi}    | [प्रत्येक प्रक्रिया के लिए श्रमिकों की संख्या। अक्षम करने के लिए 0 का उपयोग करें। अधिकतम]{lang=hi} CPU [की संख्या है।]{lang=hi} |

#### [कार्यक्षेत्र]{lang=hi}

| [गुण]{lang=hi} | [डिफ़ॉल्ट]{lang=hi}  | [इकाई]{lang=hi}   | [आवश्यक]{lang=hi}  | [विवरण]{lang=hi}   |
| :------- | :------ | :--- | :------- | :---------- |  
| [कार्यक्षेत्र]{lang=hi} (workspace) | /tmp/pgmoneta-workspace/ | String | [नहीं]{lang=hi}   | [वह डायरेक्टरी जिसे वृद्धिशील]{lang=hi} (incremental) [बैकअप अपने कार्य के लिए उपयोग कर सकता है।]{lang=hi} |

#### [भंडारण]{lang=hi}

| [गुण]{lang=hi}        | [डिफ़ॉल्ट]{lang=hi} | [इकाई]{lang=hi}   | [आवश्यक]{lang=hi} | [विवरण]{lang=hi}   |
|------------|----------|--------|--------|----------|
| storage_engine | local  | String | [नहीं]{lang=hi}    | [भंडारण इंजन प्रकार]{lang=hi} (local, ssh, s3, azure) |

#### [एन्क्रिप्शन]{lang=hi}

| [गुण]{lang=hi}        | [डिफ़ॉल्ट]{lang=hi} | [इकाई]{lang=hi}   | [आवश्यक]{lang=hi} | [विवरण]{lang=hi}   |
|------------|----------|--------|--------|----------|
| encryption | none     | String | [नहीं]{lang=hi}    | WAL [और डेटा को एन्क्रिप्ट करने का मोड]{lang=hi} <br/> `none`: [कोई एन्क्रिप्शन नहीं]{lang=hi} <br/> `aes \| aes-256 \| aes-256-cbc`: AES CBC (Cipher Block Chaining) [मोड 256 बिट कुंजी लंबाई के साथ]{lang=hi} <br/> `aes-192 \| aes-192-cbc`: AES CBC [मोड 192 बिट कुंजी लंबाई के साथ]{lang=hi} <br/> `aes-128 \| aes-128-cbc`: AES CBC [मोड 128 बिट कुंजी लंबाई के साथ]{lang=hi} <br/> `aes-256-ctr`: AES CTR (Counter) [मोड 256 बिट कुंजी लंबाई के साथ]{lang=hi} <br/> `aes-192-ctr`: AES CTR [मोड 192 बिट कुंजी लंबाई के साथ]{lang=hi} <br/> `aes-128-ctr`: AES CTR [मोड 128 बिट कुंजी लंबाई के साथ]{lang=hi} |

#### [स्लॉट प्रबंधन]{lang=hi}

| [गुण]{lang=hi}        | [डिफ़ॉल्ट]{lang=hi} | [इकाई]{lang=hi}   | [आवश्यक]{lang=hi} | [विवरण]{lang=hi}   |
|------------|----------|--------|--------|----------|
| create_slot | no      | Bool   | [नहीं]{lang=hi}    | [सभी सर्वर के लिए एक प्रतिकृति स्लॉट बनाएं। वैध मान हैं:]{lang=hi} yes, no |

#### SSH 

| [प्रॉपर्टी]{lang=hi} | [डिफ़ॉल्ट]{lang=hi} | [यूनिट]{lang=hi} | [आवश्यक]{lang=hi} | [विवरण]{lang=hi} |
| :------- | :------ | :--- | :------- | :---------- |
| ssh_hostname | | String | [हाँ]{lang=hi} | [कनेक्शन के लिए रिमोट सिस्टम का होस्टनाम निर्दिष्ट करता है]{lang=hi} |
| ssh_username | | String | [हाँ]{lang=hi} | [कनेक्शन के लिए रिमोट सिस्टम का उपयोगकर्ता नाम निर्दिष्ट करता है]{lang=hi} |
| ssh_base_dir | | String | [हाँ]{lang=hi} | [रिमोट बैकअप के लिए आधार डायरेक्टरी]{lang=hi} |
| ssh_ciphers | aes-256-ctr, aes-192-ctr, aes-128-ctr | String | [नहीं]{lang=hi} | [संचार के लिए समर्थित सिफर।]{lang=hi}<br/> `aes \| aes-256 \| aes-256-cbc`: [256-बिट कुंजी लंबाई के साथ]{lang=hi} AES CBC [(सिफर ब्लॉक चेनिंग) मोड]{lang=hi}<br/> `aes-192 \| aes-192-cbc`: [192-बिट कुंजी लंबाई के साथ]{lang=hi} AES CBC [मोड]{lang=hi}<br/> `aes-128 \| aes-128-cbc`: [128-बिट कुंजी लंबाई के साथ]{lang=hi} AES CBC [मोड]{lang=hi}<br/> `aes-256-ctr`: [256-बिट कुंजी लंबाई के साथ]{lang=hi} AES CTR [(काउंटर) मोड]{lang=hi}<br/> `aes-192-ctr`: [192-बिट कुंजी लंबाई के साथ]{lang=hi} AES CTR [मोड]{lang=hi}<br/> `aes-128-ctr`: [128-बिट कुंजी लंबाई के साथ]{lang=hi} AES CTR [मोड। अन्यथा, मूल रूप में।]{lang=hi} |

#### S3

| [गुण]{lang=hi}        | [डिफ़ॉल्ट]{lang=hi} | [इकाई]{lang=hi}   | [आवश्यक]{lang=hi} | [विवरण]{lang=hi}   |
|------------|----------|--------|--------|----------|
| s3_aws_region |      | String | [हाँ]{lang=hi}     | AWS [क्षेत्र]{lang=hi} |
| s3_access_key_id |  | String | [हाँ]{lang=hi}     | IAM [एक्सेस कुंजी आईडी]{lang=hi} |
| s3_secret_access_key | | String | [हाँ]{lang=hi}     | IAM [गुप्त एक्सेस कुंजी]{lang=hi} |
| s3_bucket |       | String | [हाँ]{lang=hi}     | AWS S3 [बकेट का नाम]{lang=hi} |
| s3_base_dir |     | String | [हाँ]{lang=hi}     | S3 [बकेट के लिए बेस निर्देशिका]{lang=hi} |

#### Azure

| [गुण]{lang=hi}        | [डिफ़ॉल्ट]{lang=hi} | [इकाई]{lang=hi}   | [आवश्यक]{lang=hi} | [विवरण]{lang=hi}   |
|------------|----------|--------|--------|----------|
| azure_storage_account | | String | [हाँ]{lang=hi}     | Azure [स्टोरेज खाता नाम]{lang=hi} |
| azure_container |       | String | [हाँ]{lang=hi}     | Azure [कंटेनर नाम]{lang=hi} |
| azure_shared_key |     | String | [हाँ]{lang=hi}     | Azure [स्टोरेज खाता कुंजी]{lang=hi} |
| azure_base_dir |        | String | [हाँ]{lang=hi}     | Azure [कंटेनर के लिए बेस निर्देशिका]{lang=hi} |

#### [संग्रहण]{lang=hi}

| [गुण]{lang=hi}        | [डिफ़ॉल्ट]{lang=hi} | [इकाई]{lang=hi}   | [आवश्यक]{lang=hi} | [विवरण]{lang=hi}
|------------|----------|--------|--------|----------|
| retention | 7, - , - , - | Array | [नहीं]{lang=hi} | [संग्रहण समय दिन, सप्ताह, महीने, साल में]{lang=hi} |


#### [लॉगिंग]{lang=hi}

| [प्रॉपर्टी]{lang=hi}  | [डिफ़ॉल्ट]{lang=hi}  | [यूनिट]{lang=hi}  | [आवश्यक]{lang=hi}  | [विवरण]{lang=hi}  |
| :-------- | :------- | :--- | :------ | :------ |
| log_type | console | String | [नहीं]{lang=hi}  | [लॉगिंग प्रकार]{lang=hi}  (console, file, syslog) |
| log_level | info | String | [नहीं]{lang=hi}  | [लॉग स्तर,]{lang=hi}  `FATAL`, `ERROR`, `WARN`, `INFO`, [और]{lang=hi}  `DEBUG` [में से कोई भी (जो]{lang=hi}  `DEBUG1` [से लेकर]{lang=hi}  `DEBUG5` [तक अधिक विशिष्ट हो सकता है।) 5 से अधिक डिबग स्तर को]{lang=hi}  `DEBUG5` [में सेट किया जाएगा। यदि मान्य नहीं है, तो]{lang=hi}  `INFO` [सेट किया जाएगा।]{lang=hi}  |
| log_path | pgmoneta.log | String | [नहीं]{lang=hi}  | [लॉग फ़ाइल स्थान। यह एक]{lang=hi}  strftime(3) [संगत स्ट्रिंग हो सकती है।]{lang=hi}  |
| log_rotation_age | 0 | String | [नहीं]{lang=hi}  | [वह आयु जो लॉग फ़ाइल के रोटेशन को ट्रिगर करेगी। यदि सकारात्मक संख्या के रूप में व्यक्त किया गया है, तो इसे सेकंड के रूप में प्रबंधित किया जाएगा। सपोर्टेड प्रत्यय:]{lang=hi}  'S' [(सेकंड, डिफ़ॉल्ट),]{lang=hi}  'M' [(मिनट)]{lang=hi} , 'H' [(घंटे)]{lang=hi} , 'D' [(दिन)]{lang=hi} , 'W' [(सप्ताह)। मान]{lang=hi}  `0`[ रोटेशन को अक्षम कर देगा।]{lang=hi}  |
| log_rotation_size | 0 | String | [नहीं]{lang=hi}  | [लॉग फ़ाइल का आकार जो रोटेशन को ट्रिगर करेगा। सपोर्टेड प्रत्यय:]{lang=hi}  'B' [(बाइट्स), यदि प्रत्यय को छोड़ा गया है तो डिफ़ॉल्ट।]{lang=hi} 'K' [या]{lang=hi} 'KB' [(किलोबाइट्स),]{lang=hi} 'M' [या ]{lang=hi}'MB' [(मेगाबाइट्स),]{lang=hi} 'G' [या]{lang=hi}  'GB' [(गीगाबाइट्स)। मान]{lang=hi}  `0` [(प्रत्यय के साथ या बिना) इसे अक्षम कर देगा।]{lang=hi}  |
| log_line_prefix | %Y-%m-%d %H:%M:%S | String | [नहीं]{lang=hi}  | [हर लॉग पंक्ति के लिए एक]{lang=hi}  strftime(3) [संगत स्ट्रिंग को पूर्वसूचक के रूप में उपयोग करने के लिए। यदि इसमें अंतराल (स्पेस) है तो इसे उद्धरण में होना चाहिए।]{lang=hi}  |
| log_mode | append | String | [नहीं]{lang=hi}  | [लॉग फ़ाइल में जोड़ें या इसे बनाएँ]{lang=hi}  (append, create) |

#### [परिवहन स्तर सुरक्षा]{lang=hi}

| [प्रॉपर्टी]{lang=hi} | [डिफ़ॉल्ट]{lang=hi} | [यूनिट]{lang=hi} | [आवश्यक]{lang=hi} | [विवरण]{lang=hi} |
| :-------- | :------- | :--- | :------ | :------ |
| tls | `off` | Boolean | [नहीं]{lang=hi} | [ट्रांसपोर्ट लेयर सुरक्षा]{lang=hi} (TLS) [सक्षम करें]{lang=hi} |
| tls_cert_file | | String | [नहीं]{lang=hi} | TLS [के लिए प्रमाणपत्र फ़ाइल। यह फ़ाइल]{lang=hi} pgmoneta [या]{lang=hi} root [द्वारा चलाने वाले उपयोगकर्ता की होनी चाहिए।]{lang=hi} |
| tls_key_file | | String | [नहीं]{lang=hi} | TLS [के लिए निजी कुंजी फ़ाइल। यह फ़ाइल]{lang=hi} pgmoneta [या]{lang=hi} root [द्वारा चलाने वाले उपयोगकर्ता की होनी चाहिए। साथ ही, जब]{lang=hi} root [द्वारा मालिक हो तो इसकी अनुमति कम से कम]{lang=hi} `0640` [और अन्यथा]{lang=hi} `0600` [होनी चाहिए।]{lang=hi} |
| tls_ca_file | | String | [नहीं]{lang=hi} | TLS [के लिए प्रमाणपत्र प्राधिकरण]{lang=hi} (CA) [फ़ाइल। यह फ़ाइल]{lang=hi} pgmoneta [या]{lang=hi} root [द्वारा चलाने वाले उपयोगकर्ता की होनी चाहिए।]{lang=hi} |
| libev | `auto` | String | [नहीं]{lang=hi} | libev [बैकएंड का चयन करें। वैध विकल्प:]{lang=hi} `auto`, `select`, `poll`, `epoll`, `iouring`, `devpoll`, [और]{lang=hi} `port` |

#### [विविध]{lang=hi}

| [प्रॉपर्टी]{lang=hi} | [डिफ़ॉल्ट]{lang=hi} | [यूनिट]{lang=hi} | [आवश्यक]{lang=hi} | [विवरण]{lang=hi} |
| :------- | :------ | :--- | :------- | :---------- |
| **backup_max_rate** | 0 | int | [नहीं]{lang=hi} | [बैकअप दर को सीमित करने के लिए प्रति सेकंड जोड़े गए टोकन के बाइट्स की संख्या]{lang=hi} |
| **network_max_rate** | 0 | int | [नहीं]{lang=hi} | [नेटवर्क बैकअप दर को सीमित करने के लिए प्रति सेकंड जोड़े गए टोकन के बाइट्स की संख्या]{lang=hi} |
| **manifest** | sha256 | string | [नहीं]{lang=hi} | [मैनिफेस्ट के लिए हैश एल्गोरिदम। वैध विकल्प:]{lang=hi} `crc32c`, `sha224`, `sha256`, `sha384` [और]{lang=hi} `sha512` |
| **blocking_timeout** | 30 | int | [नहीं]{lang=hi} | [कनेक्शन के लिए प्रक्रिया के ब्लॉक होने का समय (अक्षम = 0)]{lang=hi} |
| **keep_alive** | on | boolean | [नहीं]{lang=hi} | [सॉकेट्स पर]{lang=hi} `SO_KEEPALIVE`
| **nodelay** | on | boolean | [नहीं]{lang=hi} | [सॉकेट्स पर]{lang=hi} `TCP_NODELAY` [चालू रखें]{lang=hi} |
| **non_blocking** | on | boolean | [नहीं]{lang=hi} | [सॉकेट्स पर]{lang=hi} `O_NONBLOCK` [चालू रखें]{lang=hi} |
| **backlog** | 16 | int | [नहीं]{lang=hi} | `listen()` [के लिए बैकलॉग। न्यूनतम]{lang=hi} `16` |
| **hugepage** | `try` | string | [नहीं]{lang=hi} | [ह्यूज पेज समर्थन]{lang=hi} (`off`, `try`, `on`) |
| **pidfile** | | string | [नहीं]{lang=hi} | PID[ फ़ाइल का पथ। यदि निर्दिष्ट नहीं है, तो यह स्वचालित रूप से]{lang=hi} `unix_socket_dir/pgmoneta.<host>.pid` [पर सेट हो जाएगा, जहां]{lang=hi} `<host>` `host` [पैरामीटर का मान है या]{lang=hi} `all` [है यदि]{lang=hi} `host = *` [है।]{lang=hi} |
| **update_process_title** | `verbose` | string | [नहीं]{lang=hi} | [ऑपरेटिंग सिस्टम प्रक्रिया शीर्षक को अपडेट करने का व्यवहार। अनुमत सेटिंग्स:]{lang=hi} `never` ([या]{lang=hi} `off`), [प्रक्रिया शीर्षक को अपडेट नहीं करता है;]{lang=hi} `strict` [प्रारंभिक प्रक्रिया शीर्षक की मौजूदा लंबाई को ओवरराइड किए बिना प्रक्रिया शीर्षक सेट करने के लिए;]{lang=hi} `minimal` [बेस विवरण के लिए प्रक्रिया शीर्षक सेट करने के लिए;]{lang=hi} `verbose` ([या]{lang=hi} `full`) [पूर्ण विवरण के लिए प्रक्रिया शीर्षक सेट करने के लिए। ध्यान दें कि]{lang=hi} `strict` [और]{lang=hi} `minimal` [केवल उन्हीं सिस्टम पर मान्य हैं जो प्रक्रिया शीर्षक सेट करने के लिए एक देशी तरीका प्रदान नहीं करते । अन्य सिस्टम पर ]{lang=hi}`strict` [और]{lang=hi} `minimal` [के बीच कोई अंतर नहीं है और डिफ़ॉल्ट व्यवहार ]{lang=hi} `minimal` [माना जाता है, भले ही]{lang=hi} `strict` [का उपयोग किया गया हो।]{lang=hi} `never` [और]{lang=hi} `verbose` [सभी सिस्टम पर हमेशा मान्य होते हैं।]{lang=hi} Linux [सिस्टम पर प्रक्रिया शीर्षक हमेशा 255 अक्षरों तक सीमित होता है, जबकि सिस्टम जो प्रक्रिया शीर्षक सेट करने के लिए देशी तरीका प्रदान करते हैं, वहां यह लंबा हो सकता है।]{lang=hi} |

### [सर्वर अनुभाग]{lang=hi}

#### [सर्वर]{lang=hi}

| [प्रॉपर्टी]{lang=hi} | [डिफ़ॉल्ट]{lang=hi} | [यूनिट]{lang=hi} | [आवश्यक]{lang=hi} | [विवरण]{lang=hi} |
| :-------- | :------- | :--- | :------ | :------ |
| host | | string | [हाँ]{lang=hi} | PostgreSQL [इंस्टेंस का पता]{lang=hi} |
| port | | int | [हाँ]{lang=hi} | PostgreSQL [इंस्टेंस की पोर्ट]{lang=hi} |
| user | | string | [हाँ]{lang=hi} | [प्रतिकृति उपयोगकर्ता का नाम]{lang=hi} |
| wal_slot | | string | [हाँ]{lang=hi} | WAL [के लिए प्रतिकृति स्लॉट]{lang=hi} |

[उपयोगकर्ता को]{lang=hi}  `REPLICATION` [विकल्प के साथ प्रतिकृति के लिए सक्षम किया जाना चाहिए ताकि]{lang=hi}  Write-Ahead Log (WAL) [स्ट्रीम किया जा सके, और उसे]{lang=hi}  `postgres` [डेटाबेस तक पहुंच होनी चाहिए ताकि आवश्यक कॉन्फ़िगरेशन पैरामीटर प्राप्त किए जा सकें।]{lang=hi}

#### [स्लॉट प्रबंधन]{lang=hi}

| [प्रॉपर्टी]{lang=hi}  | [डिफ़ॉल्ट]{lang=hi}  | [यूनिट]{lang=hi}  | [आवश्यक]{lang=hi}  | [विवरण]{lang=hi}  |
| :-------- | :------- | :--- | :------ | :------ |
| create_slot | [नहीं]{lang=hi}  | boolean | [नहीं ]{lang=hi} | [इस सर्वर के लिए प्रतिकृति स्लॉट बनाएँ। वैध मान: हाँ, नहीं]{lang=hi}  |

#### [पालन]{lang=hi}

| [प्रॉपर्टी]{lang=hi}  | [डिफ़ॉल्ट]{lang=hi}  | [यूनिट]{lang=hi}  | [आवश्यक]{lang=hi}  | [विवरण]{lang=hi}  |
| :-------- | :------- | :--- | :------ | :------ |
| follow | | string | [नहीं]{lang=hi}  | [यदि पालन सर्वर विफल हो तो इस सर्वर पर फेलओवर करें]{lang=hi}  |

#### [रिटेंशन]{lang=hi}

| [प्रॉपर्टी]{lang=hi}  | [डिफ़ॉल्ट]{lang=hi}  | [यूनिट]{lang=hi}  | [आवश्यक]{lang=hi}  | [विवरण]{lang=hi}  |
| :-------- | :------- | :--- | :------ | :------ |
| retention | | array | [नहीं]{lang=hi}  | [सर्वर के लिए रिटेंशन दिनों, सप्ताहों, महीनों, वर्षों के हिसाब से]{lang=hi}  |

#### WAL [शिपिंग]{lang=hi}

| [प्रॉपर्टी]{lang=hi}  | [डिफ़ॉल्ट]{lang=hi}  | [यूनिट]{lang=hi}  | [आवश्यक]{lang=hi}  | [विवरण]{lang=hi}  |
| :-------- | :------- | :--- | :------ | :------ |
| wal_shipping | | string | [नहीं]{lang=hi}  | WAL [शिपिंग डायरेक्टरी]{lang=hi}  |

#### [हॉट स्टैंडबाई]{lang=hi}

| [प्रॉपर्टी]{lang=hi}  | [डिफ़ॉल्ट]{lang=hi}  | [यूनिट]{lang=hi}  | [आवश्यक]{lang=hi}  | [विवरण]{lang=hi}  |
| :-------- | :------- | :--- | :------ | :------ |
| hot_standby | | string | [नहीं]{lang=hi}  | [हॉट स्टैंडबाई डायरेक्टरी]{lang=hi}  |
| hot_standby_overrides | | string | [नहीं]{lang=hi}  | [हॉट स्टैंडबाई डायरेक्टरी में ओवरराइड करने के लिए फाइलें]{lang=hi}
| hot_standby_tablespaces | | string | [नहीं]{lang=hi}  | [हॉट स्टैंडबाई के लिए टेबलस्पेस मैपिंग्स। स्वरूप]{lang=hi}  `[from -> to,?]+` |

#### [कार्यकर्ता]{lang=hi}

| [प्रॉपर्टी]{lang=hi}  | [डिफ़ॉल्ट]{lang=hi}  | [यूनिट]{lang=hi}  | [आवश्यक]{lang=hi}  | [विवरण]{lang=hi}  |
| :-------- | :------- | :--- | :------ | :------ |
| workers | -1 | int | [नहीं]{lang=hi}  | [प्रत्येक प्रोसेस के लिए कार्यों के लिए उपयोग किए जा सकने वाले कार्यकर्ताओं की संख्या। 0 को अक्षम करने के लिए, -1 का अर्थ है वैश्विक सेटिंग का उपयोग करना]{lang=hi} | [अधिकतम]{lang=hi}  CPU [की संख्या है।]{lang=hi}

#### [परिवहन स्तर सुरक्षा]{lang=hi}

| [प्रॉपर्टी]{lang=hi}  | [डिफ़ॉल्ट]{lang=hi}  | [यूनिट]{lang=hi}  | [आवश्यक]{lang=hi}  | [विवरण]{lang=hi}  |
| :-------- | :------- | :--- | :------ | :------ |
| tls_cert_file | | string | [नहीं]{lang=hi}  | TLS [के लिए प्रमाणपत्र फ़ाइल। यह फ़ाइल]{lang=hi}  pgmoneta [या]{lang=hi}  root [द्वारा चलाने वाले उपयोगकर्ता की होनी चाहिए।]{lang=hi}  |
| tls_key_file | | string | [नहीं]{lang=hi}  | TLS [के लिए निजी कुंजी फ़ाइल। यह फ़ाइल]{lang=hi}  pgmoneta [या]{lang=hi}  root [द्वारा चलाने वाले उपयोगकर्ता की होनी चाहिए। साथ ही, जब]{lang=hi}  root [द्वारा मालिक हो तो इसकी अनुमति कम से कम]{lang=hi}  `0640` [और अन्यथा]{lang=hi}  `0600` [होनी चाहिए।]{lang=hi}  |
| tls_ca_file | | string | [नहीं]{lang=hi}  | TLS [के लिए प्रमाणपत्र प्राधिकरण]{lang=hi}  (CA) [फ़ाइल। यह फ़ाइल ]{lang=hi} pgmoneta [या]{lang=hi}  root [द्वारा चलाने वाले उपयोगकर्ता की होनी चाहिए।]{lang=hi}  |

#### [विविध]{lang=hi}

| [प्रॉपर्टी]{lang=hi} | [डिफ़ॉल्ट]{lang=hi} | [यूनिट]{lang=hi} | [आवश्यक]{lang=hi} | [विवरण]{lang=hi} |
| :-------- | :------- | :--- | :------ | :------ |
| backup_max_rate | -1 | int | [नहीं]{lang=hi} | [बैकअप दर को सीमित करने के लिए हर एक सेकंड में जोड़ी जाने वाली बाइट्स की संख्या। इसे अक्षम करने के लिए]{lang=hi} `0` [का उपयोग करें, -1 का मतलब है वैश्विक सेटिंग का उपयोग करें।]{lang=hi} |
| network_max_rate | -1 | int | [नहीं]{lang=hi} | [नेटवर्क बैकअप दर को सीमित करने के लिए हर एक सेकंड में जोड़ी जाने वाली बाइट्स की संख्या। इसे अक्षम करने के लिए `0` का उपयोग करें, -1 का मतलब है वैश्विक सेटिंग का उपयोग करें।]{lang=hi} |
| manifest | sha256 | string | [नहीं]{lang=hi} | [मैनिफ़ेस्ट के लिए हैश एल्गोरिदम। वैध विकल्प:]{lang=hi} `crc32c`, `sha224`, `sha256`, `sha384`, [और]{lang=hi} `sha512` |

#### [अतिरिक्त]{lang=hi}

| [प्रॉपर्टी]{lang=hi} | [डिफ़ॉल्ट]{lang=hi} | [यूनिट]{lang=hi} | [आवश्यक]{lang=hi} | [विवरण]{lang=hi} |
| :------- | :------ | :--- | :------- | :---------- |
| **extra** | | string | [नहीं]{lang=hi} | [सर्वर साइड पर पुनः प्राप्ति के लिए स्रोत निर्देशिका (विवरण अतिरिक्त अनुभाग में दिया गया है)]{lang=hi} |

`extra` [कॉन्फ़िगरेशन को सर्वर सेक्शन में सेट किया जाता है। यह आवश्यक नहीं है, लेकिन यदि आप इस पैरामीटर को कॉन्फ़िगर करते हैं, तो जब आप]{lang=hi} CLI [का उपयोग करके बैकअप करते हैं, जैसे कि]{lang=hi} `pgmoneta-cli -c pgmoneta.conf backup primary`, [तो यह सर्वर साइड पर निर्दिष्ट सभी फाइलों को कॉपी करेगा और उन्हें क्लाइंट साइड पर भेज देगा।]{lang=hi}

[यह]{lang=hi} `extra` [सुविधा सर्वर साइड पर]{lang=hi} [pgmoneta_ext](https://github.com/pgmoneta/pgmoneta_ext) [एक्सटेंशन इंस्टॉल करने की आवश्यकता रखती है और उपयोगकर्ता]{lang=hi} `repl` [को बनाना पड़ता है (भविष्य में इसे सुधारने की योजना है)। वर्तमान में, यह सुविधा केवल]{lang=hi} `SUPERUSER` [भूमिका के लिए उपलब्ध है।]{lang=hi}

[आप]{lang=hi} [README](https://github.com/pgmoneta/pgmoneta_ext/blob/main/README.md) [का अनुसरण करके आसानी से]{lang=hi} `pgmoneta_ext` [सेटअप कर सकते हैं। इसके अलावा,]{lang=hi} [DEVELOPERS](https://github.com/pgmoneta/pgmoneta_ext/blob/main/doc/DEVELOPERS.md) [दस्तावेज़ में अधिक विस्तृत निर्देश उपलब्ध हैं।]{lang=hi}

`extra` [पैरामीटर का फॉर्मेट फ़ाइल या निर्देशिका का पथ है। आप कई फ़ाइलों या निर्देशिकाओं को कॉमा से अलग करके सूचीबद्ध कर सकते हैं। इसका प्रारूप निम्नलिखित है:]{lang=hi}

```ini
extra = /tmp/myfile1, /tmp/myfile2, /tmp/mydir1, /tmp/mydir2
```  

### pgmoneta_users.conf

`pgmoneta_users` [कॉन्फ़िगरेशन सिस्टम में ज्ञात उपयोगकर्ताओं को परिभाषित करता है। यह फ़ाइल]{lang=hi} `pgmoneta-admin` [टूल के माध्यम से बनाई और प्रबंधित की जाती है।]{lang=hi}

[कॉन्फ़िगरेशन या तो]{lang=hi} `-u` [फ्लैग द्वारा निर्दिष्ट पथ से या]{lang=hi} `/etc/pgmoneta/pgmoneta_users.conf` [से लोड होती है।]{lang=hi}

### pgmoneta_admins.conf

`pgmoneta_admins` [कॉन्फ़िगरेशन सिस्टम में ज्ञात प्रशासकों को परिभाषित करता है। यह फ़ाइल]{lang=hi} `pgmoneta-admin` [टूल के माध्यम से बनाई और प्रबंधित की जाती है।]{lang=hi}

[कॉन्फ़िगरेशन को या तो]{lang=hi} `-A` [फ्लैग द्वारा निर्दिष्ट पथ से या]{lang=hi} `/etc/pgmoneta/pgmoneta_admins.conf` [से लोड किया जाता है।]{lang=hi}

[यदि]{lang=hi} pgmoneta [मे]{lang=hi} Transport Layer Security (TLS)  [और]{lang=hi} `management` [सक्षम हैं, तो]{lang=hi} `pgmoneta-cli` TLS [का उपयोग करके कनेक्ट कर सकता है। इसके लिए फाइलें]{lang=hi} `~/.pgmoneta/pgmoneta.key` [(अनुमति 0600 होनी चाहिए।)]{lang=hi}, `~/.pgmoneta/pgmoneta.crt` [और]{lang=hi} `~/.pgmoneta/root.crt` [का उपयोग करना आवश्यक है।]{lang=hi}
