# pgmoneta Grafana Dashboards

This directory contains Grafana dashboards for monitoring `pgmoneta` using Prometheus.

## Dashboards

- **pgmoneta Overview** (`pgmoneta-overview.json`):
  - **System Status**: Global pgmoneta service state (Running/Stopped).
  - **Global Statistics**: Total configured servers, servers online, and total backups across the fleet.
  - **Disk Usage**:
    - **Total Backup Size**: Aggregate disk space used by all backups.
    - **WAL Storage**: Space consumed by Write-Ahead Logs.

- **pgmoneta Server Details** (`pgmoneta-server-details.json`):
  - **Server Health**: Connectivity status (Online/Offline) and configuration validity.
  - **Backup History**:
    - **Timeline**: Visual timeline of recent backup events.
    - **All Backups**: Aggregated table showing status, storage size, and timestamps for every backup.
  - **Storage**:
    - **Backup Total Size**: Disk space used by backups for this specific server.
    - **WAL Streaming**: Current WAL streaming status and storage usage.
    - **Workspace Storage**: Space used in the working directory.

- **pgmoneta Performance** (`pgmoneta-performance.json`):
  - **Duration Analysis**:
    - **Latest/Average/Peak Duration**: Key performance indicators for backup windows.
    - **Duration History**: Trend lines for Total Time, Base Backup, and Manifest generation.
  - **Processing Overhead**:
    - **Compression Overhead**: Time spent compressing data (GZIP, ZSTD, LZ4, BZIP2).
    - **Encryption Overhead**: Time spent encrypting data.
    - **Phase Distribution**: Breakdown of time spent in each backup phase.


## Grafana Version Compatibility

These dashboards are compatible with **Grafana 12+** (schemaVersion 39). They include:
- PostgreSQL version filtering: Filter metrics by PostgreSQL major version using the `PostgreSQL Major Version` variable
- Updated panel configurations for Grafana 12
- Support for multiple PostgreSQL versions in the same environment

## Setup

1. **Configure pgmoneta**: Ensure `metrics` is enabled in `pgmoneta.conf` and Prometheus is scraping it.
   - See [GETTING_DATA.md](GETTING_DATA.md) for detailed instructions on enabling metrics
2. **Import into Grafana**:
   - Go to Dashboards -> New -> Import.
   - Upload the `.json` files.
   - Select your Prometheus data source when prompted.
   - Or use the docker-compose setup for automatic provisioning (see below)

## Local Testing with Docker

A `docker-compose.yml` is provided to quickly spin up Grafana 12 and Prometheus for testing.

1. **Run the specific stack**:
   ```bash
   docker-compose up -d
   ```

2. **Access Grafana**:
   - Open http://localhost:3000
   - Login with `admin` / `admin`
   - The dashboards are provisioned automatically (if mapped) or can be imported manually.

3. **Prometheus**:
   - Accessible at http://localhost:9090
   - Configured in `prometheus.yml`. You may need to adjust the `targets` to point to your running `pgmoneta` instance (e.g., `host.docker.internal:5001`).

4. **PostgreSQL Version Filtering**:
   - Use the "PostgreSQL Major Version" dropdown at the top of dashboards to filter metrics by PostgreSQL version
   - Select "All" to view metrics from all PostgreSQL versions
   - Select specific versions (e.g., "12", "13", "14", "15", "16") to filter to those versions only
