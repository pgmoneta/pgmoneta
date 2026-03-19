# Migration

## From 0.20.x to 0.21.0

### Backup Rate Limit Configuration

Rate-limit configuration for backups has been consolidated.

This is a **breaking change** for existing configuration files.

`backup_max_rate` and `network_max_rate` are no longer valid keys and
have been replaced by a single `max_rate` key.

`max_rate` is configured in **bytes per second**.

**Action required:**

1. Update `pgmoneta.conf` and replace old keys:
   - `backup_max_rate`
   - `network_max_rate`
2. Set `max_rate` instead (globally and/or per-server).
3. Reload or restart pgmoneta.

Example:

```conf
max_rate = 1000000
```

### Encryption Format

The AES encryption format has been upgraded to use Authenticated Encryption with Associated Data (AEAD) via AES-GCM mode for improved security and data integrity. AES-GCM is now the **only** supported encryption mode for backups.

Legacy modes (**AES-CBC** and **AES-CTR**) have been **removed** for security and performance reasons. AES-GCM is now the only supported encryption method.

Each encrypted file now starts with a unified 32-byte header:
* `Salt` (16 bytes)
* `IV` (16 bytes)

A unique, random Initialization Vector (IV) is generated for every encryption operation and stored in the header. For **AES-GCM**, an additional **Authentication Tag (16 bytes)** is stored at the **end of the data** (after the ciphertext). This unified format is used for streaming, file-based, and one-shot operations.

The PBKDF2 Master Key is now cached in volatile memory for the duration of the backup stream, 
and securely wiped once processing is complete, significantly improving performance for 
large backup sets.

This is a **breaking change**. Backups created with versions prior to 0.21.0 
cannot be decrypted by this version as the internal framing and header format has changed for all encryption modes.

**Action required:**

1. New backups will automatically use the new format.
2. If you need to restore old backups, use the version of pgmoneta they were created with.

### Vault Encryption

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
