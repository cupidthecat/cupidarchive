# CupidArchive

A lightweight, safe archive reading library for previewing archive contents without extraction.

## Overview

CupidArchive provides a clean 3-layer architecture for reading archive files:

1. **IO Layer** - Safe stream abstraction with byte limits to prevent zip bombs
2. **Filter Layer** - Decompression wrappers (gzip, bzip2, xz)
3. **Format Layer** - Archive format parsers (TAR, with more to come)

## Current Support

- **Formats:** TAR (ustar + pax extensions)
- **Compression:** gzip, bzip2 (xz coming soon)
- **Entry Types:** Regular files, directories, symlinks, hardlinks
- **Operations:** Reading, previewing, and **extraction**

## Architecture

### IO Layer (`arc_stream.h`, `arc_stream.c`)

Provides a unified stream interface that can be backed by:
- File descriptors (`pread`, `read`)
- Memory buffers
- Substreams (bounded reads for entries)
- Decompression filters

Key safety feature: **Hard byte limits per stream** to prevent zip bombs and runaway reads.

### Filter Layer (`arc_filter.h`, `arc_filter.c`)

Decompression wrappers that wrap an underlying `ArcStream` and expose another `ArcStream`:
- **gzip:** Uses zlib (`inflate`)
- **bzip2:** Uses libbz2
- **xz/lzma:** Uses liblzma (planned)

### Format Layer (`arc_reader.h`, `arc_reader.c`)

Archive format parsers. Each format implements:
- `open` - Sniff header and initialize
- `iterate next entry` - Parse entry headers
- `open entry data stream` - Get bounded stream for current entry
- `skip entry data fast` - Skip to next entry without reading data

## API

### Basic Usage

```c
#include "cupidarchive/arc_reader.h"

ArcReader *reader = arc_open_path("archive.tar.gz");
if (!reader) {
    // Handle error
    return;
}

ArcEntry entry;
while (arc_next(reader, &entry) == 0) {
    printf("Entry: %s (size: %lu)\n", entry.path, entry.size);
    
    // Optionally read entry data
    ArcStream *data = arc_open_data(reader);
    if (data) {
        char buffer[4096];
        ssize_t n = arc_stream_read(data, buffer, sizeof(buffer));
        // ... process data ...
        arc_stream_close(data);
    }
}

arc_close(reader);
```

### Extraction Usage

```c
#include "cupidarchive/arc_reader.h"

// Extract entire archive
ArcReader *reader = arc_open_path("archive.tar.gz");
if (!reader) {
    // Handle error
    return;
}

int result = arc_extract_to_path(reader, "/tmp/extracted", true, true);
if (result < 0) {
    // Handle error
}

arc_close(reader);

// Or extract entries one by one
ArcReader *reader2 = arc_open_path("archive.tar");
ArcEntry entry;
while (arc_next(reader2, &entry) == 0) {
    int result = arc_extract_entry(reader2, &entry, "/tmp/extracted", true, true);
    if (result < 0) {
        // Handle error for this entry
    }
    arc_entry_free(&entry);
}
arc_close(reader2);
```

### Entry Structure

```c
typedef struct ArcEntry {
    char     *path;        // Normalized path
    uint64_t  size;        // File size in bytes
    uint32_t  mode;        // File mode/permissions
    uint64_t  mtime;       // Modification time
    uint8_t   type;        // Entry type (file/dir/symlink)
    char     *link_target; // Symlink target (if applicable)
    uint32_t  uid;         // User ID
    uint32_t  gid;         // Group ID
} ArcEntry;
```

## Building

```bash
cd cupidarchive
make
```

This builds `libcupidarchive.a` (static library).

## Integration

Link against the library:

```bash
gcc -o myapp myapp.c -Lcupidarchive -lcupidarchive -lz -lbz2
```

## Testing

The library includes a comprehensive test suite. To run tests:

```bash
cd cupidarchive
make test
```

This will:
- Build the library (if not already built)
- Compile all test executables
- Run all tests and report results

### Advanced Testing

Run tests with AddressSanitizer for memory error detection:

```bash
cd cupidarchive/tests
make test-asan
```

Run tests with Valgrind for detailed memory analysis:

```bash
cd cupidarchive/tests
make test-valgrind
```

See `tests/README.md` for more information about the test suite.

## Safety Features

- **Byte limits:** Every stream has a hard byte limit to prevent zip bombs
- **Bounds checking:** All reads are bounds-checked
- **Error handling:** Comprehensive error codes and error reporting
- **Memory safety:** Careful memory management to prevent leaks

## Extraction Features

- **Full archive extraction:** `arc_extract_to_path()` extracts all entries
- **Single entry extraction:** `arc_extract_entry()` extracts one entry at a time
- **Directory creation:** Automatically creates parent directories as needed
- **Permission preservation:** Optional preservation of file permissions and ownership
- **Timestamp preservation:** Optional preservation of modification times
- **Symlink support:** Creates symlinks correctly
- **Hardlink handling:** Attempts to create hardlinks, falls back to copying

## Future Plans

- [ ] xz/lzma compression support
- [ ] zstd compression support
- [ ] ZIP format support
- [ ] 7z format support
- [ ] RAR format support (read-only)
- [ ] Progress callbacks for extraction
- [ ] Extraction filters (exclude patterns)

## License

This library is licensed under the GNU General Public License v3.0 (GPL-3.0).

This means:
- You are free to use, modify, and distribute this library
- If you modify the library, you must release your changes under GPL-3.0
- If you use this library in your project, your project must also be licensed under GPL-3.0 (or a compatible license)

See the LICENSE file in the parent directory for the full license text.

