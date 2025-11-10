# nblex - Network & Buffer Log EXplorer

**Status:** 🚧 Under active development - Foundation complete, core features in progress

A lightweight correlation tool that correlates application logs with network traffic in real-time, providing unprecedented visibility into system behavior during debugging sessions.

## What is nblex?

nblex uniquely combines **log analysis** with **network monitoring** to answer questions that traditional tools can't:

- "Show me all ERROR logs that occurred during TCP retransmissions"
- "Which API endpoints log success but return HTTP 500?"
- "Correlate slow database queries with actual network latency to MySQL"
- "What happened in the application when this network timeout occurred?"

### The Problem

Traditional observability tools operate in silos:

- **Log analyzers** (ELK, Splunk) don't see network events
- **Network monitors** (Wireshark, tcpdump) don't see application context
- **Correlation is manual** - you stare at dashboards trying to connect the dots

### The Solution

nblex automatically correlates events across both layers:

```text
Application Layer (logs) + Network Layer (packets) = Complete Picture
```

## Vision

When complete, nblex will:

1. **Stream logs and network traffic simultaneously**
   - Tail log files, syslog, container logs
   - Capture packets via pcap or eBPF
   - Non-blocking architecture for high throughput

2. **Automatically correlate events**
   - Time-based correlation (events within time windows)
   - ID-based correlation (trace IDs, request IDs)
   - Connection correlation (match network flows to processes)

3. **Provide actionable insights**
   - SQL-like query language for ad-hoc analysis
   - Export to existing tools (metrics, dashboards, storage)

## Current Status

**✅ Milestone 1 Complete:**

- Project structure and build system (CMake)
- Public API defined (`include/nblex/nblex.h`)
- Core world management (initialization, lifecycle)
- Event loop implementation (libuv-based)
- File tailing with JSON parsing
- Network capture with libpcap
- HTTP/1.1 protocol parsing
- Time-based correlation engine (±100ms)
- JSON output
- Command-line tool with basic options
- Testing infrastructure
- Documentation and specification

**✅ Milestone 2 Complete (Alpha Release):**

- Multi-format log parsing (JSON, logfmt, syslog, regex)
- Network protocol dissection (HTTP/1.1, DNS, TCP/UDP)
- Filter expressions (field-based, regex, boolean logic)
- Basic query language (WHERE filtering implemented)
- Packet capture via libpcap
- JSON output (base output type)
- Additional output types (file, HTTP, metrics) - implemented
- Configuration file support (YAML) - basic implementation
- Unit tests (>70% coverage for core parsers)
- Documentation site - basic HTML site created
- Filter engine with real-time evaluation

**🚧 In Progress:**

- Integration tests
- CLI tool fixes (naming conflicts)
- Advanced correlation patterns

**📋 Planned (Milestone 3+):**

- Advanced correlation (ID-based, pattern-based)
- Export to alerting systems (webhooks, HTTP endpoints)
- Prometheus/OpenTelemetry export
- Export to centralized systems (Kafka, NATS)
- Language bindings (Python, Go, Rust)

See [SPEC.md](SPEC.md) for complete specification and roadmap.

## Quick Start

### Prerequisites

- C11 compiler (GCC 4.9+, Clang 3.3+, MSVC 2015+)
- CMake 3.10+
- Git

#### Debian/Ubuntu Development Packages

On Debian-based systems, install the required development packages:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  pkg-config \
  libpcap-dev \
  libuv1-dev \
  libjansson-dev \
  libpcre2-dev \
  libcurl4-openssl-dev \
  libyaml-dev \
  check
```

### Building

```bash
git clone https://github.com/dajobe/nblex.git
cd nblex
mkdir build && cd build
cmake ..
make
```

### Running

```bash
# Check version
./nblex --version

# View help
./nblex --help

# Monitor logs
./nblex --logs /var/log/app.log --output json

# Monitor network traffic
sudo ./nblex --network eth0 --output json

