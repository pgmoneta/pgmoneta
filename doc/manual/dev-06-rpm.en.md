\newpage

# RPM

`pgmoneta` can be built into a RPM for [Fedora](https://getfedora.org/) systems.

## Requirements

```sh
dnf install gcc rpm-build rpm-devel rpmlint make python bash coreutils diffutils patch rpmdevtools chrpath
```

## Setup RPM development

```sh
rpmdev-setuptree
```

## Create source package

```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make package_source
```

## Create RPM package

```sh
cp pgmoneta-$VERSION.tar.gz ~/rpmbuild/SOURCES
QA_RPATHS=0x0001 rpmbuild -bb pgmoneta.spec
```

The resulting RPM will be located in `~/rpmbuild/RPMS/x86_64/`, if your architecture is `x86_64`.
