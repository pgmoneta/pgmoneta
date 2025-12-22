# Testing Grafana 12 Dashboards

This guide explains how to test the Grafana 12 dashboards for pgmoneta.

## Prerequisites

- Docker and Docker Compose installed
- A running pgmoneta instance with metrics enabled (port 5001 by default)
- Web browser

> **Note**: If you don't have metrics enabled yet, see [GETTING_DATA.md](GETTING_DATA.md) for instructions on how to enable pgmoneta metrics.

## Quick Start

1. **Start the Grafana and Prometheus stack**:
   ```bash
   cd contrib/grafana
   docker compose up -d
   ```

2. **Wait for services to start** (about 10-20 seconds):
   ```bash
   docker compose ps
   ```

3. **Access Grafana**:
   - Open http://localhost:3000 in your browser
   - Login with username: `admin`, password: `admin`
   - You should see the dashboards automatically provisioned

4. **Access Prometheus** (optional):
   - Open http://localhost:9090
   - Verify that Prometheus is scraping pgmoneta metrics

## Testing Checklist

### Dashboard Loading
- [ ] All three dashboards appear in the Grafana dashboard list:
  - pgmoneta Overview
  - pgmoneta Server Details
  - pgmoneta Backup Performance
- [ ] Dashboards load without errors
- [ ] No JSON parsing errors in browser console

### PostgreSQL Version Filtering
- [ ] "PostgreSQL Major Version" dropdown appears at the top of dashboards
- [ ] Dropdown shows available PostgreSQL versions (e.g., 12, 13, 14, 15, 16)
- [ ] Selecting "All" shows metrics from all versions
- [ ] Selecting a specific version filters metrics appropriately
- [ ] Multiple versions can be selected simultaneously

### Panel Functionality
- [ ] All panels render correctly
- [ ] Time series graphs display data (if metrics are available)
- [ ] Stat panels show correct values
- [ ] Tables display data correctly
- [ ] Pie charts render properly
- [ ] **Performance Dashboard**:
  - [ ] "Processing Overhead" shows distinct "Compression Overhead" and "Encryption Overhead" gauges
  - [ ] "Encryption Time" shows a single line graph (no duplicate series)
  - [ ] History charts (Duration, Compression) show single, clean trend lines
- [ ] **Server Details Dashboard**:
  - [ ] "All Backups" table shows exactly one row per backup label (no duplicates)

### Data Source
- [ ] Prometheus data source is configured correctly
- [ ] Metrics queries execute without errors
- [ ] Data appears in panels (if pgmoneta is running and has data)

## Troubleshooting

### Dashboards not appearing
- Check that dashboard files are in the correct location
- Verify provisioning configuration in `provisioning/dashboards/dashboards.yaml`
- Check Grafana logs: `docker compose logs grafana`

### No data in panels
- **First, ensure metrics are enabled**: See [GETTING_DATA.md](GETTING_DATA.md) for complete setup instructions
- Verify pgmoneta is running: `curl http://localhost:5001/metrics`
- Check Prometheus targets: http://localhost:9090/targets
- Verify `prometheus.yml` has correct target configuration
- Ensure pgmoneta has metrics enabled in `pgmoneta.conf`

### Version filter not working
- Verify `pgmoneta_backup_version` metric is available
- Check that backups exist with version labels
- Verify the metric query in the variable definition

## Stopping the Stack

```bash
docker compose down
```

To remove volumes as well:
```bash
docker compose down -v
```

