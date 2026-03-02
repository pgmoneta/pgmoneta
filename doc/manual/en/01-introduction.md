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

[pgmoneta](https://github.com/pgmoneta/pgmoneta) has a [Model Context Protocol](https://modelcontextprotocol.io/)
server called [pgmoneta_mcp](https://github.com/pgmoneta/pgmoneta_mcp).

## Platforms

The supported platforms are

* [Fedora][fedora] 39+
* [RHEL][rhel] 9
* [RockyLinux][rocky] 9
* [FreeBSD][freebsd]
* [OpenBSD][openbsd]

## Migration

### From 0.20.x to 0.21.0

#### Vault Encryption

The key derivation for vault file encryption has been upgraded to
`PKCS5_PBKDF2_HMAC` (SHA-256, random 16-byte salt, 600,000 iterations).

This is a **breaking change**. Existing vault files encrypted with the
old method cannot be decrypted by version 0.21.0.

**Action required:**

1. Stop pgmoneta
2. Delete the existing user files:
   - `pgmoneta_users.conf`
   - `pgmoneta_admins.conf`
   - Vault users file (if applicable)
3. Delete the existing master key:
   ```
   rm ~/.pgmoneta/master.key
   ```
4. Regenerate the master key:
   ```
   pgmoneta-admin master-key
   ```
5. Re-add all users:
   ```
   pgmoneta-admin user add -f <users_file>
   ```
6. Restart pgmoneta
