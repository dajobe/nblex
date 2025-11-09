# nblex Strategy & Planning Document

**Last Updated:** 2025-11-08  
**Purpose:** Strategic guidance for nblex development, positioning, and feature prioritization.  
**Status:** Living document - update as strategy evolves

---

## Executive Summary

nblex is positioned as a **lightweight, focused correlation tool** for developers and SREs who need to quickly correlate application logs with network traffic. It complements existing observability platforms rather than competing with them.

**Core Strategy:** Stay lightweight, stay focused, stay simple.

---

## Strategic Positioning

### Unique Value Proposition

1. **Lightweight & Focused** - Single binary (<100MB), minimal dependencies, does one thing well
2. **Automatic Correlation** - Built-in correlation engine vs. manual integration
3. **Easy to Run** - No complex setup, works out of the box, perfect for ad-hoc debugging
4. **Developer/SRE Focus** - Observability vs. security-only tools
5. **Real-time Streaming** - Sub-second latency vs. batch processing

### Market Gaps nblex Fills

- **No lightweight correlation tool** - Existing solutions are heavy platforms, not focused tools
- **Complex setup barriers** - Developers need something they can run immediately
- **No unified log+network correlation** - Most solutions require integration
- **Security bias** - Most tools target security teams, not developers debugging issues
- **Manual correlation** - Requires dashboard building or scripting
- **Overkill for ad-hoc use** - Heavy tools are designed for 24/7 monitoring, not quick debugging

### Competitive Threats

1. **Zeek + ELK combinations** - Entrenched in many organizations
2. **Established tools** - Hard to displace existing infrastructure
3. **Performance claims** - Must deliver on 100K events/sec promise
4. **Correlation accuracy** - Must prove correlation quality vs. manual methods

---

## Positioning Strategy

### Core Principles

1. **Lightweight & Focused** - Position as a simple tool that does one thing well, not a platform
2. **Quick to Start** - Emphasize "running in 60 seconds" vs. "setup in hours"
3. **Developer-first** - Target developers/SREs debugging issues, not security teams
4. **Complement, don't compete** - Works WITH existing tools, doesn't replace them
5. **Ad-hoc Friendly** - Perfect for debugging sessions, not just 24/7 monitoring
6. **Low Friction** - Minimal dependencies, no complex configuration

### Target Use Cases (✅ Do This)

- ✅ **Debugging production issues** - "Something broke, let me correlate logs and network"
- ✅ **Ad-hoc analysis** - "Is this error related to network timeouts?"
- ✅ **Quick investigations** - "What happened during this incident?"
- ✅ **Development environments** - Lightweight enough to run locally
- ✅ **On-demand correlation** - Run when needed, not always-on monitoring

### Out of Scope (❌ Don't Do This)

- ❌ **Long-term storage** - Use ELK for that
- ❌ **Full security platform** - Use Security Onion for that
- ❌ **Enterprise dashboards** - Use Grafana/Kibana for that
- ❌ **24/7 monitoring platforms** - Use existing monitoring stacks for that
- ❌ **Log aggregation** - Use Vector/Fluentd for that
- ❌ **Full SIEM** - Use dedicated security tools for that

### nblex's Sweet Spot

**Ideal Scenario:**

```text
Developer: "Something broke in production. Let me quickly correlate 
the error logs with network traffic to see if it's a network issue."

nblex: < 1 minute to download, install, and start correlating
```

**Not Ideal Scenario:**

```text
Team: "We need a full observability platform with dashboards, 
alerting, long-term storage, and enterprise features."

nblex: Not the right tool - use ELK/Grafana/Prometheus stack
```

---

## Integration Strategy

### Complement Existing Tools

- **Export to ELK** - Don't replace, complement. Export correlated events to Elasticsearch
- **Prometheus metrics** - Integrate with existing monitoring infrastructure
- **Kafka/NATS** - Stream to existing event pipelines
- **REST API** - Allow integration with existing tools and workflows
- **JSON output** - Standard format for easy integration

### Integration Principles

