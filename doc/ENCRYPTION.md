## Overview
AES Cipher block chaining (CBC) mode and AES Counter (CTR) mode are supported in pgmoneta. The default setup is no encryption. 

CBC is the most commonly used and considered save mode. Its main drawbacks are that encryption is sequential (decryption can be parallelized).

Along with CBC, CTR mode is one of two block cipher modes recommended by Niels Ferguson and Bruce Schneier. Both encryption and decryption are parallelizable.

Longer the key length, safer the encryption. However, with 20% (192 bit) and 40% (256 bit) extra workload compare to 128 bit.

## Encryption Configuration
`none`: No encryption (default value)

`aes | aes-256 | aes-256-cbc`: AES CBC (Cipher Block Chaining) mode with 256 bit key length

`aes-192 | aes-192-cbc`: AES CBC mode with 192 bit key length

`aes-128 | aes-128-cbc`: AES CBC mode with 128 bit key length

`aes-256-ctr`: AES CTR (Counter) mode with 256 bit key length

`aes-192-ctr`: AES CTR mode with 192 bit key length

`aes-128-ctr`: AES CTR mode with 128 bit key length

## Encryption / Decryption CLI Commands
### decrypt
Decrypt the file in place, remove encrypted file after successful decryption.

Command

```
pgmoneta-cli decrypt <file>
```


