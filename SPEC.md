# nblex Specification
## Network & Buffer Log EXplorer

**Version:** 2.0
**Status:** Draft
**Last Updated:** 2025-11-08

---

## Executive Summary

**nblex** (Network & Buffer Log EXplorer) is a high-performance, real-time observability platform that uniquely combines application log analysis with network traffic monitoring. By correlating events across both layers, nblex provides unprecedented visibility into system behavior, enabling faster debugging, security detection, and performance optimization.

### Key Differentiator
While traditional tools analyze logs OR network traffic in isolation, nblex correlates both simultaneously, answering questions like:
- "Show me all ERROR logs that occurred during network timeouts"
- "Which API endpoints are logging errors but returning 200 OK?"
- "Correlate database query logs with actual network traffic to MySQL"

---

## Vision & Goals

### Vision
Enable developers, SREs, and security teams to understand system behavior by unifying application-level insights (logs) with network-level reality (packets).

### Primary Goals
1. **Real-time streaming analysis** - Process logs and network traffic as they occur
2. **Correlation engine** - Link events across application and network layers
3. **Non-blocking architecture** - Handle high-throughput environments efficiently
4. **Developer-friendly** - Simple to deploy, query, and integrate
5. **Production-ready** - Minimal overhead, reliable, observable

### Non-Goals (v1.0)
- Full-featured log storage (use existing solutions: Elasticsearch, Loki, etc.)
- Deep packet inspection beyond L7 protocols (HTTP, DNS, TLS)
- Log aggregation from hundreds of sources (focus on quality over quantity)
- Replace APM tools (complement, don't compete)

---

## Architecture

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        nblex Core                            │
│                                                              │
│  ┌─────────────┐      ┌──────────────┐      ┌────────────┐ │
│  │   Inputs    │─────▶│   Stream     │─────▶│  Outputs   │ │
│  │             │      │  Processor   │      │            │ │
│  │ • Files     │      │              │      │ • stdout   │ │
│  │ • Sockets   │      │ • Parse      │      │ • Files    │ │
│  │ • pcap      │      │ • Filter     │      │ • HTTP     │ │
│  │ • Journals  │      │ • Correlate  │      │ • Metrics  │ │
│  └─────────────┘      │ • Aggregate  │      │ • Alerts   │ │
│                       └──────────────┘      └────────────┘ │
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
   ```
   Match logs and packets within ±100ms
   ```

2. **ID-based correlation** - Trace/request/transaction IDs
   ```
   log.request_id == http.header.x-request-id
   ```

3. **Connection correlation** - Match network flows to processes
   ```
   tcp.src_port == log.connection_info.port
   ```

4. **Pattern correlation** - Behavioral patterns
   ```
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
- **Alert systems** - Slack, PagerDuty, email
- **Databases** - PostgreSQL, ClickHouse for storage
- **Stream processors** - Kafka, NATS for downstream processing

---

## Features

### Phase 1: Foundation (v0.1-0.3)

#### Core Streaming Engine
- [x] Non-blocking I/O architecture (io_uring, epoll, kqueue)
- [x] Zero-copy buffer management where possible
- [x] Backpressure handling
- [x] Memory limits and quotas

#### Basic Log Processing
- [x] File tailing with rotation detection
- [x] JSON log parsing
- [x] Logfmt parsing
- [x] Common log formats (Apache, Nginx, syslog)
- [x] Custom regex patterns
- [x] Timestamp extraction and normalization

#### Basic Network Monitoring
- [x] Packet capture via libpcap
- [x] TCP/UDP header parsing
- [x] HTTP/1.1 request/response parsing
- [x] DNS query/response parsing
- [x] Connection tracking (flow table)

#### Simple Correlation
- [x] Time-based correlation (configurable window)
- [x] Basic filtering (log.level AND network.port)
- [x] JSON output

#### CLI Tool
```bash
nblex monitor \
  --logs /var/log/app/*.log \
  --network eth0 \
  --filter 'log.level == ERROR' \
  --output json
```

### Phase 2: Intelligence (v0.4-0.6)

#### Advanced Parsing
- [ ] Multi-line log support (stack traces, etc.)
- [ ] Binary log formats (Protobuf, MessagePack)
- [ ] HTTP/2 and gRPC dissection
- [ ] TLS metadata extraction (SNI, versions, ciphers)
- [ ] Custom Lua/JavaScript parsers

#### Query Language
SQL-like query language for real-time analysis:

```sql
-- nblex Query Language (nQL)
SELECT
  log.service,
  COUNT(*) as error_count,
  AVG(network.latency_ms) as avg_latency
FROM events
WHERE
  log.level >= ERROR
  AND network.tcp.retransmits > 0
GROUP BY log.service
WINDOW tumbling(1 minute)
HAVING error_count > 10
```

#### Enhanced Correlation
- [ ] Request ID tracking across services
- [ ] Process-to-connection mapping (via /proc, eBPF)
- [ ] Sequence detection (event A → event B → event C)
- [ ] Anomaly detection (ML-based patterns)

#### Alerting
```yaml
# alerts.yaml
alerts:
  - name: high_error_rate_with_network_issues
    query: |
      SELECT COUNT(*) as errors
      FROM events
      WHERE log.level == ERROR
        AND network.tcp.retransmits > 5
      WINDOW tumbling(1 minute)
    condition: errors > 100
    actions:
      - type: slack
        channel: "#alerts"
      - type: webhook
        url: https://oncall.example.com/alert
```

#### Metrics Export
- [ ] Prometheus exporter for dashboards
- [ ] OpenTelemetry integration
- [ ] Custom metric definitions

### Phase 3: Scale (v0.7-1.0)

#### Performance
- [ ] eBPF-based capture (lower overhead than libpcap)
- [ ] SIMD-optimized parsing
- [ ] Lock-free data structures
- [ ] Multi-core scaling

#### Distributed Mode
- [ ] Agent/server architecture
- [ ] Distributed correlation across multiple hosts
- [ ] Central query interface
- [ ] Cluster coordination (etcd/Consul)

#### Storage Integration
- [ ] Local ring buffer for replay
- [ ] Optional persistence (SQLite, Parquet files)
- [ ] Integration with existing log stores (Elasticsearch, Loki)
- [ ] Clickhouse for fast analytics

#### Advanced Features
- [ ] Live traffic sampling (analyze 10% of packets)
- [ ] Encrypted log transport (TLS)
- [ ] Multi-tenancy support
- [ ] Role-based access control
- [ ] Web UI for visualization

---

## Use Cases

### 1. Debugging Production Issues

**Scenario:** API endpoint returning errors intermittently

```bash
nblex monitor \
  --logs /var/log/nginx/access.log \
  --logs /var/log/app/error.log \
  --network any \
  --query '
    CORRELATE
      log.path == "/api/checkout"
      WITH network.dst_port == 3306
    WHERE
      log.status >= 500
      OR network.tcp.retransmits > 0
    WINDOW 1 second
  ' \
  --output json > investigation.jsonl
```

**Output shows:** Database connection resets (network) happening exactly when checkout errors occur (logs)

### 2. Security Monitoring

**Scenario:** Detect potential data exfiltration

```bash
nblex monitor \
  --logs /var/log/auth.log \
  --network eth0 \
  --query '
    CORRELATE
      log.event == "ssh_login_success"
      FOLLOWED BY network.bytes_sent > 100MB
    WITHIN 5 minutes
  ' \
  --alert 'slack://security-alerts'
```

**Output shows:** Large data transfers following SSH logins

### 3. Performance Analysis

**Scenario:** Find slow API endpoints

```bash
nblex monitor \
  --logs /var/log/app/access.log \
  --network lo \
  --query '
    SELECT
      log.endpoint,
      PERCENTILE(network.latency_ms, 95) as p95_latency,
      COUNT(*) as requests
    WHERE
      log.method == "POST"
      AND network.http.status == 200
    GROUP BY log.endpoint
    WINDOW tumbling(1 minute)
    ORDER BY p95_latency DESC
    LIMIT 10
  ' \
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
  --query '
    CORRELATE
      log.query_type == "SELECT"
      WITH network.dst_ip
    ENRICH
      log.user,
      log.table,
      network.src_ip,
      network.bytes_transferred
  ' \
  --output postgresql://audit_db/access_log
```

**Output shows:** Complete audit trail of who queried what database tables

### 5. Microservices Tracing

**Scenario:** Trace requests across services

```bash
nblex monitor \
  --logs /var/log/service-*/app.log \
  --network any \
  --query '
    TRACE network.http.header.x-request-id
    SHOW
      log.service,
      log.duration_ms,
      network.latency_ms,
      log.downstream_calls
    WHERE
      network.http.status >= 400
  ' \
  --format trace-timeline
```

**Output shows:** End-to-end request flow with timing at each hop

---

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
  --query @query.sql
```

#### Query Mode
```bash
# Interactive query shell
nblex query
nblex> SELECT * FROM logs WHERE level == ERROR LIMIT 10;

# One-shot query
nblex query "SELECT COUNT(*) FROM events WHERE network.port == 443"

# Load query from file
nblex query --file queries/errors.sql --output csv
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

alerts:
  - name: high_error_rate
    query: |
      SELECT COUNT(*) as count
      FROM events
      WHERE log.level == ERROR
      WINDOW tumbling(1 minute)
    condition: count > 100
    actions:
      - type: slack
        webhook: ${SLACK_WEBHOOK_URL}
        message: "High error rate detected: {{count}} errors/minute"

      - type: webhook
        url: https://oncall.example.com/alert
        method: POST
        body: |
          {
            "alert": "high_error_rate",
            "severity": "warning",
            "value": {{count}}
          }

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

### REST API (Server Mode)

```bash
# Start server
nblex serve --config /etc/nblex/config.yaml --port 8080
```

**Endpoints:**

```http
# Get current metrics
GET /api/v1/metrics

# Query events
POST /api/v1/query
Content-Type: application/json

{
  "query": "SELECT * FROM events WHERE log.level == ERROR LIMIT 100",
  "format": "json"
}

# Live event stream (Server-Sent Events)
GET /api/v1/stream?filter=log.level==ERROR

# Manage correlations
GET /api/v1/correlations
POST /api/v1/correlations

# Health check
GET /health
```

---

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
- **Multiple input sources** - Test file + network simultaneously
- **Output validation** - Verify JSON, metrics, alerts are correct
- **Resource limits** - Test memory/CPU quotas work
- **Framework:** Custom test harness + shell scripts

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

2. **Challenge:** Network capture requires root privileges
   - **Mitigation:** Use pre-captured pcap files for most tests, dedicated test VMs for live capture

3. **Challenge:** Difficult to reproduce real-world correlation scenarios
   - **Mitigation:** Build corpus of real incidents (anonymized), community-contributed test cases

4. **Challenge:** Performance tests may vary by hardware
   - **Mitigation:** Relative benchmarks (vs. baseline), normalized metrics, dedicated test hardware

5. **Challenge:** Testing distributed mode requires multiple machines
   - **Mitigation:** Docker Compose for local multi-node testing, cloud ephemeral instances for CI

**Test Data Management:**
```
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

---

## Milestones & Roadmap

### Milestone 1: Proof of Concept (2 months)
**Goal:** Demonstrate basic correlation between logs and network

**Deliverables:**
- [x] Core event loop (libuv-based)
- [x] File tailing with JSON parsing
- [x] Basic pcap capture and HTTP parsing
- [x] Time-based correlation (±100ms window)
- [x] JSON output
- [ ] CLI tool with basic options
- [ ] README and getting started guide

**Success Criteria:**
- Can monitor a log file and network interface simultaneously
- Can correlate ERROR logs with network events
- Outputs correlated events as JSON

### Milestone 2: Alpha Release (4 months)
**Goal:** Feature-complete for single-node deployment

**Deliverables:**
- [ ] Multi-format log parsing (JSON, logfmt, syslog, regex)
- [ ] HTTP/1.1, DNS, TCP/UDP dissection
- [ ] Filter expressions (field-based, regex)
- [ ] Basic query language (SELECT, WHERE, GROUP BY)
- [ ] Multiple output types (file, HTTP, metrics)
- [ ] Configuration file support (YAML)
- [ ] Unit tests (>70% coverage)
- [ ] Documentation site

**Success Criteria:**
- Can handle 10K events/sec on commodity hardware
- Runs stably for 24+ hours
- Documentation sufficient for self-service usage

### Milestone 3: Beta Release (6 months)
**Goal:** Production-ready for early adopters

**Deliverables:**
- [ ] Advanced correlation (ID-based, sequence detection)
- [ ] Alerting system with multiple backends
- [ ] Prometheus/OpenTelemetry export
- [ ] Performance optimizations (SIMD, zero-copy)
- [ ] Integration tests
- [ ] Benchmark suite
- [ ] Docker images
- [ ] Helm charts for Kubernetes

**Success Criteria:**
- Meets performance targets (50K events/sec)
- Used in production by 5+ organizations
- No critical bugs for 1+ month

### Milestone 4: v1.0 Release (9 months)
**Goal:** General availability

**Deliverables:**
- [ ] eBPF-based capture option
- [ ] Web UI for visualization
- [ ] Distributed mode (agent/server)
- [ ] Storage integration (Elasticsearch, Loki, ClickHouse)
- [ ] Language bindings (Python, Go)
- [ ] Security audit
- [ ] Professional documentation
- [ ] Commercial support options

**Success Criteria:**
- Feature parity with competitors in core areas
- 1,000+ GitHub stars
- Active community (Discord, forum)
- Clear path to v2.0

---

## Success Metrics

### Technical Metrics
- **Performance:** Sustained 100K events/sec throughput
- **Reliability:** 99.9% uptime in production deployments
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

---

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
2. **Unified tool** - One tool, not a stack of components
3. **Lightweight** - Single binary, minimal dependencies
4. **Real-time** - Sub-second latency from event to insight
5. **Developer-first** - CLI-native, scriptable, embeddable

---

## Open Questions & Decisions Needed

### Technical Decisions
1. **Query language design**
   - Custom DSL vs. SQL-like vs. existing language (PromQL, KQL)
   - Decision: SQL-like for familiarity, with extensions for streaming

2. **State management**
   - In-memory only vs. optional persistence
   - Decision: In-memory by default, optional RocksDB for state

3. **Distribution architecture**
   - Centralized vs. peer-to-peer
   - Decision: Centralized server model (simpler v1)

4. **Plugin system**
   - Lua vs. WebAssembly vs. native shared libraries
   - Decision: Start with Lua (simpler), add WASM later

### Business Decisions
1. **Licensing**
   - Apache 2.0 vs. GPL vs. dual-license (open core)
   - Decision: Start with Apache 2.0 for maximum adoption
   - Note: Final license will depend on dependencies used (e.g., libpcap is BSD, libuv is MIT, both compatible with Apache 2.0)

2. **Monetization**
   - Open source only vs. enterprise features vs. cloud service
   - Decision: Open source core, optional cloud service

3. **Governance**
   - Individual vs. foundation (CNCF, Apache)
   - Decision: Individual initially, consider CNCF after traction

### Community Decisions
1. **Communication channels**
   - GitHub Discussions vs. Discord vs. Discourse
   - Decision: GitHub Discussions + Discord for real-time

2. **Release cadence**
   - Time-based (quarterly) vs. feature-based
   - Decision: Feature-based until v1.0, then quarterly

---

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

---

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

---

**Document Status:** Living document - updated as project evolves
**Feedback:** Open an issue or discussion on GitHub
