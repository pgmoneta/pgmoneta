# Running pgmoneta with Docker

You can run `pgmoneta` using Docker instead of compiling it manually.

## Prerequisites

- **Docker** or **Podman** must be installed on the server where PostgreSQL is running.
- Ensure PostgreSQL is configured to allow external connections.

---

## Step 1: Enable External PostgreSQL Access

Modify the local PostgreSQL server's `postgresql.conf` file to allow connections from outside:
```ini
listen_addresses = '*'
```

Update `pg_hba.conf` to allow remote connections:
```ini
host    all    all    0.0.0.0/0    scram-sha-256
```

Then, restart PostgreSQL for the changes to take effect:
```sh
sudo systemctl restart postgresql
```

---

## Step 2: Clone the Repository
```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
```

---

## Step 3: Build the Docker Image`

There are two Dockerfiles available:
1. **Alpine-based image**  
   **Using Docker**
   ```sh
   docker build -t pgmoneta:latest -f ./contrib/docker/Dockerfile.alpine .
   ```
   **Using Podman**
   ```sh
   podman build -t pgmoneta:latest -f ./contrib/docker/Dockerfile.alpine .
   ```

2. **Rocky Linux 9-based image**  
   **Using Docker**
   ```sh
   docker build -t pgmoneta:latest -f ./contrib/docker/Dockerfile.rocky9 .
   ```
   **Using Podman**
   ```sh
   podman build -t pgmoneta:latest -f ./contrib/docker/Dockerfile.rocky9 .
   ```

---

## Step 4: Run pgmoneta as a Docker Container

Once the image is built, run the container using:
- **Using Docker**
   ```sh
   docker run -d --name pgmoneta --network host pgmoneta:latest
   ```
- **Using Podman**
   ```sh
   podman run -d --name pgmoneta --network host pgmoneta:latest
   ```

---

## Step 5: Verify the Container

Check if the container is running: 

- **Using Docker**
   ```sh
   docker ps | grep pgmoneta
   ```
- **Using Podman**
   ```sh
   podman ps | grep pgmoneta
   ```

Check logs for any errors: 
- **Using Docker**
   ```sh
   docker logs pgmoneta
   ```
- **Using Podman**
   ```sh
   podman logs pgmoneta
   ```

You can also inspect the exposed server at:
```
http://localhost:5001/
```