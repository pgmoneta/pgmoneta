# pgmoneta Prometheus Metrics Documentation Generator

This Python script generates comprehensive documentation for pgmoneta's Prometheus metrics. It fetches live metrics from your running pgmoneta instance and enriches them with detailed descriptions from an extra info file.

## Requirements

* **Python 3.6+** 
* **requests library:** Install with `pip install requests`
* **Running pgmoneta:** Your pgmoneta service must be running and exposing metrics

## Usage

```bash
./prometheus.py <port> <extra_info_file> [options]
```

**Arguments:**
* `<port>`: Port where pgmoneta is exposing metrics
* `<extra_info_file>`: File containing metric descriptions (see `extra.info`)

**Options:**
* `--manual`: Generate simple bullet-point format (like the user manual)
* `--md`: Generate detailed markdown with examples and descriptions  
* `--html`: Generate HTML documentation with styling
* `--toc`: Include table of contents
* Default (no options): Generates detailed markdown + HTML + TOC

## `extra_info_file` Format

This file provides additional context that isn't typically available in the raw Prometheus metrics output. The script expects this file to follow a specific format:

* Each metric block **must start** with the full metric name on its own line (e.g., `pgmoneta_backup_size`). This name must exactly match the metric name in the Prometheus output.
* Lines immediately following the metric name that start with `+ ` (plus sign followed by a space) are treated as the **Description**. Multiple `+ ` lines are concatenated (with spaces) into a single description paragraph in the output.
* Lines starting with `* ` (asterisk followed by a space) are treated as **Attribute Details**. These lines *must* follow the format `* Key: Value`.
  * The text between the `* ` and the first colon (`:`) becomes the "Attribute" column in the output table.
  * The text after the first colon becomes the "Value" column.
  * Leading/trailing whitespace around the Key and Value is trimmed.
  * If no `* Key: Value` lines are found for a metric, the "Attributes" section will be omitted from the output.
* Blocks for different metrics are implicitly separated by the next line starting with a metric name.

**Example `extra.info` content:**

```
pgmoneta_state
+ Provides the operational status of the pgmoneta service itself, indicating if it's running (1) or stopped/failed (0).
* 1: Running
* 0: Stopped or encountered a fatal error during startup/runtime

pgmoneta_backup_size
+ Reports the total size in bytes of a specific backup.
* name: The configured name/identifier for the PostgreSQL server.
* label: The backup identifier/timestamp.

pgmoneta_logging_info
+ Counts the total number of informational (INFO level) log messages produced by pgmoneta since its last startup.
```

## Output

The script generates files in the **current directory** (the directory where you run the script):

1. **`prometheus.md`**: A Markdown file containing the documentation.
2. **`prometheus.html`**: An HTML file with the same documentation, including a table of contents with basic styling (when using HTML option).

Each metric entry in the output files follows this structure:

* Metric Name (as heading)
* Combined Prometheus Help text and Type (e.g., `The state of pgmoneta Type is gauge.`)
* Description (from the `+` lines in `extra.info`)
* Attributes Table (generated *only* if `* Key: Value` lines were present in `extra.info`)
* Example (a sample raw metric line, preferably from the `primary` server if available, otherwise the first one found)

## Examples

Generate full documentation with examples and descriptions:
```bash
./prometheus.py 5001 extra.info
```

Generate just markdown in simple bullet-point format:
```bash  
./prometheus.py 5001 extra.info --manual
```

Generate detailed markdown with table of contents:
```bash
./prometheus.py 5001 extra.info --md --toc
```

Generate both HTML and markdown:
```bash
./prometheus.py 5001 extra.info --md --html
```

## Sample Usage with pgmoneta

1. **Start pgmoneta** with Prometheus metrics enabled:
   ```bash
   pgmoneta -c /path/to/pgmoneta.conf
   ```

2. **Verify metrics are available**:
   ```bash
   curl http://localhost:5001/metrics
   ```

3. **Generate documentation**:
   ```bash
   cd /path/to/pgmoneta/contrib/prometheus_scrape
   python3 prometheus.py 5001 extra.info
   ```

4. **View the generated documentation**:
   - Open `prometheus.html` in your browser
   - Or view `prometheus.md` in a markdown viewer

## Configuration Notes

- Ensure your `pgmoneta.conf` has the metrics configuration enabled:
  ```
  metrics = 5001
  ```
- The script will prioritize examples from servers named "primary" when multiple examples are available
- All pgmoneta core metrics are documented - no extension metrics are handled by this script

## Troubleshooting

**Connection refused errors:**
- Verify pgmoneta is running
- Check that the metrics port (5001) is correct
- Ensure firewall allows access to the metrics port

**Missing metrics in output:**
- Verify the `extra.info` file format matches the expected structure
- Check that metric names in `extra.info` exactly match those in the Prometheus output
- Use the validation script in the same directory to check for inconsistencies

**Empty or incomplete documentation:**
- Ensure pgmoneta has been running long enough to generate metrics
- Some metrics only appear after specific operations (backups, restores, etc.)