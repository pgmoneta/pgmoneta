# SSH Storage Engine Configuration

## Prerequisites
First of all, you need to have a remote server where you can store your backups on.

Lets take an EC2 instance as an example, after launching an EC2 instance you need to add new user account with SSH access to the EC2 instance:

1. [Connect to your Linux instance using SSH.](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/AccessingInstancesLinux.html)

2. Use the adduser command to add a new user account to an EC2 instance (replace new_user with the new account name).

```
$ sudo adduser new_user --disabled-password
```

3.  Change the security context to the new_user account so that folders and files you create have the correct permissions:

```
$ sudo su - new_user
```

4. Create a .ssh directory in the new_user home directory and use the chmod command to change the .ssh directory's permissions to 700:

```
$ mkdir .ssh && chmod 700 .ssh
```

5. Use the touch command to create the authorized_keys file in the .ssh directory and use the chmod command to change the .ssh/authorized_keys file permissions to 600:

```
$ touch .ssh/authorized_keys && chmod 600 .ssh/authorized_keys
```

6. Retrieve the public key for the key pair in your local computer:

```
cat ~/.ssh/id_rsa.pub
```

7. In the EC2 instance, run the cat command in append mode:

```
$ cat >> .ssh/authorized_keys
```

8. Paste the public key into the .ssh/authorized_keys file and then press Enter.

9. Press and hold Ctrl+d to exit cat and return to the command line session prompt.

To verify that the new user can use SSH to connect to the EC2 instance, run the following command from a command line prompt on your local computer:

```
ssh new_user@public_dns_name_of_EC2_instance
```

## Modify the pgmoneta configuration

You need to create a directory on your remote server where backups can be stored in.

In addition, your local computer needs to have a storage space for 1 backup.

Change `pgmoneta.conf` to add

```
storage_engine = ssh
ssh_hostname =  your-public_dns_name_of_EC2_instance
ssh_username = new_user
ssh_base_dir = the-path-of-the-directory-where-backups-stored-in
```

under the `[pgmoneta]` section.

## Optional: Custom SSH Key Files

By default, pgmoneta uses the standard SSH key pair located at `~/.ssh/id_rsa` (private key) and `~/.ssh/id_rsa.pub` (public key).

If you need to use a different SSH key pair, you can specify custom paths:

```
ssh_public_key_file = /path/to/public_key
ssh_private_key_file = /path/to/private_key
```

Both parameters support environment variable interpolation (e.g., `$HOME`, `$USER`).
