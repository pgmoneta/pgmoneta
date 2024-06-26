\newpage

# Test

## Container Environment

### Docker

First, ensure your system is up to date.

```sh
dnf update
```

Install the necessary packages for Docker.

```sh
dnf -y install dnf-plugins-core
```

Add the Docker repository to your system.

``` sh
sudo dnf config-manager --add-repo https://download.docker.com/linux/fedora/docker-ce.repo
```

Install Docker Engine, Docker CLI, and Containerd.

```sh
sudo dnf install docker-ce docker-ce-cli containerd.io
```

Start the Docker service and enable it to start on boot.

```sh
sudo systemctl start docker
sudo systemctl enable docker
```

Verify that Docker is installed correctly.

```sh
docker --version
```

If you see the Docker version, then you have successfully installed Docker on Fedora.

### Podman

Install Podman and the Docker alias package.

```sh
dnf install podman podman-docker.noarch
```

Verify that Podman is installed correctly.

```sh
podman --version
```

If you see the Podman version, then you have successfully installed Podman on Fedora.

The `podman-docker.noarch` package simplifies the use of `Podman` for users accustomed to Docker.

## Test suite

You can simply use `CTest` to test all PostgreSQL versions from 13 to 16. It will automatically run `testsuite.sh` to test `pgmoneta` and `pgmoneta_ext` for each version. The script will automatically create the Docker container, run it, and then use the `check` framework to test their functions inside it. After that, it will automatically clean up everything for you.

Go to the directory `/pgmoneta/test`, and give permission to `testsuite.sh` using:

``` sh
chmod +x testsuite.sh
```

After you follow the [DEVELOPERS.md][developers] to install `pgmoneta`, go to the directory `/pgmoneta/build` and run the test.

``` sh
make test
```

`CTest` will output logs into `/pgmoneta/build/Testing/Temporary/LastTest.log`. If you want to check the specific process, you can review that log file.

`testsuite.sh` accepts three variables. The first one is `dir`, which specifies the `/test` directory location, with a default value of `./`. The second one is `dockerfile`, with a default value of `Dockerfile.rocky8`. The third one is the PostgreSQL `version`, with a default value of `13`.
