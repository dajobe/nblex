# nblex Agents Architecture

## What is nblex?

nblex (Network & Buffer Log EXplorer) is an observability platform that correlates application logs with network traffic in real-time. It uniquely combines log analysis with network monitoring to provide insights that traditional tools operating in isolation cannot.

## Architecture Overview

nblex uses an **agent-based architecture** where specialized components work together in a data pipeline:

```text
Input Agents → Processing Agents → Output Agents
     ↓              ↓                  ↓
  Events →    Correlation →      Formatted Output
```

### Core Components

- **nblex World** - Central orchestration layer that manages all agents and the event loop
- **Input Agents** - Collect data from various sources (log files, network interfaces, syslog, etc.)
- **Processing Agents** - Parse, filter, and correlate events from different sources
- **Output Agents** - Format and deliver processed events to destinations (JSON, metrics, alerts, etc.)

## Data Flow

1. **Input agents** monitor data sources and emit events when new data arrives
2. **Processing agents** transform events (parsing formats, filtering, extracting fields)
3. **Correlation engine** links related events across different sources (e.g., matching log errors with network timeouts)
4. **Output agents** format and deliver correlated events to various destinations

## Agent Types

### Input Agents (`src/input/`)

Collect data from sources and convert them into standardized events. Examples include file tailing, packet capture, syslog reception, and socket listeners.

### Processing Agents (`src/parsers/`, `src/correlation/`)

Transform events through parsing, filtering, and correlation. The correlation engine is nblex's key differentiator - it automatically links related events across application logs and network traffic.

### Output Agents (`src/output/`)

Format and deliver events to destinations like JSON files, metrics systems (Prometheus), alerting systems, or HTTP endpoints.

## Project Structure

- `src/core/` - Core orchestration and event handling
- `src/input/` - Input agent implementations
- `src/parsers/` - Log and protocol parsers
- `src/correlation/` - Correlation engine (planned)
- `src/output/` - Output formatters
- `include/nblex/` - Public API headers
- `src/nblex_internal.h` - Internal structures and APIs

## Key Concepts

- **Events** - Standardized data structures representing logs, network packets, or correlations
- **Event Loop** - Single-threaded async I/O using libuv
- **Virtual Tables** - Polymorphic behavior pattern for input agents
- **World** - Central context that manages all agents and routes events

## Current Status

The project is under active development. Core infrastructure is in place, with file input and JSON parsing implemented. Network capture, correlation engine, and additional output formats are planned.

## Where to Learn More

- **[README.md](README.md)** - Project overview, use cases, and quick start guide
- **[SPEC.md](SPEC.md)** - Complete specification, architecture details, and roadmap
- **[CONTRIBUTING.md](CONTRIBUTING.md)** - Development guidelines and coding standards
- `include/nblex/nblex.h` - Public API reference
- `src/nblex_internal.h` - Internal implementation details

## For Coding Agents

When working on nblex:

1. **Start with the architecture** - Understand how agents interact through the event system
2. **Review existing implementations** - See `src/input/file_input.c` for an example input agent
3. **Check the API** - Public API is in `include/nblex/nblex.h`, internals in `src/nblex_internal.h`
4. **Follow patterns** - Input agents use virtual tables, events flow through the world
5. **Read the spec** - [SPEC.md](SPEC.md) contains detailed requirements and design decisions
