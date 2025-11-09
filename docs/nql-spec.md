# nQL (nblex Query Language) Specification

**Version:** 1.0  
**Status:** Draft  
**Last Updated:** 2025-11-08

## Overview

nQL (nblex Query Language) is a lightweight, filter-first query language designed specifically for nblex's streaming log and network correlation use case. Unlike SQL, nQL is optimized for real-time event processing and correlation, with a simpler syntax that builds on the existing filter engine.

## Design Principles

1. **Filter-first**: Filters are the primary operation (inspired by LogQL)
2. **Pipeline-based**: Operations chain naturally with `|` (inspired by PromQL)
3. **Correlation-native**: Built-in correlation syntax unique to nblex
4. **Minimal overhead**: Simple parser, easy to implement in C
5. **Leverages existing**: Reuses current filter expression syntax

## Core Syntax

### Grammar

```text
query := filter | correlation | aggregation | show | pipeline

filter := expression
correlation := 'correlate' filter 'with' filter ['within' duration]
aggregation := 'aggregate' func_list ['by' field_list] ['where' filter] ['window' window_spec]
show := 'show' field_list ['where' filter]
pipeline := query '|' query

func_list := func | func ',' func_list
func := func_name '(' [field] [',' percentile] ')'
func_name := 'count' | 'sum' | 'avg' | 'min' | 'max' | 'percentile' | 'distinct'

field_list := field | field ',' field_list
field := identifier ('.' identifier)*

window_spec := duration | 'tumbling' '(' duration ')' | 'sliding' '(' duration ',' duration ')'
duration := number unit
unit := 'ms' | 's' | 'm' | 'h'
```

## Query Types

### 1. Simple Filtering

Simple filters reuse the existing filter engine syntax. Filters can be used standalone or as part of other query types.

**Syntax:**

```bash
filter_expression
```

**Examples:**

```bash
# Basic equality
log.level == ERROR

# Comparison operators
log.status >= 500
network.latency_ms < 100

# Boolean logic
log.level >= WARN AND network.port == 443
log.level == ERROR OR log.level == FATAL

# Regex matching
log.message =~ /timeout/i
log.path !~ /^\/api\//

# Field existence
log.user != null
network.dst_port in (80, 443, 8080)
```

**Operators:**

- `==` - Equality
- `!=` - Inequality
- `<` - Less than
- `<=` - Less than or equal
- `>` - Greater than
- `>=` - Greater than or equal
- `=~` - Regex match
- `!~` - Regex not match
- `in` - Value in list
- `contains` - String contains substring
- `AND` - Logical AND
- `OR` - Logical OR
- `NOT` - Logical NOT

### 2. Correlation Queries

Correlation queries match events across different sources (logs and network) within a time window.

**Syntax:**

```bash
correlate left_filter with right_filter [within duration]
```

**Examples:**

```bash
# Time-based correlation
correlate log.level == ERROR with network.dst_port == 3306 within 100ms

# Correlation with multiple conditions
correlate log.status >= 500 with network.tcp.retransmits > 0 within 1s

# Default time window (100ms if not specified)
correlate log.level == ERROR with network.dst_port == 3306
```

**Behavior:**

- Matches events where both filters are true within the specified time window
- Default time window is 100ms if `within` clause is omitted
- Time window can be specified in milliseconds (`ms`), seconds (`s`), minutes (`m`), or hours (`h`)

### 3. Aggregation Queries

Aggregation queries compute statistics over groups of events, optionally within time windows.

**Syntax:**

```bash
aggregate func_list [by field_list] [where filter] [window window_spec]
```

**Examples:**

```bash
# Simple count
aggregate count() where log.level == ERROR

# Group by field
aggregate count() by log.service where log.level == ERROR

# Multiple aggregation functions
aggregate count(), avg(network.latency_ms) by log.endpoint

# With time window
aggregate count() by log.endpoint window 1m

# Tumbling window
aggregate count() by log.endpoint window tumbling(1m)

# Sliding window
aggregate count() by log.endpoint window sliding(1m, 10s)

# Percentile aggregation
aggregate percentile(network.latency_ms, 95) by log.endpoint window 1m
```

**Aggregation Functions:**

- `count()` - Count number of events
- `sum(field)` - Sum of numeric field values
- `avg(field)` - Average of numeric field values
- `min(field)` - Minimum value
- `max(field)` - Maximum value
- `percentile(field, p)` - Percentile value (p = 0-100)
- `distinct(field)` - Count of distinct values

