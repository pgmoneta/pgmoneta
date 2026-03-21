## RPM

[**pgmoneta**][pgmoneta] puede construirse en un RPM para sistemas [Fedora][fedora].

### Requisitos

```sh
dnf install gcc rpm-build rpm-devel rpmlint make python bash coreutils diffutils patch rpmdevtools chrpath
```

### Configurar desarrollo de RPM

```sh
rpmdev-setuptree
```

### Crear paquete fuente

```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make package_source
```

### Crear paquete RPM

```sh
cp pgmoneta-$VERSION.tar.gz ~/rpmbuild/SOURCES
QA_RPATHS=0x0001 rpmbuild -bb pgmoneta.spec
```

El RPM resultante se ubicará en `~/rpmbuild/RPMS/x86_64/`, si tu arquitectura es `x86_64`.
