\newpage

# Prometheus metrics

[**pgmoneta**][pgmoneta] support [Prometheus][prometheus] metrics.

We enabled the [Prometheus][prometheus] metrics in the original configuration by setting

```
metrics = 5001
```

in `pgmoneta.conf`.

## Access Prometheus metrics

You can now access the metrics via

```
http://localhost:5001/metrics
```
