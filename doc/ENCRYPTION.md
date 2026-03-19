## Overview
AES-GCM (Galois/Counter Mode) is the recommended encryption mode in pgmoneta when encryption is enabled. It provides both confidentiality (encryption) and integrity/authenticity (verification), ensuring that encrypted data has not been tampered with. The default setup is no encryption.


## Encryption Configuration
`none`: No encryption (default value)

`aes | aes-256 | aes-256-gcm`: AES-256 GCM mode with 256-bit key length (recommended)

`aes-192 | aes-192-gcm`: AES-192 GCM mode with 192-bit key length

`aes-128 | aes-128-gcm`: AES-128 GCM mode with 128-bit key length

## Encryption / Decryption CLI Commands
### decrypt
Decrypt the file in place, remove encrypted file after successful decryption.

Command

```
pgmoneta-cli decrypt <file>
```

### encrypt
Encrypt the file in place, remove unencrypted file after successful encryption.

Command

```
pgmoneta-cli encrypt <file>
```

## Technical Implementation

### File Format (Since 0.21.0)

Each encrypted file starts with a 32-byte header:

| Offset | Length | Description |
|--------|--------|-------------|
| 0      | 16     | Salt used for PBKDF2 key derivation |
| 16     | 16     | Initialization Vector (IV) field (zero-padded) |

**Note on AES-GCM**:
*   A unique, random IV (12 bytes for GCM) is generated for every encryption operation and stored in the fixed 16-byte field with the remaining bytes zero-padded.
*   The **Authentication Tag (16 bytes)** is stored at the **end of the file** (after the ciphertext).

The actual encrypted data follows immediately after the header and (for GCM) before the tag.

### Key Derivation and Caching

To encrypt many files efficiently without paying the computational cost of thousands of iterations for every file, `pgmoneta` uses a two-step key derivation process:

1. **Master Key Derivation (Slow):** The master key is derived from the user-provided password and a unique, random 16-byte salt (stored in `master.key`) using `PKCS5_PBKDF2_HMAC` (SHA-256) with a high number of iterations (600,000). This provides strong resistance against brute-force and rainbow table attacks.
2. **Key Caching:** This master key is cached in volatile memory for the duration of the process, eliminating the overhead of repeating the expensive PBKDF2 operation. Sensitive materials are automatically wiped from memory upon process termination using the GNU C destructor pattern.
3. **File Key Derivation (Fast):** For every individual file, a unique random salt and Initialization Vector (IV) are generated. A file-specific key is then derived from the cached master key and the random file salt using `PKCS5_PBKDF2_HMAC` with 1 iteration. This ensures every file is cryptographically isolated.

During decryption, `pgmoneta` reads the salt and IV from the file header. If the master key has not been cached yet, it performs the slow derivation. Then, it uses the cached master key, the file's header salt, and 1 iteration to quickly derive the correct file key.

## Benchmark
Check if your CPU has [AES-NI](https://en.wikipedia.org/wiki/AES_instruction_set)
```sh
cat /proc/cpuinfo | grep aes
```

Query number of cores on your CPU
```sh
lscpu | grep '^CPU(s):'
```

By default openssl using AES-NI if the CPU have it.
```sh
openssl speed -elapsed -evp aes-256-gcm
```
 
Speed test with explicit disabled AES-NI feature
```sh
OPENSSL_ia32cap="~0x200000200000000" openssl speed -elapsed -evp aes-256-gcm
```
 
Test decrypt
```sh
openssl speed -elapsed -decrypt -evp aes-256-gcm
```
 
Speed test with 8 cores
```
openssl speed -multi 8 -elapsed -evp aes-256-gcm
```

```console
Architecture:            x86_64
  CPU op-mode(s):        32-bit, 64-bit
  Address sizes:         39 bits physical, 48 bits virtual
  Byte Order:            Little Endian

CPU(s):                  8
  On-line CPU(s) list:   0-7
Vendor ID:               GenuineIntel
  Model name:            11th Gen Intel(R) Core(TM) i5-1135G7 @ 2.40GHz
    CPU family:          6
    Model:               140
    Thread(s) per core:  2
    Core(s) per socket:  4
    Socket(s):           1
    Stepping:            1
    CPU max MHz:         4200.0000
    CPU min MHz:         400.0000
    BogoMIPS:            4838.40
    Flags:               fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx pdpe1gb rdtscp lm constant_tsc art arch_perfmon pebs bts rep_good nopl xtopology nonstop_tsc cpuid aperfmperf tsc_known_freq pni pclmulqdq dtes64 monitor ds_cpl vmx est tm2 ssse3 sdbg fma cx16 xtpr pdcm pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand lahf_lm abm 3dnowprefetch cpuid_fault epb cat_l2 cdp_l2 ssbd ibrs ibpb stibp ibrs_enhanced tpr_shadow flexpriority ept vpid ept_ad fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid rdt_a avx512f avx512dq rdseed adx smap avx512ifma clflushopt clwb intel_pt avx512cd sha_ni avx512bw avx512vl xsaveopt xsavec xgetbv1 xsaves split_lock_detect user_shstk dtherm ida arat pln pts hwp hwp_notify hwp_act_window hwp_epp hwp_pkg_req vnmi avx512vbmi umip pku ospke avx512_vbmi2 gfni vaes vpclmulqdq avx512_vnni avx512_bitalg avx512_vpopcntdq rdpid movdiri movdir64b fsrm avx512_vp2intersect md_clear ibt flush_l1d arch_capabilities

openssl version: 3.0.13
built on: Mon Jan 26 12:31:31 2026 UTC
options: bn(64,64)

The 'numbers' are in 1000s of bytes per second processed.
type             16 bytes     64 bytes    256 bytes   1024 bytes   8192 bytes  16384 bytes
AES-128-GCM *    47737.62k    52871.51k   135023.27k   137175.72k   143777.79k   148264.28k
AES-128-GCM     653473.61k  2678628.89k  5560273.07k  7679196.84k 11830244.69k 11941167.10k
AES-128-GCM 8  2486223.05k  9099276.86k 19708302.51k 23132541.27k 35914263.21k 37839552.51k

AES-192-GCM     610705.37k  2273716.14k  5115029.25k  7338734.25k 10823898.45k 11447746.56k
AES-192-GCM 8  1997162.17k  7493411.69k 17664965.29k 21421201.75k 33725300.74k 33278711.13k

AES-256-GCM     360398.16k  1378819.35k  2759538.77k  3747126.61k  5768839.17k  6109189.46k
AES-256-GCM d   420415.24k  1641539.11k  3260967.59k  3886553.77k  5839137.45k  6007821.65k
AES-256-GCM 8  1009941.32k  4075486.85k  8428644.42k 10778290.86k 16858777.73k 16209188.18k
```

*: AES-NI disabled; 8: 8 cores; d: decryption

The results show that modern hardware (11th Gen Intel and newer) performs exceptionally well with AES-GCM.
Single-core performance for AES-256-GCM reaches ~6GB/s, while multi-core performance can scale up to ~16GB/s.
This is significantly faster than the previous CBC/CTR modes used on older hardware, providing both high performance and built-in authentication (AEAD).