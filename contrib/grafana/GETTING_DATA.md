# Getting Data for Grafana Dashboards

This guide explains how to enable pgmoneta metrics so your Grafana dashboards display data.

## Step 1: Enable Metrics in pgmoneta

### Option A: Edit Configuration File

1. **Locate your `pgmoneta.conf` file** (usually at `/etc/pgmoneta/pgmoneta.conf` or specified with `-c` flag)

2. **Add or update the `metrics` setting** in the `[pgmoneta]` section:
   ```ini
   [pgmoneta]
   host = localhost
   metrics = 5001          # Enable metrics on port 5001
   base_dir = /home/pgmoneta/backup  # Your backup directory (can vary)
   # ... other settings
   ```
   
   **Note**: The `base_dir` setting can be any directory you choose. Examples:
   - Docker setup: `/home/pgmoneta/backup`
   - Local development: `/tmp/pgmoneta`
   - Production: `/var/lib/pgmoneta/backup`
   
   The important part is just enabling `metrics = 5001` (or another port number).

3. **Restart pgmoneta** to apply the changes:
   ```bash
   # If running as a service
   sudo systemctl restart pgmoneta
   
   # Or if running manually, stop and restart
   pgmoneta -c /path/to/pgmoneta.conf
   ```

### Option B: Use CLI (Runtime Configuration)

You can enable metrics without restarting pgmoneta using the CLI:

```bash
pgmoneta-cli conf set metrics 5001
```

This will restart the metrics service automatically.

## Step 2: Verify Metrics are Available

Test that metrics are being exposed:

```bash
# Check if metrics endpoint is accessible
curl http://localhost:5001/metrics

# You should see output like:
# pgmoneta_state 1
# pgmoneta_version{version="0.20.0"} 1
# pgmoneta_logging_info 42
# pgmoneta_server_online{name="primary"} 1
# ... many more metrics
```

If you see metrics output, pgmoneta is correctly exposing metrics!

## Step 3: Configure Prometheus to Scrape Metrics

### If using the provided docker-compose.yml:

1. **Edit `prometheus.yml`** in the `contrib/grafana` directory:
   ```yaml
   global:
     scrape_interval: 15s

   scrape_configs:
     - job_name: "pgmoneta"
       metrics_path: "/metrics"
       static_configs:
         - targets: ["host.docker.internal:5001"]  # Adjust if pgmoneta is on different host/port
   ```

2. **If pgmoneta is running on the same machine as Docker:**
   - Use `host.docker.internal:5001` (works on Docker Desktop)
   - Or use your machine's IP address: `192.168.x.x:5001`
   - Or use `172.17.0.1:5001` (default Docker bridge gateway)

3. **If pgmoneta is running in Docker:**
   - Use the container name or service name: `pgmoneta:5001`
   - Make sure both containers are on the same Docker network

4. **Restart Prometheus** (if using docker-compose):
   ```bash
   docker compose restart prometheus
   ```

### Verify Prometheus is Scraping

1. **Open Prometheus UI**: http://localhost:9090

2. **Check Targets**:
   - Go to Status → Targets
   - Verify `pgmoneta` target shows as "UP" (green)

3. **Query a metric**:
   - Go to Graph tab
   - Enter: `pgmoneta_state`
   - Click "Execute"
   - You should see a value (1 if running)

## Step 4: Verify Grafana Can Access Data

1. **Open Grafana**: http://localhost:3000

2. **Check Data Source**:
   - Go to Configuration → Data Sources
   - Click on "Prometheus"
   - Click "Save & Test"
   - Should show "Data source is working"

3. **View Dashboards**:
   - Go to Dashboards
   - Open "pgmoneta Overview" (or any dashboard)
   - Panels should now show data instead of "No data"

## Troubleshooting

### No metrics available

**Problem**: `curl http://localhost:5001/metrics` returns connection refused or 404

**Solutions**:
- Verify pgmoneta is running: `ps aux | grep pgmoneta`
- Check if metrics port is correct: `netstat -tlnp | grep 5001` or `ss -tlnp | grep 5001`
- Verify `metrics = 5001` is in `pgmoneta.conf` (not commented out)
- Check pgmoneta logs for errors
- Try restarting pgmoneta

### Prometheus can't scrape

**Problem**: Prometheus target shows as "DOWN"

**Solutions**:
- Verify metrics endpoint is accessible from Prometheus container:
  ```bash
  docker compose exec prometheus wget -O- http://host.docker.internal:5001/metrics
  ```
- Check network connectivity between Prometheus and pgmoneta
- Verify the target URL in `prometheus.yml` is correct
- Check Prometheus logs: `docker compose logs prometheus`

### Grafana shows "No data"

**Problem**: Dashboards load but panels show "No data"

**Solutions**:
- Verify Prometheus has data: Query `pgmoneta_state` in Prometheus UI
- Check Grafana data source is configured correctly
- Verify time range in Grafana (top right) - try "Last 6 hours"
- Check if metrics exist: Query `{__name__=~"pgmoneta.*"}` in Prometheus
- Ensure pgmoneta has actually performed backups (some metrics only appear after backups)

### Metrics exist but dashboards are empty

**Problem**: Prometheus has data, but specific panels show nothing

**Solutions**:
- Some metrics only appear after backups are created
- Check if you have servers configured: Query `pgmoneta_server_online`
- Verify backup metrics exist: Query `pgmoneta_backup_valid`
- Check PostgreSQL version filter - try selecting "All" versions

## Example: Complete Setup from Scratch

### Standard Installation

```bash
# 1. Configure pgmoneta
sudo nano /etc/pgmoneta/pgmoneta.conf
# Add: metrics = 5001

# 2. Start/restart pgmoneta
sudo systemctl restart pgmoneta

# 3. Verify metrics
curl http://localhost:5001/metrics | head -20

# 4. Start Grafana stack
cd contrib/grafana
docker compose up -d

# 5. Wait a few seconds, then check Prometheus
curl http://localhost:9090/api/v1/targets | jq '.data.activeTargets[] | select(.labels.job=="pgmoneta")'

# 6. Open Grafana
# http://localhost:3000 (admin/admin)
```

### Docker Setup Example

If you're using the Docker setup (see `contrib/docker/pgmoneta.conf`), your configuration might look like:

```ini
[pgmoneta]
host = *
metrics = 5001
base_dir = /home/pgmoneta/backup
compression = zstd
retention = 7
# ... other settings
```

The key is that `metrics = 5001` is set - the `base_dir` path doesn't affect metrics functionality.

## Advanced: Metrics Caching

To improve performance, you can enable metrics caching in pgmoneta:

```ini
[pgmoneta]
metrics = 5001
metrics_cache_max_age = 30S    # Cache for 30 seconds
metrics_cache_max_size = 1M    # Max 1MB cache
```

This reduces load on pgmoneta when Prometheus scrapes frequently.

## Next Steps

Once metrics are flowing:
- Explore the dashboards to see backup status, performance, and disk usage
- Use the PostgreSQL version filter to focus on specific PostgreSQL versions
- Set up alerts in Grafana based on metrics (e.g., backup failures, disk space)
- Customize dashboards for your specific needs

