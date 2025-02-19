\newpage  

# [स्थापना]{lang=hi}

## [रॉकी लिनक्स]{lang=hi} 9.x

[हम वितरण को उनकी वेबसाइट से डाउनलोड कर सकते हैं:]{lang=hi} [Rocky Linux](https://www.rockylinux.org/)

```
https://rockylinux.org/download
```  

[स्थापना और सेटअप इस गाइड के दायरे से बाहर है।]{lang=hi}

[आदर्श रूप से, आप]{lang=hi} [**PostgreSQL**][postgresql] [और]{lang=hi} [**pgmoneta**][pgmoneta] [चलाने के लिए समर्पित उपयोगकर्ता खाते का उपयोग करेंगे:]{lang=hi}

```
useradd postgres  
usermod -a -G wheel postgres  
useradd pgmoneta  
usermod -a -G wheel pgmoneta  
```  

[**pgmoneta**][pgmoneta] [के लिए एक कॉन्फ़िगरेशन डायरेक्टरी जोड़ें:]{lang=hi}

```
mkdir /etc/pgmoneta  
chown -R pgmoneta:pgmoneta /etc/pgmoneta  
```  

[अब, फायरवॉल में उन पोर्ट्स को खोलें जिनकी आवश्यकता होगी:]{lang=hi}

```
firewall-cmd --permanent --zone=public --add-port=5001/tcp  
firewall-cmd --permanent --zone=public --add-port=5002/tcp  
```  

## PostgreSQL 17  

[हम]{lang=hi} PostgreSQL 17 [को आधिकारिक]{lang=hi} [YUM repositry][yum] [से सामुदायिक बाइनरी के साथ स्थापित करेंगे:]{lang=hi}

**x86_64**  

```
dnf -qy module disable postgresql  
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm  
```  

**aarch64**  

```
dnf -qy module disable postgresql  
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-aarch64/pgdg-redhat-repo-latest.noarch.rpm  
```  

[और इंस्टॉलेशन निम्न कमांड के माध्यम से करें:]{lang=hi}

```
dnf install -y postgresql17 postgresql17-server postgresql17-contrib  
```  

[सबसे पहले,]{lang=hi} `~/.bashrc` [को अपडेट करें:]{lang=hi}

```
cat >> ~/.bashrc  
export PGHOST=/tmp  
export PATH=/usr/pgsql-17/bin/:$PATH  
```  

**Ctrl-d [दबाएं]{lang=hi}** [और फिर:]{lang=hi}

```
source ~/.bashrc  
``` 

[का उपयोग करके]{lang=hi} Bash [वातावरण को फिर से लोड करें।]{lang=hi}

[अब]{lang=hi} PostgreSQL [इनिशियलाइज़ करें:]{lang=hi}

```
mkdir DB  
initdb -k DB  
```  

8 [जीबी मेमोरी मशीन के लिए कॉन्फ़िगरेशन अपडेट करें।]{lang=hi}

**postgresql.conf**  
```
listen_addresses = '*'
port = 5432
max_connections = 100
unix_socket_directories = '/tmp'
password_encryption = scram-sha-256
shared_buffers = 2GB
huge_pages = try
max_prepared_transactions = 100
work_mem = 16MB
dynamic_shared_memory_type = posix
wal_level = replica
wal_log_hints = on
max_wal_size = 16GB
min_wal_size = 2GB
log_destination = 'stderr'
logging_collector = on
log_directory = 'log'
log_filename = 'postgresql.log'
log_rotation_age = 0
log_rotation_size = 0
log_truncate_on_rotation = on
log_line_prefix = '%p [%m] [%x] '
log_timezone = UTC
datestyle = 'iso, mdy'
timezone = UTC
lc_messages = 'en_US.UTF-8'
lc_monetary = 'en_US.UTF-8'
lc_numeric = 'en_US.UTF-8'
lc_time = 'en_US.UTF-8'
``` 

**pg_hba.conf**  
```
local   all           all                   trust
host    postgres      repl   127.0.0.1/32   scram-sha-256
host    postgres      repl   ::1/128        scram-sha-256
host    replication   repl   127.0.0.1/32   scram-sha-256
host    replication   repl   ::1/128        scram-sha-256
```

[कृपया, अपने लोकल सेटअप के लिए सेटअप तैयार करने हेतु अन्य स्रोतों की जाँच करें।]{lang=hi}

[अब, हम]{lang=hi} PostgreSQL [शुरू करने के लिए तैयार हैं।]{lang=hi}

```
pg_ctl -D DB -l /tmp/ start  
```  

[डेटाबेस से कनेक्ट करें, रेप्लिकेशन उपयोगकर्ता जोड़ें, और]{lang=hi} Write-Ahead Log (WAL) [स्लॉट बनाएं:]{lang=hi}

```
psql postgres  
CREATE ROLE repl WITH LOGIN REPLICATION PASSWORD 'repl';  
SELECT pg_create_physical_replication_slot('repl', true, false);  
\q  
```  

## pgmoneta  

[**pgmoneta**][pgmoneta] [को आधिकारिक]{lang=hi} [YUM repositry][yum] [से इंस्टॉल करें:]{lang=hi}

```
dnf install -y pgmoneta  
```  

[सबसे पहले, हमें]{lang=hi} [**pgmoneta**][pgmoneta] [इंस्टॉलेशन के लिए एक मास्टर सुरक्षा कुंजी बनाने की आवश्यकता होगी, इसके लिए निम्न कमांड का उपयोग करें:]{lang=hi}

```
pgmoneta-admin -g master-key
```

[फिर, हम]{lang=hi} [**pgmoneta**][pgmoneta] [के लिए कॉन्फ़िगरेशन बनाएंगे,]{lang=hi}

```
cat > /etc/pgmoneta/pgmoneta.conf
[pgmoneta]
host = *
metrics = 5001

base_dir = /home/pgmoneta/backup

compression = zstd

retention = 7

log_type = file
log_level = info
log_path = /tmp/pgmoneta.log

unix_socket_dir = /tmp/

[primary]
host = localhost
port = 5432
user = repl
wal_slot = repl
```

[और फ़ाइल को सहेजने के लिए]{lang=hi} **Ctrl-d** [के साथ समाप्त करें।]{lang=hi}

[फिर, हम उपयोगकर्ता कॉन्फ़िगरेशन बनाएंगे,]{lang=hi}

```
pgmoneta-admin -f /etc/pgmoneta/pgmoneta_users.conf -U repl -P repl user add
```  

[अब, हम बेस डायरेक्टरी बनाएंगे और]{lang=hi} [**pgmoneta**][pgmoneta] [शुरू करेंगे,]{lang=hi}

```
mkdir backup
pgmoneta -d
```
