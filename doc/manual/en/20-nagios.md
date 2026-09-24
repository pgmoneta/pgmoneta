# Nagios

pgmoneta can serve metrics in Nagios passive check format by bridging the existing Prometheus metrics endpoint.

## Configuration

Add the following to your `pgmoneta.conf`:

```ini
nagios = 6000
```

This tells pgmoneta to listen on port 6000 for Nagios connections. The `metrics` port must also be configured and enabled, since the Nagios bridge fetches data from it.

## How it works

When a connection is received on the Nagios port, pgmoneta:

1. Connects to the internal Prometheus metrics endpoint
2. Fetches all available metrics
3. Transforms them into Nagios passive check format
4. Returns the result to the caller

## Output format

The response follows the Nagios plugin output format:
PGMONETA OK - pgmoneta is running|metric1=value1 metric2=value2 ...
Where the pipe-separated section is the performance data containing all available metrics.

## Example

Start pgmoneta with both metrics and nagios ports configured:

```ini
metrics = 5001
nagios = 6000
```

Then query the Nagios endpoint:
nc localhost 6000
PGMONETA OK - pgmoneta is running|pgmoneta_backup_size=...
## See also

* [Prometheus](10-prometheus.md)