## Benchmark
Check if your CPU have [AES-NI](https://en.wikipedia.org/wiki/AES_instruction_set)
```sh
cat /proc/cpuinfo | grep aes
```

Query number of cores on your CPU
```sh
lscpu | grep '^CPU(s):'
```

By default openssl using AES-NI if the CPU have it.
```sh
openssl speed -elapsed -evp aes-128-cbc
```

Speed test with explicit disabled AES-NI feature
```sh
OPENSSL_ia32cap="~0x200000200000000" openssl speed -elapsed -evp aes-128-cbc
```

Test decrypt
```sh
openssl speed -elapsed -decrypt -evp aes-128-cbc
```

Speed test with 8 cores
```
openssl speed -multi 8 -elapsed -evp aes-128-cbc
```


```console
Architecture:            x86_64
  CPU op-mode(s):        32-bit, 64-bit
  Address sizes:         39 bits physical, 48 bits virtual
  Byte Order:            Little Endian
CPU(s):                  12
  On-line CPU(s) list:   0-11
Vendor ID:               GenuineIntel
  Model name:            Intel(R) Core(TM) i7-9750H CPU @ 2.60GHz
    CPU family:          6
    Model:               158
    Thread(s) per core:  2
    Core(s) per socket:  6
    Socket(s):           1
    Stepping:            10
    BogoMIPS:            5183.98
    Flags:               fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx fxsr sse sse2 s
                         s ht syscall nx pdpe1gb rdtscp lm constant_tsc rep_good nopl xtopology cpuid pni pclmulqdq vmx ssse
                         3 fma cx16 pcid sse4_1 sse4_2 movbe popcnt aes xsave avx f16c rdrand hypervisor lahf_lm abm 3dnowpr
                         efetch invpcid_single pti ssbd ibrs ibpb stibp tpr_shadow vnmi ept vpid ept_ad fsgsbase bmi1 avx2 s
                         mep bmi2 erms invpcid rdseed adx smap clflushopt xsaveopt xsavec xgetbv1 xsaves flush_l1d arch_capa
                         bilities
Virtualization features: 
  Virtualization:        VT-x
  Hypervisor vendor:     Microsoft
  Virtualization type:   full
Caches (sum of all):     
  L1d:                   192 KiB (6 instances)
  L1i:                   192 KiB (6 instances)
  L2:                    1.5 MiB (6 instances)
  L3:                    12 MiB (1 instance)
Vulnerabilities:         
  Itlb multihit:         KVM: Mitigation: VMX disabled
  L1tf:                  Mitigation; PTE Inversion; VMX conditional cache flushes, SMT vulnerable
  Mds:                   Vulnerable: Clear CPU buffers attempted, no microcode; SMT Host state unknown
  Meltdown:              Mitigation; PTI
  Spec store bypass:     Mitigation; Speculative Store Bypass disabled via prctl and seccomp
  Spectre v1:            Mitigation; usercopy/swapgs barriers and __user pointer sanitization
  Spectre v2:            Mitigation; Full generic retpoline, IBPB conditional, IBRS_FW, STIBP conditional, RSB filling
  Srbds:                 Unknown: Dependent on hypervisor status
  Tsx async abort:       Not affected

openssl version: 3.0.5
built on: Tue Jul  5 00:00:00 2022 UTC
options: bn(64,64)
compiler: gcc -fPIC -pthread -m64 -Wa,--noexecstack -O2 -flto=auto -ffat-lto-objects -fexceptions -g -grecord-gcc-switches -pipe -Wall -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2 -Wp,-D_GLIBCXX_ASSERTIONS -specs=/usr/lib/rpm/redhat/redhat-hardened-cc1 -fstack-protector-strong -specs=/usr/lib/rpm/redhat/redhat-annobin-cc1  -m64  -mtune=generic -fasynchronous-unwind-tables -fstack-clash-protection -fcf-protection -O2 -flto=auto -ffat-lto-objects -fexceptions -g -grecord-gcc-switches -pipe -Wall -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2 -Wp,-D_GLIBCXX_ASSERTIONS -specs=/usr/lib/rpm/redhat/redhat-hardened-cc1 -fstack-protector-strong -specs=/usr/lib/rpm/redhat/redhat-annobin-cc1 -m64 -mtune=generic -fasynchronous-unwind-tables -fstack-clash-protection -fcf-protection -Wa,--noexecstack -Wa,--generate-missing-build-notes=yes -specs=/usr/lib/rpm/redhat/redhat-hardened-ld -specs=/usr/lib/rpm/redhat/redhat-annobin-cc1 -DOPENSSL_USE_NODELETE -DL_ENDIAN -DOPENSSL_PIC -DOPENSSL_BUILDING_OPENSSL -DZLIB -DNDEBUG -DPURIFY -DDEVRANDOM="\"/dev/urandom\"" -DSYSTEM_CIPHERS_FILE="/etc/crypto-policies/back-ends/openssl.config"
The 'numbers' are in 1000s of bytes per second processed.
type             16 bytes     64 bytes    256 bytes   1024 bytes   8192 bytes  16384 bytes
AES-128-CBC *   357381.06k   414960.06k   416301.23k   416687.10k   416175.45k   416268.29k
AES-128-CBC     902160.83k  1496344.68k  1514778.62k  1555236.52k  1542537.22k  1569259.52k
AES-128-CBC d   909710.79k  2941259.46k  5167110.31k  5927086.76k  6365967.70k  6349198.68k
AES-128-CBC 8  3912786.36k  8042348.31k  9870507.86k 10254096.38k 10653332.82k 10310331.05k
AES-128-CBC 8d 4157037.26k 12337480.36k 26613686.27k 29902703.27k 32306793.13k 31440366.25k

AES-128-CTR *   146971.83k   165696.94k   574871.64k   634507.61k   676448.94k   668139.52k
AES-128-CTR     887783.06k  2255074.22k  4800168.19k  5930596.01k  6431110.49k  6376062.98k
AES-128-CTR d   793432.63k  2181439.06k  4541298.09k  5743022.42k  6480090.45k  6271221.76k
AES-128-CTR 8  3833975.47k 10832239.55k 23757293.40k 28413146.79k 30514317.99k 30092356.27k
AES-128-CTR 8d 3456838.44k  9749773.91k 22107652.18k 27229352.28k 30703026.18k 29387025.07k

AES-192-CBC     853380.50k  1238507.90k  1299788.12k  1257189.03k  1272591.70k  1271840.77k
AES-192-CBC d   876094.29k  2843770.82k  4523019.52k  5177496.92k  5442652.84k  5372559.36k
AES-192-CTR     869039.84k  2285946.18k  4229439.91k  5049118.04k  5422994.77k  5309748.57k
AES-192-CTR d   789470.51k  2177050.05k  4194812.76k  4935891.63k  5257865.90k  5323046.91k

AES-256-CBC     834298.24k  1100648.64k  1117826.90k  1104301.40k  1130657.11k  1097285.63k
AES-256-CBC d   843079.68k  2714917.67k  4084088.23k  4510005.59k  4557821.27k  4594783.57k
AES-256-CTR     811325.74k  2222582.89k  3749333.08k  4412143.27k  4640549.55k  4554828.46k
AES-256-CTR d   730844.97k  2081179.20k  3673258.15k  4346793.64k  4515722.58k  4594335.74k
```
*: AES-NI disabled; 8: 8 cores; d: decryption