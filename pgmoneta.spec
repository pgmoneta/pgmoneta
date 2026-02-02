Name:          pgmoneta
Version:       0.21.0
Release:       1%{dist}
Summary:       Backup / restore for PostgreSQL
License:       BSD
URL:           https://github.com/pgmoneta/pgmoneta
Source0:       %{name}-%{version}.tar.gz

BuildRequires: gcc cmake make python3-docutils zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel bzip2 bzip2-devel ncurses-devel
BuildRequires: libev libev-devel openssl openssl-devel systemd systemd-devel libssh libssh-devel libarchive libarchive-devel liburing-devel
Requires:      libev openssl systemd zlib libzstd lz4 bzip2 libssh libarchive liburing

%description
pgmoneta is a backup / restore solution for PostgreSQL.

%prep
%setup -q

%build

%{__mkdir} build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DDOCS=FALSE ..
%{__make}

%install

%{__mkdir} -p %{buildroot}%{_sysconfdir}
%{__mkdir} -p %{buildroot}%{_bindir}
%{__mkdir} -p %{buildroot}%{_libdir}
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/etc
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/shell_comp
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/images
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/manual/en
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/grafana
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/grafana/provisioning/dashboards
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/grafana/provisioning/datasources
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/valgrind
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/prometheus_scrape
%{__mkdir} -p %{buildroot}%{_mandir}/man1
%{__mkdir} -p %{buildroot}%{_mandir}/man5
%{__mkdir} -p %{buildroot}%{_sysconfdir}/pgmoneta

%{__install} -m 644 %{_builddir}/%{name}-%{version}/LICENSE %{buildroot}%{_docdir}/%{name}/LICENSE
%{__install} -m 644 %{_builddir}/%{name}-%{version}/CODE_OF_CONDUCT.md %{buildroot}%{_docdir}/%{name}/CODE_OF_CONDUCT.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/README.md %{buildroot}%{_docdir}/%{name}/README.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/ARCHITECTURE.md %{buildroot}%{_docdir}/%{name}/ARCHITECTURE.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/Azure.md %{buildroot}%{_docdir}/%{name}/Azure.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/CLI.md %{buildroot}%{_docdir}/%{name}/CLI.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/CONFIGURATION.md %{buildroot}%{_docdir}/%{name}/CONFIGURATION.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/DEVELOPERS.md %{buildroot}%{_docdir}/%{name}/DEVELOPERS.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/DISTRIBUTIONS.md %{buildroot}%{_docdir}/%{name}/DISTRIBUTIONS.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/ENCRYPTION.md %{buildroot}%{_docdir}/%{name}/ENCRYPTION.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/GETTING_STARTED.md %{buildroot}%{_docdir}/%{name}/GETTING_STARTED.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/PR_GUIDE.md %{buildroot}%{_docdir}/%{name}/PR_GUIDE.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/RPM.md %{buildroot}%{_docdir}/%{name}/RPM.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/S3.md %{buildroot}%{_docdir}/%{name}/S3.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/SSH.md %{buildroot}%{_docdir}/%{name}/SSH.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/WAL.md %{buildroot}%{_docdir}/%{name}/WAL.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/WALFILTER.md %{buildroot}%{_docdir}/%{name}/WALFILTER.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/WALINFO.md %{buildroot}%{_docdir}/%{name}/WALINFO.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/PROMETHEUS.md %{buildroot}%{_docdir}/%{name}/PROMETHEUS.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/TEST.md %{buildroot}%{_docdir}/%{name}/TEST.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/TEST_ENVS.md %{buildroot}%{_docdir}/%{name}/TEST_ENVS.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgmoneta.service %{buildroot}%{_docdir}/%{name}/etc/pgmoneta.service