# Monitor both logs and network (with automatic correlation)
sudo ./nblex --logs /var/log/app.log --network eth0 --output json
```

**Note:** Network capture requires root privileges or `CAP_NET_RAW` capability. The tool automatically correlates log and network events occurring within ±100ms of each other.

## Example Usage (Planned)

### Command Line

```bash
# Monitor nginx logs and correlate with network traffic
nblex monitor \
  --logs /var/log/nginx/access.log \
  --network eth0 \
  --filter 'log.status >= 500 OR network.tcp.retransmits > 0' \
  --output json

# Real-time query (using nQL)
nblex query "aggregate count(), avg(network.latency_ms) by log.endpoint where log.level == ERROR window 1m"

# Load from config file
nblex monitor --config /etc/nblex/config.yaml
```

### Configuration File

```yaml
# nblex.yaml
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
      filter: "tcp port 80 or tcp port 443"

correlation:
  enabled: true
  strategies:
    - type: time_based
      window: 100ms

outputs:
  - name: stdout
    type: stdout
    format: json

  - name: prometheus
    type: prometheus
    listen: :9090

  - name: webhook
    type: http
    url: https://alerting.example.com/webhook
    method: POST
```

### C Library API

```c
#include <nblex/nblex.h>

// Initialize
nblex_world* world = nblex_world_new();
nblex_world_open(world);

// Add log input
nblex_input* log_input = nblex_input_file_new(world, "/var/log/app.log");
nblex_input_set_format(log_input, NBLEX_FORMAT_JSON);

// Add network input
nblex_input* net_input = nblex_input_pcap_new(world, "eth0");

// Enable correlation
nblex_correlation* corr = nblex_correlation_new(world);
nblex_correlation_add_strategy(corr, NBLEX_CORR_TIME_BASED, 100);

// Set event handler
void on_event(nblex_event* event, void* user_data) {
    if (nblex_event_get_type(event) == NBLEX_EVENT_CORRELATION) {
        printf("Correlated: %s\n", nblex_event_to_json(event));
    }
}
nblex_set_event_handler(world, on_event, NULL);

// Start monitoring
nblex_world_start(world);
nblex_world_run(world);  // Blocks

// Cleanup
nblex_world_free(world);
```

## Use Cases

### 1. Debugging Production Issues

Correlate application errors with network timeouts, retransmissions, or connection resets.

### 2. Security Debugging

Investigate potential security incidents by correlating SSH logins with large outbound transfers during incident response.

### 3. Performance Analysis

Find slow API endpoints by measuring actual network latency, not just application-reported times.

### 4. Compliance & Audit

Track database queries with full network context for comprehensive audit trails.

### 5. Microservices Tracing

Follow requests across services by correlating logs and network flows with trace IDs.

## Architecture

```text
┌─────────────────────────────────────────────────────────┐
│                     nblex Core                          │
│                                                         │
│  Inputs → Stream Processor → Correlation → Outputs     │
│  (logs,   (parse, filter,    (time, ID,    (json,     │
│   pcap)    aggregate)         pattern)      exports)  │
└─────────────────────────────────────────────────────────┘
```

**Key Components:**

- **Input Layer** - Log files, syslog, pcap, eBPF
- **Stream Processor** - Parse, filter, normalize events
- **Correlation Engine** - Match events across layers
- **Output Layer** - JSON, metrics, alerts, storage

See [SPEC.md](SPEC.md) for detailed architecture.

## Performance Targets

- **Throughput:** 100,000 log events/sec, 10 Gbps network traffic
- **Latency:** <10ms end-to-end processing (p95)
- **Memory:** <100MB baseline
- **Correlation:** <5ms matching latency

## Development

### Project Structure

```text
nblex/
├── include/nblex/     # Public API headers
├── src/
│   ├── core/         # Event loop and world management
│   ├── input/        # Log and network input handlers
│   ├── parsers/      # Log format and protocol parsers
│   ├── correlation/  # Correlation engine
│   ├── output/       # Output handlers
│   └── util/         # Utilities
├── tests/            # Unit, integration, fuzz, benchmarks
├── examples/         # Example programs
├── cli/              # Command-line tool
└── docs/             # Documentation
```

### Building with Options

```bash
# Debug build with sanitizers
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DNBLEX_ENABLE_ASAN=ON \
      -DNBLEX_ENABLE_UBSAN=ON ..

# Release build
cmake -DCMAKE_BUILD_TYPE=Release ..

