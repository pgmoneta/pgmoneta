\newpage

# Introduction

[**pgmoneta**][pgmoneta] is a backup / restore solution for [PostgreSQL][postgresql].

Ideally, you would not need to do backups and disaster recovery, but that isn't how the real World
works.

Possible scenarios that could happen

* Data corruption
* System failure
* Human error
* Natural disaster

and then it is up to the database administrator to get the database system back on-line, and to the
correct recovery point.

Two key factors are

* Recovery Point Objective (RPO): Maximum targeted period in which data might be lost from an IT service due to a major incident
* Recovery Time Objective (RTO): The targeted duration of time and a service level within which a business process must be restored
after a disaster (or disruption) in order to avoid unacceptable consequences associated with a break in business continuity

You would like to have both of these as close to zero as possible, since RPO of 0 means that you won't lose
data, and RTO of 0 means that your system recovers at once. However, that is easier said than done.

[**pgmoneta**][pgmoneta] is focused on having features that will allow database systems to get as close to
these goals as possible such that high availability of 99.99% or more can be implemented, and monitored
through standard tools.

[**pgmoneta**][pgmoneta] is named after the Roman Goddess of Memory.

## Features

* Full backup
* Restore
* Compression (gzip, zstd, lz4, bzip2)
* AES encryption support
* Symlink support
* WAL shipping support
* Hot standby
* Prometheus support
* Remote management
* Offline detection
* Transport Layer Security (TLS) v1.2+ support
* Daemon mode
* User vault

## Platforms

The supported platforms are

* [Fedora][fedora] 38+
* [RHEL][rhel] 9
* [RockyLinux][rocky] 9
* [FreeBSD][freebsd]
* [OpenBSD][openbsd]
