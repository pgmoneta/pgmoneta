\newpage

# [प्रशासनिक पहुँच]{lang=hi}

[आप]{lang=hi} [**pgmoneta**](pgmoneta) [को एक रिमोट मशीन से एक्सेस कर सकते हैं यदि आप एक्सेस सक्षम करते हैं।]{lang=hi}

## [कंफिगरेशन]{lang=hi}

[पहले, आपको रिमोट एक्सेस सक्षम करने के लिए निम्नलिखित सेटिंग जोड़नी होगी:]{lang=hi}

```
management = 5002
```

[यह सेटिंग]{lang=hi} `pgmoneta.conf` [में]{lang=hi} `[pgmoneta]` [सेक्शन में जोड़ें।]{lang=hi}

## [व्यवस्थापक]{lang=hi}

[इसके बाद, आपको]{lang=hi} `pgmoneta_admins.conf` [में एक या अधिक व्यवस्थापक जोड़ने होंगे, निम्नलिखित कमांड द्वारा:]{lang=hi}

```
pgmoneta-admin -f /etc/pgmoneta/pgmoneta_admins.conf user add
```

[उदाहरण के लिए,]{lang=hi} `admin` [उपयोगकर्ता नाम और]{lang=hi} `secretpassword` [पासवर्ड के साथ।]{lang=hi}

## pgmoneta [को पुनः प्रारंभ करें]{lang=hi}

[आपको]{lang=hi} [**pgmoneta**](pgmoneta) [को पुनः प्रारंभ करना होगा ताकि परिवर्तनों का प्रभाव पड़े।]{lang=hi}

## pgmoneta [से कनेक्ट करें]{lang=hi}

[फिर आप]{lang=hi} `pgmoneta-cli` [टूल का उपयोग करके]{lang=hi} [**pgmoneta**](pgmoneta) [को इस तरह एक्सेस कर सकते हैं:]{lang=hi}

```
pgmoneta-cli -h myhost -p 5002 -U admin status
```

[यह कमांड आपको पासवर्ड दर्ज करने के बाद]{lang=hi} `status` [कमांड को निष्पादित करेगा।]{lang=hi}
