# nblex Specification

## Network & Buffer Log EXplorer

**Version:** 2.0
**Status:** Draft
**Last Updated:** 2025-11-10

______________________________________________________________________

## Executive Summary

**nblex** (Network & Buffer Log EXplorer) is a lightweight correlation tool that uniquely combines application log analysis with network traffic monitoring. By correlating events across both layers, nblex provides unprecedented visibility into system behavior, enabling faster debugging and performance optimization. Designed for ad-hoc debugging sessions, nblex complements existing observability platforms rather than replacing them.

### Key Differentiator

While traditional tools analyze logs OR network traffic in isolation, nblex correlates both simultaneously, answering questions like:

- "Show me all ERROR logs that occurred during network timeouts"
- "Which API endpoints are logging errors but returning 200 OK?"
- "Correlate database query logs with actual network traffic to MySQL"

______________________________________________________________________

## Vision & Goals

### Vision

Enable developers and SREs to understand system behavior by unifying application-level insights (logs) with network-level reality (packets) during debugging sessions and investigations.

### Primary Goals

1. **Real-time streaming analysis** - Process logs and network traffic as they occur
1. **Correlation engine** - Link events across application and network layers
1. **Non-blocking architecture** - Handle high-throughput environments efficiently
1. **Developer-friendly** - Simple to deploy, query, and integrate
1. **Reliable for production debugging** - Minimal overhead, stable for debugging sessions

### Non-Goals (v1.0)

