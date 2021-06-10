Name:          pgmoneta
Version:       0.2.1
Release:       1%{dist}
Summary:       Backup / restore for PostgreSQL
License:       BSD
URL:           https://github.com/pgmoneta/pgmoneta
Source0:       https://github.com/pgmoneta/pgmoneta/archive/%{version}.tar.gz

BuildRequires: gcc cmake make python3-docutils zlib zlib-devel
BuildRequires: libev libev-devel openssl openssl-devel systemd systemd-devel
Requires:      libev openssl systemd postgresql zlib

%description
pgmoneta is a backup / restore solution for PostgreSQL.

%prep
%setup -q

%build

%{__mkdir} build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
%{__make}

%install

%{__mkdir} -p %{buildroot}%{_sysconfdir}
%{__mkdir} -p %{buildroot}%{_bindir}
%{__mkdir} -p %{buildroot}%{_libdir}
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/etc
%{__mkdir} -p %{buildroot}%{_mandir}/man1
%{__mkdir} -p %{buildroot}%{_mandir}/man5
%{__mkdir} -p %{buildroot}%{_sysconfdir}/pgmoneta

%{__install} -m 644 %{_builddir}/%{name}-%{version}/LICENSE %{buildroot}%{_docdir}/%{name}/LICENSE
%{__install} -m 644 %{_builddir}/%{name}-%{version}/CODE_OF_CONDUCT.md %{buildroot}%{_docdir}/%{name}/CODE_OF_CONDUCT.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/README.md %{buildroot}%{_docdir}/%{name}/README.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/ARCHITECTURE.md %{buildroot}%{_docdir}/%{name}/ARCHITECTURE.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/CONFIGURATION.md %{buildroot}%{_docdir}/%{name}/CONFIGURATION.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/GETTING_STARTED.md %{buildroot}%{_docdir}/%{name}/GETTING_STARTED.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/RPM.md %{buildroot}%{_docdir}/%{name}/RPM.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgmoneta.service %{buildroot}%{_docdir}/%{name}/etc/pgmoneta.service

%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgmoneta.conf %{buildroot}%{_sysconfdir}/pgmoneta/pgmoneta.conf

%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgmoneta.1 %{buildroot}%{_mandir}/man1/pgmoneta.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgmoneta-admin.1 %{buildroot}%{_mandir}/man1/pgmoneta-admin.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgmoneta-cli.1 %{buildroot}%{_mandir}/man1/pgmoneta-cli.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgmoneta.conf.5 %{buildroot}%{_mandir}/man5/pgmoneta.conf.5

%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgmoneta %{buildroot}%{_bindir}/pgmoneta
%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgmoneta-cli %{buildroot}%{_bindir}/pgmoneta-cli
%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgmoneta-admin %{buildroot}%{_bindir}/pgmoneta-admin

%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/libpgmoneta.so.%{version} %{buildroot}%{_libdir}/libpgmoneta.so.%{version}

chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgmoneta
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgmoneta-cli
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgmoneta-admin

cd %{buildroot}%{_libdir}/
%{__ln_s} -f libpgmoneta.so.%{version} libpgmoneta.so.0
%{__ln_s} -f libpgmoneta.so.0 libpgmoneta.so

%files
%license %{_docdir}/%{name}/LICENSE
%{_docdir}/%{name}/ARCHITECTURE.md
%{_docdir}/%{name}/CODE_OF_CONDUCT.md
%{_docdir}/%{name}/CONFIGURATION.md
%{_docdir}/%{name}/GETTING_STARTED.md
%{_docdir}/%{name}/README.md
%{_docdir}/%{name}/RPM.md
%{_docdir}/%{name}/etc/pgmoneta.service
%{_mandir}/man1/pgmoneta.1*
%{_mandir}/man1/pgmoneta-admin.1*
%{_mandir}/man1/pgmoneta-cli.1*
%{_mandir}/man5/pgmoneta.conf.5*
%config %{_sysconfdir}/pgmoneta/pgmoneta.conf
%{_bindir}/pgmoneta
%{_bindir}/pgmoneta-cli
%{_bindir}/pgmoneta-admin
%{_libdir}/libpgmoneta.so
%{_libdir}/libpgmoneta.so.0
%{_libdir}/libpgmoneta.so.%{version}

%changelog
