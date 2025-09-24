# Test environments

## S3

### Garage

[Gargage](https://garagehq.deuxfleurs.fr/) is a S3 storage.

[Download](https://garagehq.deuxfleurs.fr/download/) - use Garage 2.x.

**Install binary**

```
mv garage ~/.local/bin/
chmod 755 ~/.local/bin/garage
```

or another directory in your path

**Create configuration**

```
cat > garage.toml <<EOF
metadata_dir = "/tmp/meta"
data_dir = "/tmp/data"
db_engine = "sqlite"

replication_factor = 1

rpc_bind_addr = "[::]:3901"
rpc_public_addr = "127.0.0.1:3901"
rpc_secret = "$(openssl rand -hex 32)"

[s3_api]
s3_region = "garage"
api_bind_addr = "[::]:3900"
root_domain = ".s3.garage.localhost"

[s3_web]
bind_addr = "[::]:3902"
root_domain = ".web.garage.localhost"
index = "index.html"

[k2v_api]
api_bind_addr = "[::]:3904"

[admin]
api_bind_addr = "[::]:3903"
admin_token = "$(openssl rand -base64 32)"
metrics_token = "$(openssl rand -base64 32)"
EOF
```

Then move it to `/etc`, by

```
sudo garage.toml /etc/
```

**Start garage**

```
garage server
```

**Check that it has started**

```
garage status
```

**Create a layout**

```
garage layout assign -z dc1 -c 1G <node_id>
```

Get `<node_id>` from the `status` command.

**Apply the layout**

```
garage layout apply --yes --version 1
```

**Create a bucket for pgmoneta**

```
garage bucket create pgmoneta-bucket
```

**Create a key for pgmoneta**

```
garage key create pgmoneta-app-key
```

**Associate the key**

```
garage bucket allow --read --write --owner pgmoneta-bucket --key pgmoneta-app-key
```

**Install awscli**

```
python -m pip install --user awscli
```

**Configure awscli**

Create `~/.awsrc`

```
export AWS_ACCESS_KEY_ID='x'
export AWS_SECRET_ACCESS_KEY='y'
export AWS_DEFAULT_REGION='garage'
export AWS_ENDPOINT_URL='http://localhost:3900'
```

Get `x` from

```
garage -c garage.toml key info pgmoneta-app-key --show-secret | grep "Key ID" | awk '{print $3;}'
```

Get `y` from

```
garage -c garage.toml key info pgmoneta-app-key --show-secret | grep "Secret key" | awk '{print $3;}'
```

Apply the configuration

```
source ~/.awsrc
```

Test awscli

```
aws s3 ls
```
