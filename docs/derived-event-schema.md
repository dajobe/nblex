# Derived Event Payload Schema

## Overview

nQL queries can produce **derived events** - synthetic events that represent query results (aggregations, correlations, etc.) rather than raw input events. These derived events are emitted as `nblex_event` instances and flow through the same output pipeline as regular events.

## Event Structure

All derived events follow the standard `nblex_event` structure:

```c
typedef struct {
    nblex_event_type type;      // Event type (LOG, NETWORK, CORRELATION, etc.)
    uint64_t timestamp_ns;      // Timestamp in nanoseconds
    nblex_input* input;          // NULL for derived events (no source input)
    json_t* data;                // JSON payload (see schemas below)
} nblex_event;
```

## Derived Event Types

### 1. Aggregation Result Events

**Event Type:** `NBLEX_EVENT_LOG`  
**Result Type:** `"aggregation"`

Aggregation queries produce result events containing computed metrics grouped by specified fields.

#### Schema

```json
{
  "nql_result_type": "aggregation",
  "group": {
    "field1": "value1",
    "field2": "value2"
  },
  "metrics": {
    "count": 42,
    "avg_latency_ms": 123.45,
    "min_latency_ms": 10.0,
    "max_latency_ms": 500.0,
    "p95_latency_ms": 250.0,
    "distinct_user_id": 10
  },
  "window": {
    "start_ns": 1234567890000000000,
    "end_ns": 1234567891000000000
  }
}
```

#### Fields

- **`nql_result_type`** (string, required): Always `"aggregation"` for aggregation results
- **`group`** (object, optional): Group key values when `GROUP BY` is used. Keys are field names, values are string representations of the grouped values. Omitted if no `GROUP BY` clause.
- **`metrics`** (object, required): Computed aggregation metrics:
  - **`count`** (integer): Number of events aggregated (always present)
  - **`avg_<field>`** (number): Average of numeric field (when `avg()` is used)
  - **`min_<field>`** (number): Minimum value (when `min()` is used)
  - **`max_<field>`** (number): Maximum value (when `max()` is used)
  - **`p<percentile>_<field>`** (number): Percentile value (when `percentile()` is used, e.g., `p95_latency_ms`)
  - **`distinct_<field>`** (integer): Distinct count (when `distinct()` is used)
- **`window`** (object, optional): Window timing information:
  - **`start_ns`** (integer): Window start timestamp in nanoseconds
  - **`end_ns`** (integer): Window end timestamp in nanoseconds
  - Omitted if no windowing is used (`window NONE`)

#### Examples

**Simple count aggregation:**

```json
{
  "nql_result_type": "aggregation",
  "metrics": {
    "count": 100
  }
}
```

**Grouped aggregation with window:**

```json
{
  "nql_result_type": "aggregation",
  "group": {
    "log.service": "api",
    "log.status": "500"
  },
  "metrics": {
    "count": 5,
    "avg_latency_ms": 250.5,
    "p95_latency_ms": 450.0
  },
  "window": {
    "start_ns": 1234567890000000000,
    "end_ns": 1234567896000000000
  }
}
```

### 2. Correlation Result Events

**Event Type:** `NBLEX_EVENT_CORRELATION`  
**Result Type:** `"correlation"`

Correlation queries produce result events when matching events are found within the specified time window.

#### Correlation Schema

```json
{
  "nql_result_type": "correlation",
  "window_ms": 100,
  "left_event": {
    "log.level": "ERROR",
    "log.message": "Database connection failed"
  },
  "right_event": {
    "network.dst_port": 3306,
    "network.tcp.flags": "RST"
  },
  "time_diff_ms": 45.2
}
```

#### Correlation Fields

- **`nql_result_type`** (string, required): Always `"correlation"` for correlation results
- **`window_ms`** (integer, required): Correlation window size in milliseconds
- **`left_event`** (object, required): JSON data from the left-side matching event (from `correlate ... with`)
- **`right_event`** (object, required): JSON data from the right-side matching event (the `with ...` side)
- **`time_diff_ms`** (number, required): Time difference between events in milliseconds (positive if left_event is later, negative if right_event is later)

#### Correlation Examples

**Time-based correlation:**

```json
{
  "nql_result_type": "correlation",
  "window_ms": 100,
  "left_event": {
    "log.level": "ERROR",
    "log.service": "api"
  },
  "right_event": {
    "network.dst_port": 3306,
    "network.tcp.flags": "RST"
  },
  "time_diff_ms": 50.0
}
```

## Event Flow

Derived events follow the same flow as regular events:

1. **Creation**: Query executor creates derived event with appropriate schema
2. **Emission**: Event is emitted via `nblex_event_emit()`
3. **Filtering**: Event passes through input filters (if any)
4. **Correlation**: Event can be processed by correlation engine (if enabled)
5. **Output**: Event is formatted and delivered to configured outputs

## Output Formatting

Output formatters should recognize `nql_result_type` field to provide appropriate formatting:

- **JSON output**: Includes full schema as shown above
- **Metrics output**: Extracts metrics from `metrics` object for Prometheus/StatsD
- **File output**: Serializes as JSON lines
- **HTTP output**: POSTs JSON payload to configured endpoint

## Timestamps

- **Aggregation events**: Timestamp is set to the window end time (or current time for non-windowed)
- **Correlation events**: Timestamp is set to the later of the two correlated events

## Source Information

Derived events have `input == NULL` since they don't originate from a specific input source. Output formatters should handle this gracefully.

## Versioning

This schema is version 1.0. Future versions may add fields but will maintain backward compatibility for existing fields.
