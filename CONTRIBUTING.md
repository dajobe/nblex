# Contributing to nblex

Thank you for your interest in contributing to nblex!

## Development Setup

### Prerequisites

- C11 compiler (GCC 4.9+, Clang 3.3+, MSVC 2015+)
- CMake 3.10+
- Git

### Building from Source

```bash
git clone https://github.com/dajobe/nblex.git
cd nblex
mkdir build && cd build
cmake ..
make
```

### Running Tests

```bash
cd build
make test
```

### Building with Sanitizers

```bash
cmake -DNBLEX_ENABLE_ASAN=ON -DNBLEX_ENABLE_UBSAN=ON ..
make
```

## Project Structure

```
nblex/
├── include/nblex/      # Public API headers
├── src/
│   ├── core/          # Core event loop and world management
│   ├── input/         # Input handlers (logs, network)
│   ├── parsers/       # Log and protocol parsers
│   ├── correlation/   # Correlation engine
│   ├── output/        # Output handlers
│   └── util/          # Utilities
├── tests/             # Test suite
├── examples/          # Example programs
├── cli/               # Command-line tool
└── docs/              # Documentation
```

## Coding Standards

### C Style

- **Standard:** C11
- **Indentation:** 2 spaces (no tabs)
- **Line length:** 80 characters (soft limit)
- **Braces:** K&R style

Example:
```c
int nblex_world_open(nblex_world* world) {
  if (!world) {
    return -1;
  }

  world->opened = true;
  return 0;
}
```

### Naming Conventions

- **Functions:** `nblex_module_function()` (snake_case with module prefix)
- **Types:** `nblex_type_name` (snake_case)
- **Structs:** `struct nblex_type_s` (with `_s` suffix)
- **Macros:** `NBLEX_CONSTANT` (UPPER_CASE)

### Error Handling

- Return 0 on success, non-zero on error
- Use NULL for pointer errors
- Always check for NULL before dereferencing

### Memory Management

- Always free what you allocate
- Set pointers to NULL after freeing
- Use `nblex_malloc/free` wrappers (not raw `malloc/free`)

## Submitting Changes

### Workflow

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Make your changes
4. Add tests for new functionality
5. Ensure all tests pass
6. Run static analysis (if available)
7. Commit with clear messages
8. Push to your fork
9. Open a pull request

### Commit Messages

Format:
```
component: Brief description (50 chars or less)

Longer explanation if needed (wrap at 72 characters).

- Bullet points for details
- Reference issues: Fixes #123
```

Example:
```
correlation: Add time-based correlation strategy

Implements time-based correlation using configurable time windows.
Events within the window are matched and emitted as correlation events.

- Add nblex_correlation_time_based.c
- Add unit tests for time window matching
- Update API documentation

Fixes #42
```

### Pull Request Guidelines

- One feature/fix per PR
- Include tests
- Update documentation
- Pass all CI checks
- Respond to review comments

## Testing

### Unit Tests

Add unit tests in `tests/unit/` for all new functions.

### Integration Tests

Add end-to-end tests in `tests/integration/` for new features.

### Test Data

- Add sample logs to `tests/data/logs/`
- Add sample pcaps to `tests/data/pcaps/`
- Add correlation scenarios to `tests/data/scenarios/`

### Fuzzing

Add fuzzing harnesses in `tests/fuzz/` for parsers and input handling.

## Documentation

- Update public API documentation in header files
- Add examples for new features
- Update README.md if adding major features
- Use Doxygen-style comments

Example:
```c
/**
 * nblex_world_new - Create a new nblex world
 *
 * Allocates and initializes a new world instance.
 *
 * Returns: New world instance or NULL on error
 */
NBLEX_API nblex_world* nblex_world_new(void);
```

## Performance Considerations

- Profile before optimizing
- Document performance-critical code
- Add benchmarks for performance-sensitive features
- Avoid premature optimization

## Security

- Always validate input
- Use safe string functions (`strncpy`, not `strcpy`)
- Check buffer bounds
- Sanitize user-provided patterns/queries
- Report security issues privately (see SECURITY.md)

## License

By contributing, you agree that your contributions will be licensed under the Apache License 2.0.

## Questions?

- Open a GitHub Discussion for questions
- Join our Discord (link TBD)
- Read the specification in SPEC.md

Thank you for contributing!
