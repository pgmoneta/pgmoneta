## Annotate a backup with comments

This tutorial is about adding commnents to a backup.

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+, OpenSSL and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

### Add a comment

You can add a comment by

```
pgmoneta-cli -c pgmoneta.conf annotate primary newest add mykey mycomment
```

### Update a comment

You can update a comment by

```
pgmoneta-cli -c pgmoneta.conf annotate primary newest update mykey mynewcomment
```

### Remove a comment

You can remove a comment by

```
pgmoneta-cli -c pgmoneta.conf annotate primary newest remove mykey
```

### View comments

You can view the comments by

```
pgmoneta-cli -c pgmoneta.conf info primary newest
```