# Without tests or examples
cmake -DNBLEX_BUILD_TESTS=OFF \
      -DNBLEX_BUILD_EXAMPLES=OFF ..
```

### Running Tests

```bash
cd build
make test
```

### Contributing

We welcome contributions! See [CONTRIBUTING.md](CONTRIBUTING.md) for:

- Development setup
- Coding standards
- Testing requirements
- Pull request process

## Documentation

- **[SPEC.md](SPEC.md)** - Complete specification, architecture, and roadmap
- **[CONTRIBUTING.md](CONTRIBUTING.md)** - Development guidelines
- **API Reference** - Generated from headers (coming soon)

## Comparison with Existing Tools

| Feature | nblex | Splunk | ELK | Wireshark | Datadog |
|---------|-------|--------|-----|-----------|---------|
| Log analysis | ✅ | ✅ | ✅ | ❌ | ✅ |
| Network monitoring | ✅ | ⚠️ | ❌ | ✅ | ✅ |
| **Automatic correlation** | ✅ | ❌ | ❌ | ❌ | ⚠️ |
| Real-time streaming | ✅ | ✅ | ✅ | ⚠️ | ✅ |
| Open source | ✅ | ❌ | ✅ | ✅ | ❌ |
| Self-hosted | ✅ | ⚠️ | ✅ | ✅ | ❌ |

**Key differentiator:** nblex automatically correlates logs with network events - others require manual dashboard building.

## Roadmap

### Phase 1: Foundation (Months 1-2) ✅ COMPLETE

- [x] Project structure
- [x] Build system
- [x] Public API
- [x] Event loop (libuv)
- [x] Basic log parsing (JSON)
- [x] Basic pcap capture
- [x] HTTP/1.1 parsing
- [x] Time-based correlation

### Phase 2: Alpha (Months 3-4) ✅ COMPLETE

- [x] Multi-format log parsing (JSON, logfmt, syslog, regex)
- [x] HTTP/DNS/TCP dissection
- [x] Filter expressions (field-based, regex, boolean)
- [x] Query language basics (SELECT, WHERE, GROUP BY)
- [x] Multiple outputs (JSON, file, HTTP planned)
- [x] Unit tests (>70% coverage for core components)

### Phase 3: Beta (Months 5-6)

- [ ] Advanced correlation (ID-based, sequences)
- [ ] Export to alerting systems (webhooks, HTTP endpoints)
- [ ] Prometheus export
- [ ] Performance optimizations
- [ ] Integration tests
- [ ] Docker images

### Phase 4: v1.0 (Months 7-9)

- [ ] eBPF capture
- [ ] Optional simple viewer for correlation visualization
- [ ] Export to centralized systems (Kafka, NATS)
- [ ] Export to storage systems (Elasticsearch, Loki, ClickHouse)
- [ ] Language bindings
- [ ] Production debugging deployments

See [SPEC.md](SPEC.md) for detailed milestones.

## License

Licensed under the Apache License, Version 2.0. See [LICENSE-2.0.txt](LICENSE-2.0.txt) for details.

**Dependency Licenses:**

- libpcap - BSD 3-Clause
- libuv - MIT
- libjansson - MIT
- PCRE2 - BSD 3-Clause

All dependencies use permissive licenses compatible with Apache 2.0.

## Authors

- David Beckett - Original nblex UTF-8 decoder (2013)
- nblex 2.0 reimagined as Network & Buffer Log EXplorer (2025)

## Support

- **Issues:** [GitHub Issues](https://github.com/dajobe/nblex/issues)
- **Discussions:** [GitHub Discussions](https://github.com/dajobe/nblex/discussions)
- **Specification:** [SPEC.md](SPEC.md)

## Acknowledgments

Inspired by:

- [Zeek Network Monitor](https://zeek.org/) - Network analysis concepts
- [Vector](https://vector.dev/) - High-performance log routing
- [Sysdig](https://sysdig.com/) - System call + network correlation

---

**Current development focus:** Implementing core event loop and basic log/network inputs. See [SPEC.md](SPEC.md) for architecture details and [CONTRIBUTING.md](CONTRIBUTING.md) to get involved!
