# Test environments

## S3

### versitygw

[versitygw](https://github.com/versity/versitygw) is an S3 gateway that serves a local directory
as S3. It enforces S3 the way AWS does, including Object Lock and the checksum headers AWS requires,
and it is what the storage-engine integration tests use.

**Start versitygw**

```
mkdir -p /tmp/versitygw/data/pgmoneta-bucket /tmp/versitygw/versions
podman run -d --name versitygw --network host \
  -e ROOT_ACCESS_KEY=pgmoneta -e ROOT_SECRET_KEY=pgmoneta-secret-key \
  -v /tmp/versitygw:/tmp/versitygw:Z \
  versity/versitygw:v1.11.0 \
  --port :7070 posix --versioning-dir /tmp/versitygw/versions /tmp/versitygw/data
```

or use `docker` instead of `podman`. Every directory under the data directory is a bucket, so
`pgmoneta-bucket` exists as soon as the gateway is up, and the access and secret keys are the
`ROOT_ACCESS_KEY` and `ROOT_SECRET_KEY` values.

**Install awscli**

```
python -m pip install --user awscli
```

**Configure awscli**

Create `~/.awsrc`

```
export AWS_ACCESS_KEY_ID='pgmoneta'
export AWS_SECRET_ACCESS_KEY='pgmoneta-secret-key'
export AWS_DEFAULT_REGION='us-east-1'
export AWS_ENDPOINT_URL='http://localhost:7070'
```

Apply the configuration

```
source ~/.awsrc
```

Test awscli

```
aws s3 ls
```