**Window Types:**

- `window duration` - Simple time window (defaults to tumbling)
- `window tumbling(duration)` - Tumbling window (non-overlapping)
- `window sliding(duration, slide)` - Sliding window (overlapping)

### 4. Field Selection (Show)

Select specific fields from matching events.

**Syntax:**

```bash
show field_list [where filter]
```

**Examples:**

```bash
# Select specific fields
show log.service, log.message, network.latency_ms where log.level == ERROR

# Select all fields (implicit)
* where log.level == ERROR
```

### 5. Pipeline Operations

Chain multiple operations together using the pipe operator `|`.

**Syntax:**

```bash
query | query
```

**Examples:**

```bash
# Filter then aggregate
log.level == ERROR | aggregate count() by log.service

# Correlate then aggregate
correlate log.level == ERROR with network.retransmits > 0 within 100ms | aggregate count()

# Multiple stages
log.method == POST AND network.http.status == 200 |
aggregate avg(network.latency_ms) by log.endpoint window 1m |
top(10) by avg(network.latency_ms)
```

**Pipeline Behavior:**

- Left query filters/processes events
- Right query operates on results from left query
- Can chain multiple operations: `query | query | query`

## Time Windows

Time windows define time ranges for aggregating or correlating events.

### Duration Syntax

```bash
100ms    # 100 milliseconds
1s       # 1 second
5m       # 5 minutes
1h       # 1 hour
```

### Window Types

**Tumbling Windows:**

- Non-overlapping time windows
- Each event belongs to exactly one window
- Syntax: `window tumbling(duration)` or `window duration`

**Sliding Windows:**

- Overlapping time windows
- Events can belong to multiple windows
- Syntax: `window sliding(window_size, slide_interval)`

**Example:**

```bash
# Tumbling window: 1-minute windows, no overlap
aggregate count() window tumbling(1m)

# Sliding window: 1-minute windows, sliding every 10 seconds
aggregate count() window sliding(1m, 10s)
```

## Field References

Fields are referenced using dot notation to access nested properties.

**Examples:**

```bash
log.level                    # Top-level log field
log.service.name            # Nested field
network.http.header.x-request-id  # Deeply nested field
network.tcp.retransmits     # Network protocol field
```

**Field Naming:**

- Fields can contain letters, numbers, underscores, and dots
- Field names are case-sensitive
- Use quotes for field names with special characters (future)

## Comparison with SQL

| SQL                             | nQL                          | Notes                                 |
|:--------------------------------|:-----------------------------|:--------------------------------------|
| `SELECT * FROM events WHERE ...` | `* where ...`                | No FROM needed (always events)        |
| `SELECT field WHERE ...`        | `show field where ...`       | Explicit field selection              |
| `GROUP BY field`                | `aggregate ... by field`     | Aggregation syntax                    |
| `WINDOW ...`                    | `window ...`                 | Simpler window syntax                 |
| `CORRELATE ... WITH ...`        | `correlate ... with ...`     | Native correlation                    |
| Pipeline operator               | pipe character               | Chaining operations                   |
| `COUNT(*)`                      | `count()`                    | Simpler function syntax               |
| `AVG(field)`                    | `avg(field)`                 | Lowercase function names              |

## Complete Examples

### Example 1: Error Monitoring

**Goal:** Count errors by service in 1-minute windows

**nQL:**

```bash
aggregate count() by log.service where log.level == ERROR window 1m
```

**SQL Equivalent:**

```sql
SELECT log.service, COUNT(*) as errors
FROM events
WHERE log.level == ERROR
GROUP BY log.service
WINDOW tumbling(1 minute)
```

### Example 2: Correlation

**Goal:** Find database errors correlated with network issues

**nQL:**

```bash
correlate log.level == ERROR with network.dst_port == 3306 within 1s
```

**SQL Equivalent:**

```sql
CORRELATE
  log.level == ERROR
  WITH network.dst_port == 3306
WITHIN 1 second
```

### Example 3: Performance Analysis

**Goal:** Find top 10 slowest endpoints by P95 latency

**nQL:**

```bash
log.method == POST AND network.http.status == 200 |
aggregate count(), percentile(network.latency_ms, 95) by log.endpoint window 1m |
top(10) by percentile(network.latency_ms, 95)
```

**SQL Equivalent:**

```sql
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
```

### Example 4: Security Investigation

**Goal:** Find SSH logins followed by large data transfers