%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/*.md %{buildroot}%{_docdir}/%{name}/manual/en/
%{__mkdir_p} %{buildroot}%{_docdir}/%{name}/images/
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/images/grafana_datasource.jpg %{buildroot}%{_docdir}/%{name}/images/grafana_datasource.jpg
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/images/grafana_dashboard.jpg %{buildroot}%{_docdir}/%{name}/images/grafana_dashboard.jpg
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/images/prometheus_console.jpg %{buildroot}%{_docdir}/%{name}/images/prometheus_console.jpg
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/images/prometheus_graph.jpg %{buildroot}%{_docdir}/%{name}/images/prometheus_graph.jpg

%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgmoneta.conf %{buildroot}%{_sysconfdir}/pgmoneta/pgmoneta.conf
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgmoneta_walfilter.conf %{buildroot}%{_sysconfdir}/pgmoneta/pgmoneta_walfilter.conf
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgmoneta_walinfo.conf %{buildroot}%{_sysconfdir}/pgmoneta/pgmoneta_walinfo.conf

%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/shell_comp/pgmoneta_comp.bash %{buildroot}%{_docdir}/%{name}/shell_comp/pgmoneta_comp.bash
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/shell_comp/pgmoneta_comp.zsh %{buildroot}%{_docdir}/%{name}/shell_comp/pgmoneta_comp.zsh

%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/*.json %{buildroot}%{_docdir}/%{name}/grafana/
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/README.md %{buildroot}%{_docdir}/%{name}/grafana/README.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/TESTING.md %{buildroot}%{_docdir}/%{name}/grafana/TESTING.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/GETTING_DATA.md %{buildroot}%{_docdir}/%{name}/grafana/GETTING_DATA.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/prometheus.yml %{buildroot}%{_docdir}/%{name}/grafana/prometheus.yml
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/docker-compose.yml %{buildroot}%{_docdir}/%{name}/grafana/docker-compose.yml
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/provisioning/dashboards/dashboards.yaml %{buildroot}%{_docdir}/%{name}/grafana/provisioning/dashboards/dashboards.yaml
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/provisioning/datasources/datasources.yaml %{buildroot}%{_docdir}/%{name}/grafana/provisioning/datasources/datasources.yaml
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/valgrind/pgmoneta.supp %{buildroot}%{_docdir}/%{name}/valgrind/pgmoneta.supp
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/valgrind/README.md %{buildroot}%{_docdir}/%{name}/valgrind/README.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/prometheus_scrape/* %{buildroot}%{_docdir}/%{name}/prometheus_scrape/

%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgmoneta.1 %{buildroot}%{_mandir}/man1/pgmoneta.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgmoneta-admin.1 %{buildroot}%{_mandir}/man1/pgmoneta-admin.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgmoneta-cli.1 %{buildroot}%{_mandir}/man1/pgmoneta-cli.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgmoneta.conf.5 %{buildroot}%{_mandir}/man5/pgmoneta.conf.5
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgmoneta-cli.conf.5 %{buildroot}%{_mandir}/man5/pgmoneta-cli.conf.5
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgmoneta-walfilter.1 %{buildroot}%{_mandir}/man1/pgmoneta-walfilter.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgmoneta-walinfo.1 %{buildroot}%{_mandir}/man1/pgmoneta-walinfo.1

%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgmoneta %{buildroot}%{_bindir}/pgmoneta
%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgmoneta-cli %{buildroot}%{_bindir}/pgmoneta-cli
%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgmoneta-admin %{buildroot}%{_bindir}/pgmoneta-admin
%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgmoneta-walfilter %{buildroot}%{_bindir}/pgmoneta-walfilter
%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgmoneta-walinfo %{buildroot}%{_bindir}/pgmoneta-walinfo

%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/libpgmoneta.so.%{version} %{buildroot}%{_libdir}/libpgmoneta.so.%{version}

chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgmoneta
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgmoneta-cli
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgmoneta-admin
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgmoneta-walfilter
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgmoneta-walinfo

cd %{buildroot}%{_libdir}/
%{__ln_s} -f libpgmoneta.so.%{version} libpgmoneta.so.0
%{__ln_s} -f libpgmoneta.so.0 libpgmoneta.so

%files
%license %{_docdir}/%{name}/LICENSE
%{_docdir}/%{name}/ARCHITECTURE.md
%{_docdir}/%{name}/Azure.md
%{_docdir}/%{name}/CODE_OF_CONDUCT.md
%{_docdir}/%{name}/CLI.md
%{_docdir}/%{name}/CONFIGURATION.md
%{_docdir}/%{name}/DEVELOPERS.md
%{_docdir}/%{name}/DISTRIBUTIONS.md
%{_docdir}/%{name}/ENCRYPTION.md
%{_docdir}/%{name}/GETTING_STARTED.md
%{_docdir}/%{name}/PR_GUIDE.md
%{_docdir}/%{name}/README.md
%{_docdir}/%{name}/RPM.md
%{_docdir}/%{name}/S3.md
%{_docdir}/%{name}/SSH.md
%{_docdir}/%{name}/WAL.md
%{_docdir}/%{name}/WALFILTER.md
%{_docdir}/%{name}/WALINFO.md
%{_docdir}/%{name}/PROMETHEUS.md
%{_docdir}/%{name}/TEST.md
%{_docdir}/%{name}/TEST_ENVS.md
%{_docdir}/%{name}/manual/en/*.md
%{_docdir}/%{name}/prometheus_scrape/*
%{_docdir}/%{name}/etc/pgmoneta.service
%{_docdir}/%{name}/shell_comp/pgmoneta_comp.bash
%{_docdir}/%{name}/shell_comp/pgmoneta_comp.zsh
%{_docdir}/%{name}/grafana/*
%{_docdir}/%{name}/valgrind/*
%{_mandir}/man1/pgmoneta.1*
%{_mandir}/man1/pgmoneta-admin.1*
%{_mandir}/man1/pgmoneta-cli.1*
%{_mandir}/man5/pgmoneta.conf.5*
%{_mandir}/man5/pgmoneta-cli.conf.5*

%{_docdir}/%{name}/images/grafana_datasource.jpg
%{_docdir}/%{name}/images/grafana_dashboard.jpg
%{_docdir}/%{name}/images/prometheus_console.jpg
%{_docdir}/%{name}/images/prometheus_graph.jpg

%{_mandir}/man1/pgmoneta-walfilter.1*
%{_mandir}/man1/pgmoneta-walinfo.1*
%config %{_sysconfdir}/pgmoneta/pgmoneta.conf
%config %{_sysconfdir}/pgmoneta/pgmoneta_walfilter.conf
%config %{_sysconfdir}/pgmoneta/pgmoneta_walinfo.conf
%{_bindir}/pgmoneta
%{_bindir}/pgmoneta-cli
%{_bindir}/pgmoneta-admin
%{_bindir}/pgmoneta-walfilter
%{_bindir}/pgmoneta-walinfo
%{_libdir}/libpgmoneta.so
%{_libdir}/libpgmoneta.so.0
%{_libdir}/libpgmoneta.so.%{version}

%changelog
