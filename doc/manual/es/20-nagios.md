# Nagios

pgmoneta puede servir métricas en el formato de chequeo pasivo de Nagios actuando como puente con el endpoint de métricas existente de Prometheus.

## Configuración

Agrega lo siguiente a tu archivo `pgmoneta.conf`:

```ini
nagios = 6000
```

Esto le indica a pgmoneta que escuche en el puerto 6000 para conexiones de Nagios. El puerto de `metrics` también debe estar configurado y habilitado, ya que el puente de Nagios obtiene los datos a partir de él.

## Cómo funciona

Cuando se recibe una conexión en el puerto de Nagios, pgmoneta:

1. Se conecta al endpoint interno de métricas de Prometheus
2. Obtiene todas las métricas disponibles
3. Las transforma al formato de chequeo pasivo de Nagios
4. Devuelve el resultado al cliente

## Formato de salida

La respuesta sigue el formato de salida de los plugins de Nagios:
PGMONETA OK - pgmoneta is running|metric1=value1 metric2=value2 ...
Donde la sección separada por la barra vertical (`|`) contiene los datos de rendimiento (*performance data*) con todas las métricas disponibles.

## Ejemplo

Inicia pgmoneta con los puertos de métricas y nagios configurados:

```ini
metrics = 5001
nagios = 6000
```

Luego consulta el endpoint de Nagios:
nc localhost 6000
PGMONETA OK - pgmoneta is running|pgmoneta_backup_size=...
## Véase también

* [Prometheus](10-prometheus.md)
