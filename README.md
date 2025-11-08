# nblex - Non-Blocking Lexer

A lightweight C library for performing lexical analysis with Unicode support in a non-blocking, streaming manner.

## Overview

nblex (Non-Blocking Lexer) is designed to process text input incrementally, making it ideal for applications that need to parse or analyze text/XML content where data arrives in chunks (network streams, pipes, etc.) rather than all at once. The library automatically handles UTF-8 to Unicode conversion and maintains state across multiple input calls.

## Features

- **Streaming Input Processing** - Process data byte-by-byte or in chunks without requiring the entire input upfront
- **UTF-8 to Unicode Conversion** - Automatic decoding of UTF-8 byte sequences into Unicode codepoints
- **Non-Blocking Architecture** - Stateful processing allows for incremental data handling
- **XML Name Validation** - Built-in functions for validating XML 1.0/1.1 naming rules
- **Flexible Input Methods** - Accept data as raw bytes (auto-decoded) or pre-decoded Unicode codepoints
- **Error Handling** - Detects and reports invalid UTF-8 sequences
- **Cross-Platform** - Standard C library with minimal dependencies

## Requirements

- C compiler (GCC, Clang, or compatible)
- Autotools (autoconf >= 2.62, automake >= 1.11)
- libtool
- Standard C library headers (string.h, stdlib.h)

## Building

nblex uses the standard Autotools build system:

```bash
./autogen.sh      # Generate configure script (if building from git)
./configure       # Configure the build
make              # Build the library
make check        # Run tests (optional)
sudo make install # Install the library
```

### Build Options

- `--enable-debug` - Enable debug messages (default: disabled)

## Installation

After building, install with:

```bash
sudo make install
```

This installs:
- **libnblex** - Shared/static library
- **nblex** - Command-line test utility
- Header files in `/usr/local/include`

## Usage

### Basic Example

```c
#include <nblex.h>
#include <stdio.h>

int main() {
    // Initialize the lexer world
    nblex_world* world = nblex_new_world();
    if (!world) {
        fprintf(stderr, "Failed to create nblex world\n");
        return 1;
    }

    if (nblex_world_open(world) != 0) {
        fprintf(stderr, "Failed to open nblex world\n");
        nblex_free_world(world);
        return 1;
    }

    // Start lexing
    nblex_start(world);

    // Add bytes (UTF-8 encoded text)
    const unsigned char* text = (const unsigned char*)"Hello, 世界!";
    nblex_add_bytes(world, text, strlen((const char*)text));

    // Or add individual bytes
    nblex_add_byte(world, 'A');

    // Finish lexing
    nblex_finish(world);

    // Clean up
    nblex_free_world(world);

    return 0;
}
```

### Streaming Example

```c
// Process data as it arrives (e.g., from network or file)
nblex_world* world = nblex_new_world();
nblex_world_open(world);
nblex_start(world);

// Read and process data in chunks
unsigned char buffer[1024];
size_t bytes_read;

while ((bytes_read = read_from_source(buffer, sizeof(buffer))) > 0) {
    nblex_add_bytes(world, buffer, bytes_read);
    // Process the decoded codepoints...
}

nblex_finish(world);
nblex_free_world(world);
```

### Using Unicode Codepoints Directly

```c
nblex_world* world = nblex_new_world();
nblex_world_open(world);
nblex_start(world);

// Add pre-decoded Unicode codepoints
nblex_unichar codepoints[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
nblex_add_codepoints(world, codepoints, 5);

// Or individual codepoints
nblex_add_codepoint(world, 0x4E16); // 世
nblex_add_codepoint(world, 0x754C); // 界

nblex_finish(world);
nblex_free_world(world);
```

## API Reference

### Initialization Functions

```c
nblex_world* nblex_new_world(void);
```
Create a new nblex world object.

```c
int nblex_world_open(nblex_world* world);
```
Open and initialize a world object for use.

```c
int nblex_free_world(nblex_world* world);
```
Free a world object and release resources.

### Lexing Functions

```c
int nblex_start(nblex_world* world);
```
Start lexing process.

```c
int nblex_finish(nblex_world* world);
```
Finish lexing process.

### Input Functions

```c
int nblex_add_byte(nblex_world* world, const unsigned char b);
```
Add a single byte to be processed.

```c
int nblex_add_bytes(nblex_world* world, const unsigned char* buffer, size_t len);
```
Add multiple bytes to be processed.

```c
int nblex_add_codepoint(nblex_world* world, const nblex_unichar codepoint);
```
Add a single Unicode codepoint.

```c
int nblex_add_codepoints(nblex_world* world, const nblex_unichar* codepoints, size_t len);
```
Add multiple Unicode codepoints.

### Special Codepoint Values

- `NBLEX_CODEPOINT_INVALID` (0xF0000D) - Invalid UTF-8 sequence detected
- `NBLEX_CODEPOINT_END_OF_INPUT` (0xFFFFFF) - End of input reached
- `NBLEX_UNICODE_MAX_CODEPOINT` (0x10FFFF) - Maximum valid Unicode codepoint

## Command-Line Tool

The `nblex` utility reads from stdin and processes it through the lexer:

```bash
echo "Hello, World!" | nblex
cat file.txt | nblex
```

## Use Cases

- **Streaming Parsers** - Parse XML/text from network streams without buffering entire document
- **Protocol Handlers** - Process text-based protocols incrementally
- **Interactive Applications** - Handle user input character-by-character with proper Unicode support
- **Memory-Constrained Systems** - Process large files without loading them entirely into memory
- **Pipeline Processing** - Filter and transform text in Unix-style pipelines

## Version

Current version: **0.1**

## License

nblex is triple-licensed under your choice of:

1. **GNU Lesser General Public License (LGPL) V2.1** or any newer version
2. **GNU General Public License (GPL) V2** or any newer version
3. **Apache License, V2.0** or any newer version

You may not use this file except in compliance with at least one of the above three licenses.

See LICENSE.html, LICENSE.txt, COPYING.LIB, COPYING, and LICENSE-2.0.txt for complete license terms.

## Author

**David Beckett**
- Website: http://www.dajobe.org/
- Bug Reports: http://bugs.librdf.org/

## Copyright

Copyright (C) 2013, David Beckett

## Contributing

This project follows standard Autotools conventions. When contributing:

1. Maintain code style consistency
2. Add tests for new features
3. Update documentation as needed
4. Ensure all compiler warnings are addressed

## Links

- Project website: http://www.dajobe.org/
- Bug tracker: http://bugs.librdf.org/