1. **Don't duplicate** - If ELK does storage well, export to ELK. Don't build storage.
2. **Don't compete** - If Grafana does dashboards well, export metrics. Don't build dashboards.
3. **Stay focused** - nblex does correlation. Let other tools do what they do best.
4. **Easy integration** - Make it trivial to pipe nblex output into existing workflows

---

## Feature Priorities

### Priority 1: Core Value (Must Have)

1. **Ease of use** - Simple CLI, sensible defaults, works out of the box
2. **Correlation quality** - Must be accurate and useful
3. **Lightweight footprint** - Keep dependencies minimal, memory usage low
4. **Quick start** - Running in under 1 minute from download

### Priority 2: Developer Experience (Should Have)

1. **Clear output** - Human-readable and JSON formats
2. **Good documentation** - Examples, quick start guide, common use cases
3. **Fast feedback** - Real-time correlation, low latency
4. **Error handling** - Clear error messages, graceful degradation

### Priority 3: Integration (Nice to Have)

1. **Export formats** - JSON, CSV, Prometheus metrics
2. **APIs** - REST API for programmatic access
3. **Streaming** - Kafka/NATS output
4. **Filtering** - Basic filtering to reduce noise

### Priority 4: Advanced Features (Future)

1. **Query language** - Simple query syntax for filtering
2. **Configuration file** - YAML config for complex setups
3. **Multiple inputs** - Multiple log files, multiple interfaces
4. **Advanced correlation** - ID-based, pattern-based correlation

### Feature Creep Prevention

**Questions to Ask Before Adding Features:**

1. **Does this make nblex heavier?** - If yes, consider making it optional or external
2. **Does this belong in another tool?** - If yes, integrate with that tool instead
3. **Does this add complexity?** - If yes, is the value worth it?
4. **Does this help with correlation?** - If no, it's probably out of scope
5. **Can developers use this in 60 seconds?** - If no, simplify or remove

