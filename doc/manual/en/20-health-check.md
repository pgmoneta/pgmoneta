\newpage

# Health Check

[**pgmoneta**][pgmoneta] can periodically check the health of the [PostgreSQL][postgresql] instances it manages. A dedicated worker process probes each configured server on an interval and records whether it is reachable. The result is exposed through the Prometheus metrics endpoint so that external tooling can observe the live status of each server.

## Configuration

The health check behavior is controlled by the following parameters in the `[pgmoneta]` section of `pgmoneta.conf`:

* `health_check`: Enables or disables the periodic health check worker (`on`/`off`). Default is `off`.
* `health_check_period`: The interval between health check scans. Default is 30 seconds.
* `health_check_timeout`: The timeout for each health check probe. Default is 5 seconds.
* `health_check_user`: The user used to connect to the database for health checks. **This parameter is mandatory when `health_check` is enabled.**

Each probe authenticates to the server, runs `SELECT 1`, and disconnects. A server is marked `UP` on the first successful probe. After `health_check_timeout` is exceeded or a probe fails, the failure counter is incremented; once it reaches the retry threshold (3 consecutive failures) the server is marked `DOWN`. The health state is independent of the existing `online` flag that pgmoneta maintains for its own operations.

## Security and User Setup

Health checks are performed by connecting to a database within the target cluster, which requires user credentials. Using an unprivileged user to connect is the recommended and more secure way to configure the health check feature.

### Option 1: Existing backup user

Using a user that is already configured for pgmoneta (for example the replication user) is the simplest way to get started, since the credentials are already registered in `pgmoneta_users.conf`.

#### Pros
* No additional setup required

#### Cons
* The replication user is more privileged than a liveness check requires

### Option 2: Dedicated unprivileged user

Creating a dedicated, restricted user is the recommended and more secure approach.

#### Pros
* **Improved Security**: Limits the impact if credentials are leaked
* **Better Auditing**: Health check connections are clearly identifiable in [PostgreSQL][postgresql] logs

#### Cons
* Requires manual setup in [PostgreSQL][postgresql] and HBA configuration

### Setup Steps

To set up a dedicated health check user:

1. **Create the user in PostgreSQL**:
   Connected as a superuser, run:
   ```sql
   CREATE ROLE pgmoneta_health WITH LOGIN PASSWORD 'your_secure_password' CONNECTION LIMIT 1;
   ```

2. **Register the user with pgmoneta**:
   Use `pgmoneta-admin` to add the user to your `pgmoneta_users.conf`:
   ```bash
   pgmoneta-admin -f pgmoneta_users.conf -U pgmoneta_health -P your_secure_password user add
   ```

3. **Configure pgmoneta.conf**:
   Ensure the health check is turned on and configured properly -- both `health_check` and `health_check_user` must be set; health checks will not run without both:
   ```ini
   [pgmoneta]
   health_check = on
   health_check_user = pgmoneta_health
   ```

4. **Update HBA settings on the backend**:
   Ensure the backend [PostgreSQL][postgresql] `pg_hba.conf` allows the health check user to connect from the [**pgmoneta**][pgmoneta] host:
   ```
   # TYPE  DATABASE          USER             ADDRESS         METHOD
   host    postgres          pgmoneta_health  127.0.0.1/32    scram-sha-256
   ```

## Monitoring

The health status of each server is exposed via the Prometheus metrics endpoint (`pgmoneta_server_health`).

* `1`: Server is UP
* `0`: Server is DOWN
* `2`: State is UNKNOWN (initial state or pending first check)

The metric includes an `auth` label identifying the authentication method used during the last probe (e.g., `trust`, `scram-sha-256`, `error`).
