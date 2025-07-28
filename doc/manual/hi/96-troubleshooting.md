\newpage

# [समस्या निवारण]{lang=hi}

## [सर्वर के लिए संस्करण प्राप्त नहीं कर सके]{lang=hi}

[यदि आपको स्टार्टअप के दौरान]{lang=hi} `FATAL` [संदेश मिलता है, तो अपने]{lang=hi} PostgreSQL [लॉगिन्स की जांच करें:]{lang=hi}

```
psql postgres
```

[और]{lang=hi}

```
psql -U repl postgres
```

[फिर,]{lang=hi} PostgreSQL [लॉग्स में किसी भी त्रुटि की जांच करें।]{lang=hi}

[अगर फिर भी समस्या बनी रहती है, तो]{lang=hi} `pgmoneta.conf` [में]{lang=hi} `log_level` [को]{lang=hi} `DEBUG5` [पर सेट करने से त्रुटि के बारे में अधिक जानकारी मिल सकती है।]{lang=hi}
