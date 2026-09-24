## Cifrado

### Descripción general

AES-GCM (Galois/Counter Mode) es el modo de cifrado recomendado en [**pgmoneta**][pgmoneta]. Proporciona tanto confidencialidad (cifrado) como integridad/autenticidad (verificación), asegurando que los datos cifrados no hayan sido manipulados.

### Configuración de cifrado

`none`: Sin cifrado (valor predeterminado)

`aes | aes-256 | aes-256-gcm`: Modo AES-256 GCM con longitud de clave de 256 bits (Recomendado)

`aes-192 | aes-192-gcm`: Modo AES-192 GCM con longitud de clave de 192 bits

`aes-128 | aes-128-gcm`: Modo AES-128 GCM con longitud de clave de 128 bits

### Comandos CLI de cifrado/descifrado

**decrypt**

Descifra el archivo directamente en su ubicación y elimina el archivo cifrado tras un descifrado exitoso.

Comando

``` sh
pgmoneta-cli decrypt <file>
```

**encrypt**

Cifra el archivo directamente en su ubicación y elimina el archivo sin cifrar tras un cifrado exitoso.

Comando

``` sh
pgmoneta-cli encrypt <file>
```

### Implementación técnica

#### Formato de archivo (desde 0.21.0)

Cada archivo cifrado comienza con un encabezado unificado de 28 bytes:

| Desplazamiento | Longitud | Descripción |
|----------------|----------|-------------|
| 0              | 16       | Salt utilizado para la derivación de claves PBKDF2 |
| 16             | 12       | Campo del Vector de Inicialización (IV) |

Se genera un IV aleatorio único de 12 bytes para cada operación de cifrado y se almacena directamente después del salt. La **Etiqueta de Autenticación (16 bytes)** se añade al **final del archivo** (después del texto cifrado).

Los datos cifrados reales van después del encabezado y (para GCM) antes de la etiqueta.

#### Derivación de claves y almacenamiento en caché

Para cifrar muchos archivos eficientemente sin incurrir en el costo computacional de miles de iteraciones por cada archivo, `pgmoneta` utiliza un proceso de derivación de claves en dos pasos:

1. **Derivación de la clave maestra (lenta):** La clave maestra se deriva a partir de la contraseña provista por el usuario y un **salt generado aleatoriamente** (almacenado en `master.key`) mediante `PKCS5_PBKDF2_HMAC` (SHA-256) con un número elevado de iteraciones (600,000). Esto ofrece una gran resistencia frente a ataques de fuerza bruta. La presencia de un salt aleatorio en el archivo `master.key` es **obligatoria**. Los archivos de clave antiguos que solo contenían una contraseña ya no son compatibles y deben regenerarse utilizando `pgmoneta-admin user master-key`.
2. **Almacenamiento en caché de la clave:** Esta clave maestra se mantiene en caché en memoria volátil durante el tiempo que dure la transmisión del backup o restore, eliminando la sobrecarga de repetir la costosa operación PBKDF2.
3. **Derivación de la clave de archivo (rápida):** Para cada archivo individual, se genera un salt aleatorio único y un Vector de Inicialización (IV). Posteriormente, se deriva una clave específica para dicho archivo a partir de la clave maestra en caché y el salt del archivo mediante `PKCS5_PBKDF2_HMAC` con 1 sola iteración. Esto asegura que cada archivo permanezca criptográficamente aislado.

Durante el descifrado, `pgmoneta` lee el salt y el IV de la cabecera del archivo. Si la clave maestra aún no ha sido cargada en caché, realiza la derivación lenta. A continuación, utiliza la clave maestra en caché, el salt de la cabecera del archivo y 1 iteración para derivar rápidamente la clave de archivo correcta.

### Evaluación comparativa

Verifica si tu CPU cuenta con [AES-NI][aes_ni]

```sh
cat /proc/cpuinfo | grep aes
```

Consulta el número de núcleos en tu CPU

```sh
lscpu | grep '^CPU(s):'
```

Por defecto, openssl utiliza AES-NI si la CPU dispone de dicha instrucción.

```sh
openssl speed -elapsed -evp aes-256-gcm
```

Prueba de velocidad con la característica AES-NI explícitamente deshabilitada

```sh
OPENSSL_ia32cap="~0x200000200000000" openssl speed -elapsed -evp aes-256-gcm
```

Prueba de descifrado

```sh
openssl speed -elapsed -decrypt -evp aes-256-gcm
```

Prueba de velocidad con 8 núcleos

``` sh
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

*: AES-NI deshabilitado; 8: 8 núcleos; d: descifrado
