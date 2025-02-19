### प्रशासनिक पहुँच (Administration Access)

आप [**pgmoneta**](pgmoneta) को एक रिमोट मशीन से एक्सेस कर सकते हैं यदि आप एक्सेस सक्षम करते हैं।

#### कंफिगरेशन (Configuration)

पहले, आपको रिमोट एक्सेस सक्षम करने के लिए निम्नलिखित सेटिंग जोड़नी होगी:

```
management = 5002
```

यह सेटिंग `pgmoneta.conf` में `[pgmoneta]` सेक्शन में जोड़ें।

#### व्यवस्थापक (Administrators)

इसके बाद, आपको `pgmoneta_admins.conf` में एक या अधिक व्यवस्थापक जोड़ने होंगे, निम्नलिखित कमांड द्वारा:

```
pgmoneta-admin -f /etc/pgmoneta/pgmoneta_admins.conf user add
```

उदाहरण के लिए, `admin` उपयोगकर्ता नाम और `secretpassword` पासवर्ड के साथ।

#### pgmoneta को पुनः प्रारंभ करें (Restart pgmoneta)

आपको [**pgmoneta**](pgmoneta) को पुनः प्रारंभ करना होगा ताकि परिवर्तनों का प्रभाव पड़े।

#### pgmoneta से कनेक्ट करें (Connect to pgmoneta)

फिर आप `pgmoneta-cli` टूल का उपयोग करके [**pgmoneta**](pgmoneta) को इस तरह एक्सेस कर सकते हैं:

```
pgmoneta-cli -h myhost -p 5002 -U admin status
```

यह कमांड आपको पासवर्ड दर्ज करने के बाद `status` कमांड को निष्पादित करेगा।