**nQL:**

```bash
correlate log.event == "ssh_login_success" with network.bytes_sent > 104857600 within 5m
```

### Example 5: Database Audit

**Goal:** Track database queries with network context

**nQL:**

```bash
correlate log.query_type == "SELECT" with network.dst_ip |
show log.user, log.table, network.src_ip, network.bytes_transferred
```

## Implementation Status

### Phase 1: Parser Foundation ✅

- [x] Recursive descent parser (`src/parsers/nql_parser.c`)
- [x] AST nodes for filters, correlations, aggregations
- [x] Integration with existing filter engine

### Phase 2: Query Execution ✅

- [x] Basic query executor (`src/core/nql_executor.c`)
- [x] Filter evaluation
- [x] Correlation matching (basic)
- [ ] Full aggregation implementation
- [ ] Window state management

### Query Result Delivery

- ✅ Decision: Emit derived nQL results (aggregates, correlations, pipeline outputs) as synthetic `nblex_event` instances dispatched through the existing event handler chain.
- ▶ Consequences:
  - Requires a documented payload schema so outputs can distinguish derived vs raw events.
  - Executor must maintain per-query state and emit events when windows close or correlations succeed.
  - Back-pressure and buffering need to be considered to avoid flooding downstream handlers.

## Test Coverage

### Parser Tests – Current Gaps

- Aggregation functions beyond `count()`/`avg()` (`sum`, `min`, `max`, `percentile`, `distinct`)
- Window syntaxes not covered: `window 1m` shorthand and `window tumbling(...)`
- Correlation error handling other than missing `with`
- Complex pipelines (three-or-more stages, mixed query types)
- Successful `nql_parse_ex` usage (only error path validated)

### Execution Tests – Current Gaps

- Correlation behaviour in `nql_execute`
- Aggregation semantics beyond WHERE filtering (state, grouping, windowing)
- Aggregations without a WHERE clause
- Multi-stage pipelines beyond `filter | show`
- Edge cases: empty events, missing fields, or failing stages

### Phase 3: Windowing (Planned)

- [ ] Time-based windows (tumbling, sliding)
- [ ] Window state management
- [ ] Event buffering for windows
- [ ] Window expiration and cleanup

### Phase 4: Advanced Features (Planned)

- [x] Pipeline operator (`|`)
- [x] Field selection (`show`)
- [ ] Ordering and limits (`top`, `order by`)
- [ ] ID-based correlation
- [ ] Sequence detection

## CLI Usage

### Basic Usage

```bash
# Simple filter query
nblex --logs /var/log/app.log --query 'log.level == ERROR'

# Correlation query
nblex --logs /var/log/app.log --network eth0 \
  --query 'correlate log.level == ERROR with network.dst_port == 3306 within 1s'

# Aggregation query
nblex --logs /var/log/app.log \
  --query 'aggregate count() by log.service where log.level == ERROR window 1m'
```

### With Configuration File

```yaml
# config.yaml
queries:
  - name: error_monitoring
    query: aggregate count() by log.service where log.level == ERROR window 1m
    outputs:
      - type: prometheus
        port: 9090
```

## API Usage

### C API

```c
#include <nblex/nblex.h>

// Parse and execute query
nql_query_t* query = nql_parse("log.level == ERROR");
if (query) {
    int matches = nql_execute("log.level == ERROR", event, world);
    nql_free(query);
}
```

## Future Enhancements

1. **ID-based Correlation**: Match events by request ID, trace ID, etc.

   ```bash
   correlate log.request_id == network.http.header.x-request-id
   ```

2. **Sequence Detection**: Detect event patterns over time

   ```bash
   sequence log.event == "login" followed by log.event == "logout" within 1h
   ```

3. **Anomaly Detection**: ML-based pattern detection

   ```bash
   detect anomaly in network.bytes_sent window 5m
   ```

4. **Subqueries**: Nested query support

   ```bash
   aggregate count() by log.service where (
     log.level == ERROR AND network.retransmits > 0
   )
   ```

5. **Functions**: User-defined functions and transformations

   ```bash
   aggregate count() by extract(log.path, '/api/(.*)') where log.method == POST
   ```

## References

- [SPEC.md](../SPEC.md) - Complete nblex specification
- [README.md](../README.md) - Project overview and quick start
- [CONTRIBUTING.md](../CONTRIBUTING.md) - Development guidelines

## License

This specification is part of the nblex project, licensed under the Apache License, Version 2.0.
