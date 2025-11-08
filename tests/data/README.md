# Test Data

This directory contains test data for nblex tests.

## Structure

- `logs/` - Sample log files in various formats
- `pcaps/` - Sample packet capture files
- `scenarios/` - Known correlation scenarios with expected outputs

## Adding Test Data

When adding test data:

1. Use realistic but anonymized data
2. Keep files small (< 1MB preferred)
3. Document the expected behavior
4. Add corresponding test cases

## Scenarios

Each scenario should have:
- `input.log` - Log file input
- `input.pcap` - Packet capture input
- `expected_output.json` - Expected correlated events

Example:
```
scenarios/scenario_001_error_with_network_timeout/
├── input.log
├── input.pcap
└── expected_output.json
```

## License

Test data is provided for testing purposes only.