**Red Flags (Don't Add These):**

- ❌ Long-term storage/retention
- ❌ Complex dashboards/UI
- ❌ Alerting systems
- ❌ User management/authentication
- ❌ Multi-tenancy
- ❌ Enterprise SSO/RBAC
- ❌ Full query language (keep it simple)
- ❌ Plugin system (adds complexity)

---

## Success Factors

### Critical Success Criteria

1. **Stay focused** - Don't bloat with features that belong in other tools
2. **Keep it simple** - Easy to install, easy to run, easy to understand
3. **Prove correlation value** - Show accurate, useful correlations quickly
4. **Maintain lightweight nature** - Resist feature creep that adds complexity
5. **Developer experience** - Fast feedback, clear output, good examples

### Success Metrics

**Technical Metrics:**

- Setup time: < 1 minute from download to running
- Memory usage: < 100MB idle
- Binary size: < 10MB
- Dependencies: Only libuv and libpcap
- Correlation accuracy: > 90% for time-based correlation

**Adoption Metrics:**

- GitHub stars: Target 1K+ in first year
- Downloads: Track release downloads
- Usage: Track active users (anonymized)
- Community: Active discussions, contributions

**Quality Metrics:**

- Documentation: Clear, examples, quick start guide
- Error handling: Graceful failures, clear messages
- Performance: Meets 100K events/sec target
- Stability: No crashes, handles edge cases

---

## Competitive Advantages

### vs. ELK Stack

**nblex Advantages:**

- ✅ Lightweight (<100MB vs. GB+)
- ✅ Quick setup (<1 min vs. hours)
- ✅ Automatic correlation vs. manual dashboards
- ✅ Native network capture

**ELK Advantages (We Don't Compete):**

- Long-term storage
- Advanced search
- Rich dashboards
- Enterprise features

**Strategy:** Export to ELK, don't replace it.

### vs. Zeek

**nblex Advantages:**

- ✅ Unified log + network correlation
- ✅ Developer/SRE focus vs. security focus
- ✅ Simpler deployment
- ✅ Application log handling

**Zeek Advantages (We Don't Compete):**

- Deep protocol analysis
- Extensive dissectors
- Security-focused features

**Strategy:** Complement Zeek, don't compete.

### vs. Security Onion

**nblex Advantages:**

- ✅ Lightweight single binary vs. full OS distro
- ✅ Developer/SRE focus
- ✅ Easier deployment
- ✅ Ad-hoc use case

**Security Onion Advantages (We Don't Compete):**

- Full security platform
- Pre-integrated tools
- SOC features

**Strategy:** Different use case entirely.

---

## Development Roadmap Principles

### Phase 1: MVP (Months 1-2)

**Goal:** Prove lightweight correlation works

- Basic file log input
- Basic pcap network capture
- Time-based correlation (±100ms window)
- JSON output
- Simple CLI

**Success Criteria:** Can correlate ERROR logs with network events in < 1 minute setup.

### Phase 2: Usability (Months 3-4)

**Goal:** Make it easy to use

- Better CLI with sensible defaults
- Multiple log formats (JSON, logfmt, syslog)
- Basic filtering
- Clear output formatting
- Documentation and examples

**Success Criteria:** Developers can use it without reading docs.

### Phase 3: Integration (Months 5-6)

**Goal:** Work well with existing tools

- Export to ELK
- Prometheus metrics export
- REST API
- Kafka/NATS output
- Better error handling

**Success Criteria:** Integrates seamlessly with existing stacks.

### Phase 4: Polish (Months 7-9)

**Goal:** Production-ready

- Performance optimizations
- Advanced correlation (ID-based)
- Configuration file support
- Better documentation
- Community building

**Success Criteria:** Used in production by early adopters.

---

## Risk Mitigation

### Risk: Feature Creep

**Mitigation:**

- Regular reviews against this strategy document
- "Does this belong in nblex?" checklist
- Community feedback on scope
- Say "no" to features that bloat the tool

### Risk: Performance Claims

**Mitigation:**

- Early benchmarking
- Real-world testing
- Performance regression tests
- Be honest about limitations

### Risk: Correlation Accuracy

**Mitigation:**

- Start with simple time-based correlation
- Test with real-world data
- Provide confidence scores
- Allow manual tuning

### Risk: Adoption

**Mitigation:**

- Clear positioning (lightweight, focused)
- Great developer experience
- Good documentation
- Community engagement
- Integration with existing tools

---

## Decision Framework

### When Evaluating New Features

**Ask These Questions:**

1. **Does it help with correlation?** (Must answer yes)
2. **Does it stay lightweight?** (Must answer yes)
3. **Can it be used in < 1 minute?** (Should answer yes)
4. **Does it belong in another tool?** (Should answer no)
5. **Does it add complexity?** (Prefer no)

**Scoring:**

- 5/5 yes = Strong candidate
- 4/5 yes = Consider carefully
- 3/5 yes = Probably not
- <3/5 yes = Reject

### When Evaluating Integration Requests

**Ask These Questions:**

1. **Does it complement existing tools?** (Must answer yes)
2. **Is it simple to integrate?** (Should answer yes)
3. **Does it add value?** (Must answer yes)
4. **Does it bloat nblex?** (Must answer no)

---

## Key Principles Summary

1. **Lightweight** - Single binary, minimal dependencies, low memory
2. **Focused** - Does correlation well, nothing else
3. **Simple** - Easy to install, easy to run, easy to understand
4. **Fast** - Quick to start, quick to correlate, quick feedback
5. **Complementary** - Works WITH existing tools, doesn't replace them
6. **Developer-first** - Built for developers debugging issues
7. **Ad-hoc friendly** - Perfect for on-demand use, not 24/7 platforms

---

## Review & Update Process

This document should be reviewed:

- **Monthly** - Check alignment with development progress
- **Quarterly** - Review strategy and adjust based on feedback
- **Before major features** - Ensure new features align with strategy
- **After user feedback** - Incorporate learnings from users

**Update Triggers:**

- Significant user feedback
- Competitive landscape changes
- Major feature decisions
- Pivot in direction

---

**Remember:** nblex's strength is being lightweight and focused. Don't lose that by trying to be everything to everyone. Stay focused on correlation, stay lightweight, stay simple.