- Full-featured log storage (use existing solutions: Elasticsearch, Loki, etc.)
- Deep packet inspection beyond L7 protocols (HTTP, DNS, TLS)
- Log aggregation from hundreds of sources (focus on quality over quantity)
- Replace APM tools (complement, don't compete)

______________________________________________________________________

## Architecture

### High-Level Architecture

```text
┌─────────────────────────────────────────────────────────────┐
│                        nblex Core                           │
│                                                             │
│  ┌─────────────┐      ┌──────────────┐      ┌────────────┐  │
│  │   Inputs    │─────▶│   Stream     │─────▶│  Outputs   │  │
│  │             │      │  Processor   │      │            │  │
│  │ • Files     │      │              │      │ • stdout   │  │
│  │ • Sockets   │      │ • Parse      │      │ • Files    │  │
│  │ • pcap      │      │ • Filter     │      │ • HTTP     │  │
│  │ • Journals  │      │ • Correlate  │      │ • Metrics  │  │
│  └─────────────┘      │ • Aggregate  │      │ • Alerts   │  │
│                       └──────────────┘      └────────────┘  │
│                              │                              │
│                       ┌──────▼───────┐                      │
│                       │  Correlation │                      │
│                       │    Engine    │                      │
│                       │              │                      │
│                       │ • Time-based │                      │
│                       │ • ID-based   │                      │
│                       │ • Pattern    │                      │
│                       └──────────────┘                      │
└─────────────────────────────────────────────────────────────┘
```

### Core Components

#### 1. Input Layer

Handles multiple data sources with unified streaming interface:

**Log Sources:**

- **File tailing** - `/var/log/app/*.log` with rotation detection
- **Syslog** - TCP/UDP syslog receiver (RFC 5424, RFC 3164)
- **Journald** - systemd journal integration
- **Docker/Container logs** - Docker API, containerd
- **Unix sockets** - `/dev/log` style inputs
- **HTTP endpoints** - Receive logs via POST

**Network Sources:**

- **Packet capture** - libpcap/AF_PACKET for live capture
- **pcap files** - Offline analysis of captured traffic
- **Mirror ports** - SPAN/TAP traffic analysis
- **eBPF probes** - Low-overhead kernel-level capture
- **Socket statistics** - `ss` style connection monitoring

#### 2. Stream Processor

Non-blocking event processing pipeline:

**Parser Subsystem:**

- **Schema detection** - Auto-detect JSON, logfmt, syslog, common formats
- **Custom parsers** - Regex, Grok patterns, structured formats
- **Network dissection** - HTTP, DNS, TLS, TCP/UDP headers
- **Timestamp normalization** - Handle multiple time formats, timezones
- **Encoding** - UTF-8, ASCII, with fallback handling

**Filter Engine:**

- **Field-based filtering** - `level >= ERROR`
- **Regex matching** - `message =~ /timeout/i`
- **Network filters** - `tcp.port == 443 AND http.status >= 500`
- **Boolean logic** - Complex AND/OR/NOT expressions
- **Performance** - Compiled filter expressions for speed

**Aggregation Engine:**

- **Windowing** - Tumbling, sliding, session windows
- **Group by** - Aggregate by fields (service, status, etc.)
- **Functions** - COUNT, SUM, AVG, MIN, MAX, PERCENTILE
- **Top-K** - "Top 10 error messages in last hour"

#### 3. Correlation Engine

The unique value proposition of nblex:

**Correlation Strategies:**

1. **Time-based correlation** - Events within time windows

   ```text
   Match logs and packets within ±100ms
   ```

1. **ID-based correlation** - Trace/request/transaction IDs

   ```text
   log.request_id == http.header.x-request-id
   ```

1. **Connection correlation** - Match network flows to processes

   ```text
   tcp.src_port == log.connection_info.port
   ```

1. **Pattern correlation** - Behavioral patterns

   ```text
   "ERROR in logs" FOLLOWED BY "connection reset" within 5s
   ```

**Correlation Output:**

```json
{
  "correlation_id": "corr_abc123",
  "timestamp": "2025-11-08T10:30:45.123Z",
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

#### 4. Output Layer

Flexible export and action mechanisms:

**Output Types:**

- **stdout/stderr** - Human-readable or JSON
- **Files** - Append to files with rotation
- **HTTP webhooks** - POST to external services
- **Metrics exporters** - Prometheus, StatsD, OpenTelemetry
- **Export to alerting systems** - Slack, PagerDuty, email (via webhooks)
- **Databases** - PostgreSQL, ClickHouse for storage
- **Stream processors** - Kafka, NATS for downstream processing

______________________________________________________________________

## Features

### Phase 1: Foundation (v0.1-0.3) ✅ COMPLETE

**Goal:** Core infrastructure and basic functionality

#### Core Streaming Engine

- [x] Non-blocking I/O architecture (io_uring, epoll, kqueue)
- [x] Zero-copy buffer management where possible
- [x] Backpressure handling
- [x] Memory limits and quotas

#### Basic Log Processing

- [x] File tailing with rotation detection
- [x] File input watching with libuv fs_event support
- [x] JSON log parsing
- [x] Logfmt parsing
- [x] Common log formats (Apache, Nginx, syslog)
- [x] Custom regex patterns
- [x] Timestamp extraction and normalization

______________________________________________________________________

### Phase 2: Alpha Release (v0.4-0.5) 🟡 MOSTLY COMPLETE

**Goal:** Feature-complete for single-node deployment

#### Log Parsing & Format Support

- [x] JSON log parsing
- [x] Logfmt parsing
- [x] Syslog parsing (RFC 5424, RFC 3164)
- [x] Nginx log format parsing
- [x] Automatic format detection
- [x] Custom regex patterns

#### Network Protocol Dissection

- [x] Packet capture via libpcap
- [x] TCP/UDP header parsing
- [x] HTTP/1.1 request/response parsing
- [x] DNS query/response parsing
- [x] Connection tracking (flow table)

#### Filter Engine

- [x] Field-based filtering (level == "ERROR")
- [x] Regex matching (=~ operator)
- [x] Boolean logic (AND/OR/NOT)

#### Basic Query Language (nQL)

nblex uses a lightweight, filter-first query language (nQL) optimized for streaming log and network correlation. nQL is simpler than SQL, designed for real-time event processing, and builds on the existing filter expression syntax.

**Current status (2025-11-10, updated):**

- [x] Parser refactored with shared AST header and extended `nql_parse_ex` API
- [x] Executor updated to respect multi-stage pipelines
- [x] Unit tests expanded to cover additional parse/execute scenarios
- [x] Decision: derived query results will be emitted as synthetic `nblex_event` instances
- [x] SELECT, WHERE, GROUP BY (basic implementation)
- [x] Field-based filtering
- [x] Aggregation state management implemented with lazy timer initialization
- [x] Windowing support (tumbling, sliding, session windows)
- [x] Correlation query execution with bidirectional matching
- [x] Derived-event payload schema defined and documented (see `docs/derived-event-schema.md`)
- [x] Comprehensive test suite refactored into logical modules (parse, execute, windows, schema)
- [x] Tests for aggregates, correlations, and advanced pipelines end-to-end

**Core Syntax:**

```bash
# Simple filtering (reuses existing filter syntax)
log.level == ERROR
log.level >= WARN AND network.port == 443
log.message =~ /timeout/i

# Correlation queries
correlate log.level == ERROR with network.dst_port == 3306 within 100ms
correlate log.status >= 500 with network.tcp.retransmits > 0 within 1s

# Aggregation
aggregate count() where log.level == ERROR
aggregate count() by log.service where log.level == ERROR window 1m
aggregate count(), avg(network.latency_ms) by log.endpoint window tumbling(1m)

# Pipeline (chaining operations)
log.level == ERROR | aggregate count() by log.service
correlate log.level == ERROR with network.retransmits > 0 within 100ms | aggregate count()

# Field selection
show log.service, log.message, network.latency_ms where log.level == ERROR
* where log.level == ERROR
```

**Key Features:**

- **Filter-first**: Filters are the primary operation (like LogQL)
- **Pipeline-based**: Operations chain naturally with `|` (like PromQL)
- **Correlation-native**: Built-in `correlate ... with ...` syntax
- **Stream-optimized**: Designed for real-time event processing
- **Leverages existing**: Reuses filter engine expression syntax

#### Basic Correlation

- [x] Time-based correlation (configurable window)
- [x] Basic filtering (log.level AND network.port)
- [x] JSON output

#### Output Types

- [x] JSON output
- [x] File output
- [x] HTTP output
- [x] Metrics output (Prometheus) with aggregation metrics export

#### Configuration & CLI

- [x] YAML configuration file support
- [x] CLI tool with basic options

```bash
nblex monitor \
  --logs /var/log/app/*.log \
  --network eth0 \
  --filter 'log.level == ERROR' \
  --output json
```

#### Testing

- [x] Parser unit tests (>70% coverage for core parsers)
- [x] Filter engine unit tests (comprehensive coverage)
- [x] nQL parser tests (test_nql_parse.c)
- [x] nQL execution tests (test_nql_execute.c) - filters, pipelines, aggregates, correlations
- [x] nQL windowing tests (test_nql_windows.c) - tumbling, sliding, session windows
- [x] nQL schema validation tests (test_nql_schema.c) - aggregate and correlation event schemas
- [x] Derived event schema tests
- [x] Metrics output tests (test_metrics_output.c) - aggregation metrics export
- [x] Shared test helpers for event creation and capture
- [x] Integration tests

**Integration Test Coverage (as of 2025-11-10):**

**Implemented Integration Tests:**

- [x] **Correlation Integration** (`test_integration_correlation.c`)
  - Time window correlation (log + network events within window)
  - Events outside correlation window (should not correlate)
  - Bidirectional matching (network → log and log → network)
  - Multiple events in correlation buffer

- [x] **Configuration Integration** (`test_integration_config.c`)
  - YAML configuration loading and application to world
  - Correlation settings (enabled, window_ms)
  - Performance settings (worker_threads, buffer_size, memory_limit)
  - Multiple input configuration

- [x] **Output Integration** (`test_integration_output.c`)
  - JSON serialization for log events
  - JSON serialization for correlation events
  - Event handler pipeline (event capture and processing)
  - Multiple events handling

**Missing Integration Tests:**

- [ ] **File Input Integration** (`test_integration_file.c`)
  - File tailing with libuv fs_event (once cleanup is stabilized)
  - File input → JSON parsing → filter pipeline
  - Multiple log formats (JSON, logfmt, syslog) end-to-end
  - File rotation detection and handling

- [ ] **End-to-End Flows**
  - File input + network input simultaneously
  - Log parsing → correlation → output pipeline
  - Multiple input sources with different formats
  - Real pcap file processing (offline analysis)

- [ ] **Output Formatters Integration**
  - File output (writing, rotation management)
  - HTTP output (webhook delivery, method/timeout configuration)
  - Metrics output (Prometheus format validation, endpoint delivery)

- [ ] **Resource Limits**
  - Memory quota enforcement
  - CPU quota enforcement
  - Buffer size limits
  - Event rate limiting

- [ ] **Multi-Stage Pipeline**
  - nQL query execution end-to-end
  - Filter → aggregate → output pipeline
  - Correlation → aggregation → metrics pipeline

**Test Coverage Gaps (as of 2025-11-10):**

**Core Components:**

- [x] `nblex_world.c` - World lifecycle, event handling, start/stop/run
- [x] `config.c` - YAML configuration parsing and application
- [ ] `nblex_event.c` - Event creation, emission, type checking (partial via integration)

**Input Components:**

- [ ] `pcap_input.c` - Live packet capture (only mock tests exist)
- [ ] `input_base.c` - Input management functions (format detection tested)

**Parser Components:**

- [ ] `dns_parser.c` - DNS query/response parsing
- [ ] `http_parser.c` - HTTP/1.1 request/response parsing
- [ ] `regex_parser.c` - Custom regex pattern parsing

**Output Components:**

- [ ] `file_output.c` - File writing, rotation management
- [ ] `http_output.c` - HTTP webhook output, method/timeout configuration
- [ ] `json_output.c` - JSON serialization and output

**Correlation Components:**

- [x] `time_correlation.c` - Time-based correlation strategies, event matching

**Utility Components:**

- [ ] `buffer.c` - Buffer management functions
- [ ] `memory.c` - Memory allocation wrappers

**Estimated Overall Coverage:** ~45-50% code coverage, ~70% functional coverage

#### Documentation

- [x] Derived event payload schema documentation (`docs/derived-event-schema.md`)
- [ ] API documentation
- [ ] User guide

______________________________________________________________________

### Phase 3: Beta Release (v0.6-0.7)

**Goal:** Usable for production debugging by early adopters

#### Advanced Parsing

- [ ] Multi-line log support (stack traces, etc.)
- [ ] Binary log formats (Protobuf, MessagePack)
- [ ] Custom Lua/JavaScript parsers

#### Advanced Network Protocol Dissection

- [ ] HTTP/2 and gRPC dissection
- [ ] TLS metadata extraction (SNI, versions, ciphers)

#### Enhanced Query Language (nQL)

- [x] Aggregation state management (completed in Phase 2)
- [x] Windowing (tumbling, sliding, session windows) (completed in Phase 2)
- [x] Correlation query execution (completed in Phase 2)
- [x] Derived-event payload schema definition (completed in Phase 2)
- [ ] Advanced pipeline support (multi-stage optimizations)

#### Enhanced Correlation

- [ ] Request ID tracking across services
- [ ] Process-to-connection mapping (via /proc, eBPF)
- [ ] Sequence detection (event A → event B → event C)
- [ ] ID-based correlation (trace/request/transaction IDs)
- [ ] Connection correlation (match network flows to processes)

#### Export to Alerting Systems

nblex exports correlated events to external alerting systems via webhooks and HTTP endpoints. Alerting logic and routing are handled by existing tools (PagerDuty, Slack, etc.), not built into nblex.

- [ ] HTTP webhook output for alerting systems
- [ ] Event filtering and routing to different endpoints
- [ ] Integration with existing alerting infrastructure

#### Enhanced Metrics Export

- [ ] Prometheus exporter for dashboards (enhanced)
- [ ] OpenTelemetry integration
- [ ] Custom metric definitions

#### Performance Optimizations

- [ ] SIMD-optimized parsing
- [ ] Lock-free data structures
- [ ] Zero-copy optimizations

#### Testing & Quality

- [x] Integration tests (basic coverage: correlation, config, output)
- [ ] Integration tests (comprehensive: file input, end-to-end flows, output formatters, resource limits)
- [ ] Benchmark suite
- [x] Unit tests for core components (world, config, correlation engine)
- [ ] Unit tests for output formatters (file, HTTP, JSON)
- [ ] Unit tests for network parsers (DNS, HTTP)
- [ ] Unit tests for pcap input (live capture tests)
- [x] Extended test coverage for aggregates, correlations, and advanced pipelines

#### Deployment & Distribution

- [ ] Docker images
- [ ] Helm charts for Kubernetes

______________________________________________________________________

### Phase 4: v1.0 Release (v0.8-1.0)

**Goal:** General availability with scale and polish

#### Performance & Scale

- [ ] eBPF-based capture (lower overhead than libpcap)
- [ ] Multi-core scaling
- [ ] Live traffic sampling (analyze 10% of packets)

#### Advanced Correlation

- [ ] Pattern correlation (behavioral patterns)
- [ ] Anomaly detection (ML-based patterns)

#### Export to Centralized Systems

- [ ] Export correlated events to centralized collection systems
- [ ] Integration with existing event pipelines (Kafka, NATS)
- [ ] Central query interface for accessing exported data

#### Export to Storage Systems

- [ ] Export to existing log stores (Elasticsearch, Loki, ClickHouse)
- [ ] Local ring buffer for short-term replay during debugging sessions
- [ ] Optional export to file formats (JSON, Parquet) for analysis

#### Advanced Features

- [ ] Encrypted log transport (TLS)
- [ ] Optional simple viewer for real-time correlation visualization
- [ ] Mature nQL execution (window management, result schemas, fault handling)

#### Language Bindings

- [ ] Python bindings (ctypes/CFFI)
- [ ] Go bindings (cgo)
- [ ] Rust bindings (bindgen)
- [ ] Node.js bindings (N-API)

#### Security & Quality

- [ ] Security audit
- [ ] Professional documentation
- [ ] Commercial support options

______________________________________________________________________

## Use Cases

### 1. Debugging Production Issues

**Scenario:** API endpoint returning errors intermittently

```bash
nblex monitor \
  --logs /var/log/nginx/access.log \
  --logs /var/log/app/error.log \
  --network any \
  --query 'correlate log.path == "/api/checkout" with network.dst_port == 3306 within 1s where log.status >= 500 OR network.tcp.retransmits > 0' \
  --output json > investigation.jsonl
```

**Output shows:** Database connection resets (network) happening exactly when checkout errors occur (logs)

### 2. Security Debugging

**Scenario:** Investigate potential data exfiltration during incident response

```bash
nblex monitor \
  --logs /var/log/auth.log \
  --network eth0 \
  --query 'correlate log.event == "ssh_login_success" with network.bytes_sent > 104857600 within 5m' \
  --output json > investigation.jsonl
```

**Output shows:** Large data transfers following SSH logins

### 3. Performance Analysis

**Scenario:** Find slow API endpoints

```bash
nblex monitor \
  --logs /var/log/app/access.log \
  --network lo \
  --query 'log.method == POST AND network.http.status == 200 | aggregate count(), percentile(network.latency_ms, 95) by log.endpoint window 1m | top(10) by percentile(network.latency_ms, 95)' \
  --output prometheus
```

**Output shows:** Top 10 slowest endpoints by P95 latency

### 4. Compliance & Audit

**Scenario:** Track database access with query logs

```bash
nblex monitor \
  --logs /var/log/app/queries.log \
  --network eth0 \
  --filter 'network.dst_port == 5432' \
  --query 'correlate log.query_type == "SELECT" with network.dst_ip | show log.user, log.table, network.src_ip, network.bytes_transferred' \
  --output postgresql://audit_db/access_log
```

**Output shows:** Complete audit trail of who queried what database tables

### 5. Microservices Tracing

**Scenario:** Trace requests across services

```bash
nblex monitor \
  --logs /var/log/service-*/app.log \
  --network any \
  --query 'trace network.http.header.x-request-id show log.service, log.duration_ms, network.latency_ms, log.downstream_calls where network.http.status >= 400' \
  --format trace-timeline
```

**Output shows:** End-to-end request flow with timing at each hop

______________________________________________________________________

## API Design

### Command-Line Interface

#### Basic Usage

```bash
# Monitor logs only
nblex monitor --logs /var/log/app.log

# Monitor network only
nblex monitor --network eth0

# Combine both
nblex monitor \
  --logs /var/log/app.log \
  --network eth0 \
  --output json

# Use configuration file
nblex monitor --config /etc/nblex/config.yaml

# Offline analysis
nblex analyze \
  --logs archive.log.gz \
  --pcap traffic.pcap \
  --query @query.nql
```

#### Query Mode

```bash
# Interactive query shell
nblex query
nblex> log.level == ERROR limit 10

# One-shot query
nblex query "aggregate count() where network.port == 443"

# Load query from file
nblex query --file queries/errors.nql --output csv
```

#### Configuration Testing

```bash
# Validate configuration
nblex config validate /etc/nblex/config.yaml

# Test filters without running
nblex filter test --expr 'log.level >= WARN' --sample data.json

# Benchmark performance
nblex benchmark --logs test.log --duration 60s
```

### Configuration File Format

```yaml
# /etc/nblex/config.yaml
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
    - name: main_interface
      type: pcap
      interface: eth0
      snaplen: 65535
      filter: "tcp port 80 or tcp port 443"

    - name: database_traffic
      type: pcap
      interface: any
      filter: "tcp port 3306 or tcp port 5432"

processors:
  - name: parse_timestamps
    type: timestamp
    field: timestamp
    formats:
      - "2006-01-02T15:04:05Z07:00"
      - "02/Jan/2006:15:04:05 -0700"

  - name: enrich_geoip
    type: geoip
    source_field: network.src_ip
    target_field: geo
    database: /usr/share/GeoIP/GeoLite2-City.mmdb

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

    - type: connection
      match:
        log.connection.port: network.src_port
        log.connection.ip: network.src_ip

queries:
  - name: error_monitoring
    query: |
      SELECT
        log.service,
        COUNT(*) as errors
      FROM events
      WHERE log.level == ERROR
      GROUP BY log.service
      WINDOW tumbling(1 minute)
    outputs:
      - type: prometheus
        port: 9090

  - name: slow_requests
    query: |
      SELECT *
      FROM events
      WHERE
        network.http.method IN (GET, POST)
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

  - name: webhook_export
    type: http
    url: https://alerting.example.com/webhook
    method: POST

performance:
  worker_threads: 4
  buffer_size: 64MB
  max_memory: 1GB
  flow_table_size: 100000
```

### Library API (C)

```c
#include <nblex.h>

// Initialize nblex world
nblex_world* world = nblex_world_new();
nblex_world_open(world);

// Configure log input
nblex_input* log_input = nblex_input_file_new(world, "/var/log/app.log");
nblex_input_set_format(log_input, NBLEX_FORMAT_JSON);

// Configure network input
nblex_input* net_input = nblex_input_pcap_new(world, "eth0");
nblex_input_set_filter(net_input, "tcp port 443");

// Set up correlation
nblex_correlation* corr = nblex_correlation_new(world);
nblex_correlation_add_strategy(corr, NBLEX_CORR_TIME_BASED, 100); // 100ms window

// Define output handler
void on_event(nblex_event* event, void* user_data) {
    if (event->type == NBLEX_EVENT_CORRELATION) {
        printf("Correlated: %s\n", nblex_event_to_json(event));
    }
}

nblex_set_event_handler(world, on_event, NULL);

// Start processing
nblex_start(world);

// Run event loop (blocks)
nblex_run(world);

// Cleanup
nblex_free_world(world);
```

### Library API (Python)

```python
import nblex

# Create monitor
monitor = nblex.Monitor()

# Add inputs
monitor.add_log_input(
    path="/var/log/app.log",
    format="json"
)

monitor.add_network_input(
    interface="eth0",
    filter="tcp port 443"
)

# Enable correlation
monitor.enable_correlation(
    strategy="time_based",
    window_ms=100
)

# Define event handler
@monitor.on_event
def handle_event(event):
    if event.type == "correlation":
        print(f"Correlated: {event.log} <-> {event.network}")

# Start monitoring (non-blocking)
monitor.start()

# Or use as iterator
for event in monitor.stream():
    if event.log.level == "ERROR":
        print(f"Error: {event.log.message}")

# Query interface
results = monitor.query("""
    SELECT COUNT(*) as errors
    FROM events
    WHERE log.level == ERROR
    GROUP BY log.service
    WINDOW tumbling(1m)
""")

for row in results:
    print(f"{row.service}: {row.errors} errors")
```

### REST API (Programmatic Access)

For programmatic access and integration with existing tools, nblex provides a REST API for querying and exporting correlated events. This enables integration with existing workflows and tools without requiring nblex to run as a long-running service.

```bash
# Query correlated events programmatically
curl -X POST http://localhost:8080/api/v1/query \
  -H "Content-Type: application/json" \
  -d '{"query": "SELECT * FROM events WHERE log.level == ERROR LIMIT 100"}'
```

**Endpoints:**

```http
# Get current metrics
GET /api/v1/metrics

# Query events (programmatic access)
POST /api/v1/query
Content-Type: application/json

{
  "query": "SELECT * FROM events WHERE log.level == ERROR LIMIT 100",
  "format": "json"
}

# Export event stream (for integration)
GET /api/v1/stream?filter=log.level==ERROR

# Export correlations
GET /api/v1/correlations
POST /api/v1/correlations
```

______________________________________________________________________

## Technical Implementation

### Technology Stack

**Core Language:** C (for performance, portability)

- Targeting C11 (ISO/IEC 9899:2011) standard for modern features
- Minimal dependencies for base functionality
- Optional features can add dependencies

**Required Dependencies:**

- `libpcap` - Packet capture (or eBPF alternative)
- `libuv` - Event loop, async I/O
- `libjansson` - JSON parsing
- `PCRE2` - Regular expressions

**Optional Dependencies:**

- `librdkafka` - Kafka output
- `libpq` - PostgreSQL output
- `hiredis` - Redis output
- `libcurl` - HTTP webhooks
- `lua` - Custom scripting
- `maxminddb` - GeoIP enrichment

**Build System:**

- CMake (replacing Autotools)
- pkg-config support
- Conan/vcpkg for dependency management

**Bindings:**

- Python (ctypes/CFFI)
- Go (cgo)
- Rust (bindgen)
- Node.js (N-API)

### Testing Strategy

Testing nblex presents unique challenges due to its dual nature (logs + network) and real-time requirements. A comprehensive multi-layered approach is essential:

**Unit Testing:**

- **Parser tests** - Test each log format parser with known inputs/outputs
- **Protocol dissectors** - Test HTTP, DNS, TCP parsing with crafted packets
- **Filter expressions** - Test boolean logic, field matching, regex
- **Correlation algorithms** - Test time-based, ID-based matching with synthetic events
- **Framework:** Check or Unity for C unit testing
- **Coverage target:** >80% for core logic

**Integration Testing:**

- **End-to-end flows** - Inject logs + packets, verify correlated output
  - ✅ Basic correlation pipeline (log + network events)
  - ❌ File input + network input simultaneously
  - ❌ Real pcap file processing
- **Multiple input sources** - Test file + network simultaneously
  - ✅ Multiple events in correlation buffer
  - ❌ Multiple input sources with different formats
  - ❌ File rotation detection
- **Output validation** - Verify JSON, metrics, alerts are correct
  - ✅ JSON serialization (log and correlation events)
  - ✅ Event handler pipeline
  - ❌ File output (writing, rotation)
  - ❌ HTTP output (webhook delivery)
  - ❌ Metrics output (Prometheus format)
- **Resource limits** - Test memory/CPU quotas work
  - ❌ Memory quota enforcement
  - ❌ CPU quota enforcement
  - ❌ Buffer size limits
- **Framework:** Custom test harness using Check framework

**Synthetic Testing:**

- **Traffic generation** - Use tcpreplay for packet injection
- **Log generation** - Scripts to generate realistic log patterns
- **Known scenarios** - Pre-recorded pcap + log files with expected correlations
- **Example:** Error log at T+0ms, TCP RST at T+50ms → should correlate

**Fuzzing:**

- **Input fuzzing** - AFL or libFuzzer for parsers (UTF-8, JSON, HTTP, pcap)
- **Protocol fuzzing** - Malformed packets, invalid log formats
- **State fuzzing** - Random event sequences to find crashes
- **Critical for security** - Parsers handle untrusted input

**Performance Testing:**

- **Benchmarks** - Measure throughput (events/sec) with real-world data
- **Latency tests** - Measure p50/p95/p99 processing latency
- **Load tests** - Sustained high throughput (24+ hours)
- **Profiling** - perf, valgrind, flamegraphs to find bottlenecks
- **Tools:** Custom benchmark suite + perf

**Real-world Testing:**

- **Dogfooding** - Run nblex on its own logs/network
- **Beta deployments** - Real production environments (with permission)
- **Diverse environments** - Different log formats, traffic patterns, OS versions
- **Feedback loop** - User reports inform test cases

**Regression Testing:**

- **Test corpus** - Collection of real-world logs + pcaps (anonymized)
- **CI pipeline** - All tests run on every commit
- **Platform matrix** - Linux (Ubuntu, RHEL, Alpine), macOS, BSDs
- **Sanitizers** - AddressSanitizer, ThreadSanitizer, UndefinedBehaviorSanitizer
- **CI tools:** GitHub Actions, pre-commit hooks

**Challenges & Mitigations:**

1. **Challenge:** Timing-dependent correlation is hard to test deterministically

   - **Mitigation:** Mock time in tests, use synthetic timestamps

1. **Challenge:** Network capture requires root privileges

   - **Mitigation:** Use pre-captured pcap files for most tests, dedicated test VMs for live capture

1. **Challenge:** Difficult to reproduce real-world correlation scenarios

   - **Mitigation:** Build corpus of real incidents (anonymized), community-contributed test cases

1. **Challenge:** Performance tests may vary by hardware

   - **Mitigation:** Relative benchmarks (vs. baseline), normalized metrics, dedicated test hardware

1. **Challenge:** Testing distributed mode requires multiple machines

   - **Mitigation:** Docker Compose for local multi-node testing, cloud ephemeral instances for CI

**Test Data Management:**

```text
tests/
├── unit/                    # Unit tests for individual components
│   ├── test_parsers.c
│   ├── test_correlation.c
│   └── test_filters.c
├── integration/             # End-to-end integration tests
│   ├── test_file_and_pcap.sh
│   └── test_alerting.sh
├── data/                    # Test data corpus
│   ├── logs/
│   │   ├── nginx_access.log
│   │   ├── app_errors.json
│   │   └── syslog_samples.log
│   ├── pcaps/
│   │   ├── http_traffic.pcap
│   │   ├── dns_queries.pcap
│   │   └── tcp_retransmits.pcap
│   └── scenarios/           # Known correlation scenarios
│       ├── scenario_001_error_with_network_timeout/
│       │   ├── input.log
│       │   ├── input.pcap
│       │   └── expected_output.json
│       └── scenario_002_sql_injection_attempt/
│           ├── input.log
│           ├── input.pcap
│           └── expected_output.json
├── fuzz/                    # Fuzzing harnesses
│   ├── fuzz_json_parser.c
│   └── fuzz_http_parser.c
└── bench/                   # Performance benchmarks
    ├── bench_throughput.c
    └── bench_latency.c
```

### Performance Targets

**Throughput:**

- Logs: 100,000 lines/sec (single core)
- Network: 10 Gbps (with filtering)
- Combined: 50,000 correlated events/sec

**Latency:**

- End-to-end processing: < 10ms (p95)
- Correlation latency: < 5ms

**Resource Usage:**

- Memory: < 100MB baseline, configurable limits
- CPU: < 20% on idle monitoring
- Disk I/O: Optional, all in-memory by default

**Scaling:**

- Single node: 100K events/sec
- Distributed: 1M+ events/sec

### Security Considerations

**Privilege Requirements:**

- Raw packet capture requires `CAP_NET_RAW` or root
- eBPF requires `CAP_BPF` or root
- File reading requires appropriate permissions

**Privilege Dropping:**

```yaml
security:
  drop_privileges:
    enabled: true
    user: nblex
    group: nblex
  capabilities:
    - CAP_NET_RAW  # Only keep what's needed
```

**Data Handling:**

- Sensitive data masking (credit cards, passwords)
- Configurable PII filtering
- No data persistence by default
- TLS for network communication

**Input Validation:**

- All external inputs sanitized
- Query language parser with limits
- Resource quotas to prevent DoS

______________________________________________________________________

## Milestones & Roadmap

### Milestone 1: Proof of Concept (2 months) ✅ COMPLETE

**Goal:** Demonstrate basic correlation between logs and network

**Deliverables:**

- [x] Core event loop (libuv-based)
- [x] File tailing with JSON parsing
- [x] Basic pcap capture and HTTP parsing
- [x] Time-based correlation (±100ms window)
- [x] JSON output
- [x] CLI tool with basic options
- [x] README and getting started guide

**Success Criteria:**

- ✅ Can monitor a log file and network interface simultaneously
- ✅ Can correlate ERROR logs with network events
- ✅ Outputs correlated events as JSON

### Milestone 2: Alpha Release (v0.4-0.5) 🟡 IN PROGRESS

**Goal:** Feature-complete for single-node deployment

**Status:** Mostly complete - see Phase 2 in Features section

**Remaining Deliverables:**

- [ ] API documentation
- [ ] User guide

**Success Criteria:**

- Can handle 10K events/sec on commodity hardware
- Runs stably for 24+ hours
- Documentation sufficient for self-service usage

### Milestone 3: Beta Release (v0.6-0.7)

**Goal:** Usable for production debugging by early adopters

**Deliverables:** See Phase 3 in Features section

- [ ] Advanced correlation (ID-based, sequence detection, process-to-connection mapping)
- [ ] Enhanced nQL (aggregation state, windowing, correlation execution)
- [ ] Export to alerting systems (webhooks, HTTP endpoints)
- [ ] Enhanced metrics export (OpenTelemetry, custom metrics)
- [ ] Performance optimizations (SIMD, lock-free structures, zero-copy)
- [x] Integration tests
- [ ] Benchmark suite
- [ ] Docker images
- [ ] Helm charts for Kubernetes

**Success Criteria:**

- Meets performance targets (50K events/sec)
- Used for production debugging by 5+ organizations
- No critical bugs for 1+ month

### Milestone 4: v1.0 Release (v0.8-1.0)

**Goal:** General availability

**Deliverables:** See Phase 4 in Features section

- [ ] eBPF-based capture option
- [ ] Multi-core scaling
- [ ] Advanced correlation (pattern correlation, anomaly detection)
- [ ] Export to centralized systems (Kafka, NATS)
- [ ] Export to storage systems (Elasticsearch, Loki, ClickHouse)
- [ ] Mature nQL execution (window management, result schemas, fault handling)
- [ ] Language bindings (Python, Go, Rust, Node.js)
- [ ] Optional simple viewer for correlation visualization
- [ ] Security audit
- [ ] Professional documentation
- [ ] Commercial support options

**Success Criteria:**

- Feature parity with competitors in core correlation areas
- 1,000+ GitHub stars
- Active community (Discord, forum)
- Clear path to v2.0

______________________________________________________________________

## Success Metrics

### Technical Metrics

- **Performance:** Sustained 100K events/sec throughput
- **Reliability:** Stable for debugging sessions, handles edge cases gracefully
- **Accuracy:** >95% correlation accuracy (based on ground truth)
- **Latency:** p95 < 10ms for event processing

### Adoption Metrics

- **Users:** 1,000 active deployments within 1 year
- **Contributors:** 20+ code contributors
- **Integrations:** 10+ third-party integrations (Grafana, Datadog, etc.)
- **Stars:** 5,000+ GitHub stars

### Business Metrics

- **Market:** Clear differentiation vs. competitors (Splunk, Datadog, ELK)
- **Revenue:** Sustainable funding model (enterprise, cloud, support)
- **Community:** Active Discord/Slack with 500+ members

______________________________________________________________________

## Competitive Analysis

### Existing Solutions

| Tool | Logs | Network | Correlation | Open Source | Performance |
|------|------|---------|-------------|-------------|-------------|
| **Splunk** | ✅ | ⚠️ Limited | ⚠️ Manual | ❌ | Medium |
| **Datadog** | ✅ | ✅ APM | ⚠️ Separate | ❌ | High |
| **ELK Stack** | ✅ | ❌ | ❌ | ✅ | Medium |
| **Wireshark** | ❌ | ✅ | ❌ | ✅ | Low |
| **tcpdump** | ❌ | ✅ | ❌ | ✅ | High |
| **Vector** | ✅ | ❌ | ❌ | ✅ | High |
| **Fluentd** | ✅ | ❌ | ❌ | ✅ | Medium |
| **nblex** | ✅ | ✅ | ✅ Auto | ✅ | High |

### Differentiation

1. **Automatic correlation** - No manual dashboard building needed
1. **Unified tool** - One tool, not a stack of components
1. **Lightweight** - Single binary, minimal dependencies
1. **Real-time** - Sub-second latency from event to insight
1. **Developer-first** - CLI-native, scriptable, embeddable

______________________________________________________________________

## Open Questions & Decisions Needed

### Technical Decisions

1. **Query language design**

   - Custom DSL vs. SQL-like vs. existing language (PromQL, KQL)
   - Decision: nQL (nblex Query Language) - lightweight, filter-first DSL optimized for streaming correlation, inspired by LogQL and PromQL but simpler than SQL

1. **State management**

   - In-memory only vs. optional persistence
   - Decision: In-memory by default, optional RocksDB for state

1. **Distribution architecture**

   - Centralized vs. peer-to-peer
   - Decision: Centralized server model (simpler v1)

1. **Plugin system**

   - Lua vs. WebAssembly vs. native shared libraries
   - Decision: Start with Lua (simpler), add WASM later

### Business Decisions

1. **Licensing**

   - Apache 2.0 vs. GPL vs. dual-license (open core)
   - Decision: Start with Apache 2.0 for maximum adoption
   - Note: Final license will depend on dependencies used (e.g., libpcap is BSD, libuv is MIT, both compatible with Apache 2.0)

1. **Monetization**

   - Open source only vs. enterprise features vs. cloud service
   - Decision: Open source core, optional cloud service

1. **Governance**

   - Individual vs. foundation (CNCF, Apache)
   - Decision: Individual initially, consider CNCF after traction

### Community Decisions

1. **Communication channels**

   - GitHub Discussions vs. Discord vs. Discourse
   - Decision: GitHub Discussions + Discord for real-time

1. **Release cadence**

   - Time-based (quarterly) vs. feature-based
   - Decision: Feature-based until v1.0, then quarterly

______________________________________________________________________

## Risks & Mitigation

### Technical Risks

**Risk:** Performance doesn't meet targets

- **Mitigation:** Early benchmarking, profiling, SIMD optimizations
- **Fallback:** Reduce scope (e.g., sample traffic instead of all)

**Risk:** Correlation accuracy is low

- **Mitigation:** Start with simple correlations, add ML later
- **Fallback:** Manual correlation rules as alternative

**Risk:** Memory usage too high with large flow tables

- **Mitigation:** LRU eviction, configurable limits
- **Fallback:** Sampling, external state store

### Market Risks

**Risk:** Low adoption due to crowded market

- **Mitigation:** Clear differentiation, strong marketing, community building
- **Fallback:** Focus on niche (e.g., security teams only)

**Risk:** Competing with well-funded vendors

- **Mitigation:** Open source advantage, faster iteration
- **Fallback:** Partner with vendors instead of compete

### Operational Risks

**Risk:** Maintainer burnout

- **Mitigation:** Build contributor community early
- **Fallback:** Find co-maintainers or corporate sponsor

**Risk:** Security vulnerabilities in packet parsing

- **Mitigation:** Fuzzing, security audits, safe parsing libraries
- **Fallback:** Disable network monitoring by default

______________________________________________________________________

## Appendix

### Glossary

- **Correlation:** Linking related events across different data sources
- **Flow:** A network connection identified by 5-tuple (src IP/port, dst IP/port, protocol)
- **Sentinel value:** Special value indicating invalid/end-of-input (from original nblex)
- **Window:** Time period for aggregating or correlating events
- **Dissector:** Parser for a specific network protocol
- **nQL:** nblex Query Language (SQL-like syntax for streaming queries)

### References

- [Zeek Network Monitor](https://zeek.org/) - Similar correlation concepts
- [Sysdig](https://sysdig.com/) - System call + network monitoring
- [Vector](https://vector.dev/) - High-performance log routing
- [BPF Performance Tools](http://www.brendangregg.com/bpf-performance-tools-book.html)
- [Log Parsing Best Practices](https://www.splunk.com/en_us/blog/tips-and-tricks/log-parsing-best-practices.html)

### Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, coding standards, and pull request process.

### License

Apache License 2.0 - See [LICENSE](LICENSE) file for details.

**Dependency Licenses:**

- libpcap - BSD 3-Clause (compatible with Apache 2.0)
- libuv - MIT (compatible with Apache 2.0)
- libjansson - MIT (compatible with Apache 2.0)
- PCRE2 - BSD 3-Clause (compatible with Apache 2.0)

All required dependencies use permissive licenses compatible with Apache 2.0. The final combined work may be distributed under Apache 2.0.

______________________________________________________________________

**Document Status:** Living document - updated as project evolves
**Feedback:** Open an issue or discussion on GitHub
