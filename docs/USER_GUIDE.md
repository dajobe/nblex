# nblex User Guide

**Network & Buffer Log EXplorer**
**Version:** 0.5.0
**Last Updated:** 2025-11-13

______________________________________________________________________

## Table of Contents

1. [Introduction](#introduction)
2. [Installation](#installation)
3. [Quick Start](#quick-start)
4. [Basic Concepts](#basic-concepts)
5. [Command-Line Usage](#command-line-usage)
6. [Configuration](#configuration)
7. [Log Formats](#log-formats)
8. [Network Monitoring](#network-monitoring)
9. [Correlation](#correlation)
10. [nQL Queries](#nql-queries)
11. [Output and Export](#output-and-export)
12. [Performance Tuning](#performance-tuning)
13. [Troubleshooting](#troubleshooting)
14. [Best Practices](#best-practices)

______________________________________________________________________

## Introduction

nblex (Network & Buffer Log EXplorer) is a lightweight correlation tool that combines application log analysis with network traffic monitoring. It helps you understand system behavior by correlating events across both layers.

### Key Features

- **Real-time streaming analysis** - Process logs and network traffic as they occur
- **Automatic correlation** - Link events across application and network layers
- **Flexible query language** - nQL for filtering, aggregation, and correlation
- **Multiple log formats** - JSON, logfmt, syslog, nginx, and custom regex
- **Network protocol support** - HTTP, DNS, TCP/UDP dissection
- **Export capabilities** - JSON, Prometheus metrics, HTTP webhooks

### Use Cases

- Debugging production issues (correlate errors with network problems)
- Performance analysis (find slow API endpoints)
- Security investigation (detect suspicious patterns)
- Compliance auditing (track database access)

______________________________________________________________________

## Installation

### Prerequisites

- Linux, macOS, or BSD
- GCC or Clang compiler
- CMake 3.10+
- Dependencies:
  - libuv (async I/O)
  - libjansson (JSON)
  - libpcap (packet capture)
  - PCRE2 (regex)

### Build from Source

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install libuv1-dev libjansson-dev libpcap-dev libpcre2-dev

# Install dependencies (macOS)
brew install libuv jansson libpcap pcre2

# Build nblex
git clone https://github.com/yourorg/nblex.git
cd nblex
mkdir build && cd build
cmake ..
make
sudo make install
```

### Verify Installation

```bash
nblex --version
nblex --help
```

______________________________________________________________________

## Quick Start

### Example 1: Monitor a Log File

```bash
# Monitor JSON logs, show only errors
nblex monitor \
  --logs /var/log/app.log \
  --format json \
  --filter 'log.level == ERROR'
```

### Example 2: Monitor Network Traffic

```bash
# Monitor HTTPS traffic on eth0
nblex monitor \
  --network eth0 \
  --filter 'tcp port 443' \
  --output json
```

### Example 3: Correlate Logs and Network

```bash
# Find errors that occur during network problems
nblex monitor \
  --logs /var/log/app.log \
  --network eth0 \
  --query 'correlate log.level == ERROR with network.retransmits > 0 within 100ms' \
  --output json
```

______________________________________________________________________

## Basic Concepts

### World

The **world** is the main context that holds all inputs, outputs, and the correlation engine. Think of it as the nblex runtime environment.

### Inputs

**Inputs** are sources of events:
- **File inputs** - Log files (with rotation support)
- **Network inputs** - Packet capture from interfaces or pcap files
- **Syslog inputs** - UDP/TCP syslog receivers

### Events

**Events** are the fundamental data unit:
- **Log events** - Parsed from log files
- **Network events** - Dissected from packets
- **Correlation events** - Created when log and network events correlate

### Correlation

**Correlation** links related events across different sources based on:
- **Time windows** - Events within ±100ms
- **Request IDs** - Matching trace/request IDs
- **Connections** - Network flows to processes

### Filters

**Filters** select which events to process based on field values and boolean logic.

### nQL

**nQL** (nblex Query Language) is a lightweight SQL-like language for:
- Filtering events
- Aggregating metrics
- Correlating across sources
- Windowing time-series data

______________________________________________________________________

## Command-Line Usage

### Basic Syntax

```bash
nblex [command] [options]
```

### Commands

#### `nblex monitor`

Monitor logs and/or network traffic in real-time.

**Options:**
- `--logs PATH` - Log file to monitor (supports wildcards)
- `--network INTERFACE` - Network interface to capture
- `--format FORMAT` - Log format (json, logfmt, syslog, nginx)
- `--filter EXPR` - Filter expression
- `--query QUERY` - nQL query
- `--output TYPE` - Output type (json, file, http, metrics)
- `--config FILE` - Configuration file

**Examples:**
```bash
# Monitor single log file
nblex monitor --logs /var/log/app.log --format json

# Monitor multiple log files
nblex monitor --logs '/var/log/app/*.log' --format json

# Monitor network interface
nblex monitor --network eth0 --filter 'tcp port 80 or tcp port 443'

# Monitor both with correlation
nblex monitor \
  --logs /var/log/app.log \
  --network eth0 \
  --query 'correlate log.level == ERROR with network.dst_port == 3306 within 100ms'
```

#### `nblex analyze`

Analyze offline data (log files and pcap files).

**Examples:**
```bash
# Analyze historical logs and pcap
nblex analyze \
  --logs archive.log.gz \
  --pcap traffic.pcap \
  --query @query.nql \
  --output json > results.jsonl
```

#### `nblex query`

Interactive or one-shot nQL query execution.

**Examples:**
```bash
# Interactive mode
nblex query

# One-shot query
nblex query "aggregate count() where log.level == ERROR"

# Load query from file
nblex query --file queries/errors.nql
```

#### `nblex config`

Configuration management.

**Examples:**
```bash
# Validate configuration
nblex config validate /etc/nblex/config.yaml

# Show effective configuration
nblex config show
```

______________________________________________________________________

## Configuration

### Configuration File

nblex can be configured via YAML file.

**Location:** `/etc/nblex/config.yaml` or `~/.nblex/config.yaml`

### Basic Configuration

```yaml
version: "1"

inputs:
  logs:
    - name: app_logs
      type: file
      path: /var/log/app/*.log
      format: json

  network:
    - name: main_interface
      type: pcap
      interface: eth0
      filter: "tcp port 443"

correlation:
  enabled: true
  strategies:
    - type: time_based
      window: 100ms

outputs:
  - name: stdout
    type: stdout
    format: json

  - name: metrics
    type: prometheus
    listen: :9090
```

### Full Configuration Example

```yaml
version: "1"

inputs:
  logs:
    - name: nginx_access
      type: file
      path: /var/log/nginx/access.log
      format: nginx_combined

    - name: app_logs
      type: file
      path: /var/log/app/*.log
      format: json
      multiline:
        pattern: '^\s+'
        match: after

    - name: syslog
      type: syslog
      listen: udp://0.0.0.0:514

  network:
    - name: main_traffic
      type: pcap
      interface: eth0
      snaplen: 65535
      filter: "tcp port 80 or tcp port 443"

processors:
  - name: parse_timestamps
    type: timestamp
    field: timestamp
    formats:
      - "2006-01-02T15:04:05Z07:00"
      - "02/Jan/2006:15:04:05 -0700"

  - name: filter_noise
    type: filter
    expression: |
      log.level >= INFO
      OR network.port IN (80, 443, 3306)

correlation:
  enabled: true
  strategies:
    - type: time_based
      window: 100ms

    - type: id_based
      log_field: request_id
      network_field: http.header.x-request-id

queries:
  - name: error_monitoring
    query: |
      aggregate count() where log.level == ERROR
      by log.service window tumbling(1m)
    outputs:
      - type: prometheus
        port: 9090

  - name: slow_requests
    query: |
      * where network.http.method IN (GET, POST)
      AND network.latency_ms > 1000
    outputs:
      - type: file
        path: /var/log/nblex/slow_requests.jsonl

outputs:
  - name: default_stdout
    type: stdout
    format: json

  - name: metrics
    type: prometheus
    listen: :9090

  - name: archive
    type: file
    path: /var/log/nblex/correlations.jsonl
    rotation:
      max_size: 100MB
      max_age: 7d
      max_count: 10

  - name: webhook
    type: http
    url: https://alerting.example.com/webhook
    method: POST
    timeout: 30s

performance:
  worker_threads: 4
  buffer_size: 64MB
  max_memory: 1GB
  flow_table_size: 100000
```

______________________________________________________________________

## Log Formats

### Supported Formats

#### JSON

```json
{"level":"ERROR","message":"Connection failed","service":"api"}
```

**Configuration:**
```bash
nblex monitor --logs /var/log/app.log --format json
```

#### Logfmt

```
level=ERROR message="Connection failed" service=api
```

**Configuration:**
```bash
nblex monitor --logs /var/log/app.log --format logfmt
```

#### Syslog (RFC 5424 / RFC 3164)

```
<34>Oct 11 22:14:15 server app: Connection failed
```

**Configuration:**
```bash
nblex monitor --logs /var/log/syslog --format syslog
```

#### Nginx Combined

```
192.168.1.1 - - [11/Oct/2025:22:14:15 +0000] "GET /api/users HTTP/1.1" 200 1234
```

**Configuration:**
```bash
nblex monitor --logs /var/log/nginx/access.log --format nginx
```

#### Custom Regex

Define custom patterns in configuration:

```yaml
inputs:
  logs:
    - name: custom_logs
      type: file
      path: /var/log/custom.log
      format: regex
      regex: '^(?P<timestamp>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) \[(?P<level>\w+)\] (?P<message>.+)$'
```

### Automatic Format Detection

nblex can auto-detect common formats:

```bash
nblex monitor --logs /var/log/app.log --format auto
```

______________________________________________________________________

## Network Monitoring

### Live Capture

```bash
# Capture from specific interface
nblex monitor --network eth0

# Capture from all interfaces
nblex monitor --network any

# Apply BPF filter
nblex monitor --network eth0 --filter 'tcp port 443'
```

### Offline Analysis

```bash
# Analyze pcap file
nblex analyze --pcap traffic.pcap --output json
```

### Supported Protocols

- **TCP/UDP** - Transport layer analysis
- **HTTP/1.1** - Request/response parsing
- **DNS** - Query/response parsing
- **TLS** - Handshake metadata (SNI, versions)

### Common Filters

```bash
# HTTP traffic
--filter 'tcp port 80 or tcp port 443'

# Database connections
--filter 'tcp port 3306 or tcp port 5432'

# DNS queries
--filter 'udp port 53'

# Specific host
--filter 'host 192.168.1.100'
```

______________________________________________________________________

## Correlation

### Time-Based Correlation

Match events within a time window.

**Example:**
```bash
nblex monitor \
  --logs /var/log/app.log \
  --network eth0 \
  --query 'correlate log.level == ERROR with network.retransmits > 0 within 100ms'
```

**Configuration:**
```yaml
correlation:
  enabled: true
  strategies:
    - type: time_based
      window: 100ms
```

### ID-Based Correlation

Match events by request/trace ID.

**Example:**
```nql
correlate log.request_id with network.http.header.x-request-id
```

**Configuration:**
```yaml
correlation:
  strategies:
    - type: id_based
      log_field: request_id
      network_field: http.header.x-request-id
```

### Understanding Correlation Output

Correlation events contain both the log and network events:

```json
{
  "type": "correlation",
  "correlation_id": "corr_abc123",
  "timestamp": "2025-11-13T10:30:45.123Z",
  "events": [
    {
      "type": "log",
      "level": "ERROR",
      "message": "Database query timeout",
      "source": "app.log"
    },
    {
      "type": "network",
      "protocol": "tcp",
      "dst_port": 3306,
      "tcp_flags": "RST",
      "latency_ms": 5002
    }
  ],
  "correlation_type": "time_based",
  "confidence": 0.95
}
```

______________________________________________________________________

## nQL Queries

### Filter Queries

Simple field-based filtering:

```nql
log.level == ERROR
log.level >= WARN
log.message =~ /timeout/i
network.dst_port == 443
```

### Correlation Queries

```nql
correlate log.level == ERROR with network.dst_port == 3306 within 100ms
correlate log.status >= 500 with network.tcp.retransmits > 0 within 1s
```

### Aggregation Queries

```nql
aggregate count() where log.level == ERROR
aggregate count() by log.service where log.level == ERROR window 1m
aggregate count(), avg(network.latency_ms) by log.endpoint window tumbling(1m)
```

### Pipeline Queries

Chain operations with `|`:

```nql
log.level == ERROR | aggregate count() by log.service
correlate log.level == ERROR with network.retransmits > 0 within 100ms | aggregate count()
```

### Window Types

- **Tumbling**: Non-overlapping fixed windows
  ```nql
  window tumbling(1m)
  ```

- **Sliding**: Overlapping windows
  ```nql
  window sliding(1m, 30s)  /* 1min window, 30s slide */
  ```

- **Session**: Activity-based windows
  ```nql
  window session(5m)  /* 5min idle timeout */
  ```

______________________________________________________________________

## Output and Export

### Standard Output

```bash
nblex monitor --logs /var/log/app.log --output json
```

### File Output

```bash
nblex monitor \
  --logs /var/log/app.log \
  --output file \
  --output-file /var/log/nblex/output.jsonl
```

**With rotation:**
```yaml
outputs:
  - name: archive
    type: file
    path: /var/log/nblex/output.jsonl
    rotation:
      max_size: 100MB
      max_age: 7d
      max_count: 10
```

### HTTP Webhook

```yaml
outputs:
  - name: webhook
    type: http
    url: https://example.com/webhook
    method: POST
    timeout: 30s
```

### Prometheus Metrics

```yaml
outputs:
  - name: metrics
    type: prometheus
    listen: :9090
```

Access metrics at `http://localhost:9090/metrics`

______________________________________________________________________

## Performance Tuning

### Resource Limits

```yaml
performance:
  worker_threads: 4          # Number of worker threads
  buffer_size: 64MB          # Event buffer size
  max_memory: 1GB            # Maximum memory usage
  flow_table_size: 100000    # Network flow table size
```

### Optimizations

**1. Use BPF filters for network**
```bash
--filter 'tcp port 443'  # Reduces packets processed
```

**2. Filter early in pipeline**
```nql
log.level >= WARN | aggregate count()  /* Better */
aggregate count() where log.level >= WARN  /* Also works */
```

**3. Limit window sizes**
```nql
window tumbling(1m)  /* Better than 1h */
```

**4. Use file rotation**
```yaml
rotation:
  max_size: 100MB  # Prevents disk full
```

### Monitoring Performance

```bash
# Check memory usage
ps aux | grep nblex

# Check CPU usage
top -p $(pgrep nblex)

# Access internal metrics
curl http://localhost:9090/metrics
```

______________________________________________________________________

## Troubleshooting

### Common Issues

#### "Permission denied" on network capture

**Problem:** Need root/CAP_NET_RAW to capture packets

**Solution:**
```bash
# Run as root
sudo nblex monitor --network eth0

# Or grant capabilities
sudo setcap cap_net_raw+ep /usr/local/bin/nblex
```

#### "No events captured"

**Problem:** Filter too restrictive or wrong log format

**Solution:**
```bash
# Try without filter first
nblex monitor --logs /var/log/app.log --format auto

# Enable debug logging
nblex monitor --logs /var/log/app.log --debug
```

#### "High memory usage"

**Problem:** Too many events in buffer or large flow table

**Solution:**
```yaml
performance:
  buffer_size: 32MB      # Reduce buffer
  max_memory: 512MB      # Set hard limit
  flow_table_size: 50000 # Reduce flow table
```

#### "Correlation not working"

**Problem:** Events outside time window or wrong field names

**Solution:**
```bash
# Increase correlation window
--query 'correlate log.level == ERROR with network.port == 3306 within 500ms'

# Check field names
--output json  # Inspect actual field names
```

### Debug Mode

```bash
nblex monitor --logs /var/log/app.log --debug
```

Enables verbose logging of parsing, filtering, and correlation.

### Log Files

Check nblex logs for errors:
```bash
journalctl -u nblex -f
```

______________________________________________________________________

## Best Practices

### 1. Start Simple

Begin with basic monitoring before adding correlation:

```bash
# Step 1: Monitor logs only
nblex monitor --logs /var/log/app.log --format json

# Step 2: Add filtering
nblex monitor --logs /var/log/app.log --filter 'log.level >= WARN'

# Step 3: Add network
nblex monitor --logs /var/log/app.log --network eth0

# Step 4: Add correlation
nblex monitor --logs /var/log/app.log --network eth0 \
  --query 'correlate log.level == ERROR with network.retransmits > 0 within 100ms'
```

### 2. Use Configuration Files

For production, use YAML configuration instead of CLI arguments:

```bash
nblex monitor --config /etc/nblex/config.yaml
```

### 3. Filter Early

Apply filters as early as possible to reduce processing:

```yaml
inputs:
  logs:
    - name: app_logs
      path: /var/log/app.log
      format: json
      filter: 'log.level >= WARN'  # Filter at input level
```

### 4. Set Resource Limits

Always set memory and buffer limits for production:

```yaml
performance:
  max_memory: 1GB
  buffer_size: 64MB
```

### 5. Use Rotation

Enable log rotation to prevent disk full:

```yaml
outputs:
  - type: file
    path: /var/log/nblex/output.jsonl
    rotation:
      max_size: 100MB
      max_age: 7d
      max_count: 10
```

### 6. Monitor nblex Itself

Export metrics to Prometheus and alert on anomalies:

```yaml
outputs:
  - type: prometheus
    listen: :9090
```

### 7. Test Queries Offline

Test complex queries on historical data before production:

```bash
nblex analyze \
  --logs archive.log \
  --pcap archive.pcap \
  --query @my-query.nql \
  --output json
```

______________________________________________________________________

## Next Steps

- Read the [API Documentation](API.md) for library usage
- See [SPEC.md](../SPEC.md) for architecture details
- Check [examples/](../examples/) for code samples
- Visit [GitHub](https://github.com/yourorg/nblex) for updates

______________________________________________________________________

## Support

- **Issues:** https://github.com/yourorg/nblex/issues
- **Discussions:** https://github.com/yourorg/nblex/discussions
- **Discord:** https://discord.gg/nblex

______________________________________________________________________

**Version:** 0.5.0
**Last Updated:** 2025-11-13
