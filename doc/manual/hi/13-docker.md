\newpage

# Docker [के साथ]{lang=hi} pgmoneta [निष्पादित करना]{lang=hi}

[आप इसे मैन्युअल रूप से संकलित करने के बजाय डॉकर का उपयोग करके]{lang=hi} [**pgmoneta**][pgmoneta] [निष्पादित कर सकते हैं।]{lang=hi}

## [पूर्वापेक्षाएँ]{lang=hi}

* [**Docker**][docker] [या]{lang=hi} [**Podman**][podman] [को उस सर्वर पर स्थापित किया जाना चाहिए जहाँ यह]{lang=hi} PostgreSQL [चला रहा है।]{lang=hi}
* [सुनिश्चित करें कि]{lang=hi} PostgreSQL [बाहरी कनेक्शन की अनुमति देने के लिए कॉन्फ़िगर किया गया है।]{lang=hi}

## [यदि आवश्यक हो तो कॉन्फ़िगरेशन फ़ाइल को अपडेट करें:]{lang=hi}

```ini
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

## [चरण]{lang=hi} 1: PostgreSQL [के लिए बाहरी पहुँच सक्षम करें]{lang=hi}

[बाहर से कनेक्शन की अनुमति देने के लिए]{lang=hi} PostgreSQL [स्थानीय सर्वर के]{lang=hi} `postgresql.conf` [को संशोधित करेंः]{lang=hi}
```ini
listen_addresses = '*'
```

`pg_hba.conf` [को दूरस्थ कनेक्शन की अनुमति देने के लिए अद्यतन करेंः]{lang=hi}
```ini
host    all    all    0.0.0.0/0    scram-sha-256
```

[फिर, परिवर्तनों को प्रभावी बनाने के लिए]{lang=hi} PostgreSQL [को पुनरारंभ करेंः]{lang=hi}
```sh
sudo systemctl restart postgresql
```

## [चरण]{lang=hi} 2: [रिपॉजिटरी को क्लोन करें]{lang=hi}
```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
```

## [चरण]{lang=hi} 3: [डॉकर इमेज बनाएं]{lang=hi}

[दो डॉकरफाइल उपलब्ध हैंः]{lang=hi}

1. **Alpine [इमेज]{lang=hi}**

**Docker [का उपयोग करके]{lang=hi}**
```sh
docker build -t pgmoneta:latest -f ./contrib/docker/Dockerfile.alpine .
```

**Podman [का उपयोग करके]{lang=hi}**

```sh
podman build -t pgmoneta:latest -f ./contrib/docker/Dockerfile.alpine .
```

2. **Rocky Linux 9 [इमेज]{lang=hi}**

**Docker [का उपयोग करके]{lang=hi}**
```sh
docker build -t pgmoneta:latest -f ./contrib/docker/Dockerfile.rocky9 .
```

**Podman [का उपयोग करके]{lang=hi}**

```sh
podman build -t pgmoneta:latest -f ./contrib/docker/Dockerfile.rocky9 .
```

## [चरण]{lang=hi} 4: pgmoneta [को डॉकर कंटेनर के रूप में चलाएँ]{lang=hi}

[एक बार इमेज बन जाने पर, कंटेनर को निम्न का उपयोग करके चलाएँ:]{lang=hi}

- **Docker [का उपयोग करके]{lang=hi}**

```sh
docker run -d --name pgmoneta --network host pgmoneta:latest
```

- **Podman [का उपयोग करके]{lang=hi}**

```sh
podman run -d --name pgmoneta --network host pgmoneta:latest
```

## [चरण]{lang=hi} 5: [कंटेनर को सत्यापित करें]{lang=hi}

[जाँचें कि कंटेनर चल रहा है या नहीं:]{lang=hi}

- **Docker [का उपयोग करके]{lang=hi}**

```sh
docker ps | grep pgmoneta -->
```

- **Podman [का उपयोग करके]{lang=hi}**
```sh
podman ps | grep pgmoneta
```

[किसी भी त्रुटि के लिए लॉग की जाँच करें:]{lang=hi}

- **Docker [का उपयोग करके]{lang=hi}**

```sh
docker logs pgmoneta
```

- **Podman [का उपयोग करके]{lang=hi}**

```sh
podman logs pgmoneta
```

[आप उजागर मीट्रिक्स का निरीक्षण यहां भी कर सकते हैं:]{lang=hi}
```
http://localhost:5001/metrics
```

[आप निम्न का उपयोग करके कंटेनर को बंद कर सकते हैं]{lang=hi}

- **Docker [का उपयोग करके]{lang=hi}**

```sh
docker stop pgmoneta
```

- **Podman [का उपयोग करके]{lang=hi}**

```sh
podman stop pgmoneta
```

[आप कंटेनर में]{lang=hi} exec [कर सकते हैं और]{lang=hi} cli [कमांड को चला सकते हैं]{lang=hi}

```sh
docker exec -it pgmoneta /bin/bash
#or using podman
podman exec -it pgmoneta /bin/bash

cd /etc/pgmoneta
/usr/local/bin/pgmoneta-cli -c pgmoneta.conf shutdown
```

[अधिक]{lang=hi} cli [कमांड के लिए]{lang=hi} [[यह]{lang=hi}](https://github.com/pgmoneta/pgmoneta/blob/main/doc/manual/user-10-cli.md) [देखें।]{lang=hi}

[आप]{lang=hi} `/usr/local/bin` [पर तीनों बाइनरी तक पहुँच सकते हैं।]{lang=hi}
