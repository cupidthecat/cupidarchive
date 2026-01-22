#include "arc_reader.h"
#include "arc_tar.h"
#include "arc_zip.h"
#include "arc_filter.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>

// Forward declarations
static int detect_format(ArcStream *stream, ArcStream **decompressed);
static ArcReader *create_reader(ArcStream *stream, int format);

// Format types (must match arc_tar.c and arc_zip.c)
#define ARC_FORMAT_TAR 0
#define ARC_FORMAT_ZIP 1

int arc_next(ArcReader *reader, ArcEntry *entry) {
    if (!reader || !entry) {
        return -1;
    }
    // Check format field (first field in both TarReader and ZipReader)
    int format = ((int *)reader)[0];
    switch (format) {
        case ARC_FORMAT_TAR:
            return arc_tar_next(reader, entry);
        case ARC_FORMAT_ZIP:
            return arc_zip_next(reader, entry);
        default:
            return -1;
    }
}

void arc_entry_free(ArcEntry *entry) {
    if (entry) {
        free(entry->path);
        free(entry->link_target);
        memset(entry, 0, sizeof(*entry));
    }
}

ArcStream *arc_open_data(ArcReader *reader) {
    if (!reader) {
        return NULL;
    }
    int format = ((int *)reader)[0];
    switch (format) {
        case ARC_FORMAT_TAR:
            return arc_tar_open_data(reader);
        case ARC_FORMAT_ZIP:
            return arc_zip_open_data(reader);
        default:
            return NULL;
    }
}

int arc_skip_data(ArcReader *reader) {
    if (!reader) {
        return -1;
    }
    int format = ((int *)reader)[0];
    switch (format) {
        case ARC_FORMAT_TAR:
            return arc_tar_skip_data(reader);
        case ARC_FORMAT_ZIP:
            return arc_zip_skip_data(reader);
        default:
            return -1;
    }
}

void arc_close(ArcReader *reader) {
    if (reader) {
        int format = ((int *)reader)[0];
        switch (format) {
            case ARC_FORMAT_TAR:
                arc_tar_close(reader);
                break;
            case ARC_FORMAT_ZIP:
                arc_zip_close(reader);
                break;
            default:
                // Unknown format, try both (one will fail gracefully)
                arc_tar_close(reader);
                arc_zip_close(reader);
                break;
        }
    }
}

ArcReader *arc_open_path(const char *path) {
    if (!path) {
        return NULL;
    }
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }
    
    // Get file size for byte limit
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return NULL;
    }
    
    // Create stream with reasonable limit (10x file size for compressed archives)
    ArcStream *stream = arc_stream_from_fd(fd, st.st_size * 10);
    if (!stream) {
        close(fd);
        return NULL;
    }
    
    // Detect format and decompression
    ArcStream *decompressed = NULL;
    int format = detect_format(stream, &decompressed);
    if (format < 0) {
        arc_stream_close(stream);
        return NULL;
    }
    
    // Use decompressed stream if available
    ArcStream *final_stream = decompressed ? decompressed : stream;
    
    // Create reader
    ArcReader *reader = create_reader(final_stream, format);
    if (!reader) {
        if (decompressed) {
            arc_stream_close(decompressed);
        }
        arc_stream_close(stream);
        return NULL;
    }
    
    return reader;
}

ArcReader *arc_open_stream(ArcStream *stream) {
    if (!stream) {
        return NULL;
    }
    
    // Detect format
    ArcStream *decompressed = NULL;
    int format = detect_format(stream, &decompressed);
    if (format < 0) {
        return NULL;
    }
    
    ArcStream *final_stream = decompressed ? decompressed : stream;
    return create_reader(final_stream, format);
}

// Detect archive format and compression
static int detect_format(ArcStream *stream, ArcStream **decompressed) {
    *decompressed = NULL;
    
    // Read first few bytes to detect compression
    uint8_t magic[4];
    int64_t pos = arc_stream_tell(stream);
    ssize_t n = arc_stream_read(stream, magic, sizeof(magic));
    if (n < 2) {
        return -1;
    }
    
    // Check for gzip (0x1f 0x8b)
    if (magic[0] == 0x1f && magic[1] == 0x8b) {
        arc_stream_seek(stream, pos, SEEK_SET);
        *decompressed = arc_filter_gzip(stream, 0); // 0 = use stream's limit
        if (!*decompressed) {
            return -1;
        }
        stream = *decompressed;
        n = arc_stream_read(stream, magic, sizeof(magic));
        if (n < 2) {
            return -1;
        }
    }
    // Check for bzip2 (BZ)
    else if (magic[0] == 'B' && magic[1] == 'Z' && n >= 3 && magic[2] == 'h') {
        arc_stream_seek(stream, pos, SEEK_SET);
        *decompressed = arc_filter_bzip2(stream, 0);
        if (!*decompressed) {
            return -1;
        }
        stream = *decompressed;
        n = arc_stream_read(stream, magic, sizeof(magic));
        if (n < 2) {
            return -1;
        }
    }
    // Check for xz (FD 37 7A 58 5A 00)
    else if (magic[0] == 0xFD && magic[1] == 0x37 && n >= 4 &&
             magic[2] == 0x7A && magic[3] == 0x58) {
        arc_stream_seek(stream, pos, SEEK_SET);
        *decompressed = arc_filter_xz(stream, 0);
        if (!*decompressed) {
            return -1;
        }
        stream = *decompressed;
        n = arc_stream_read(stream, magic, sizeof(magic));
        if (n < 2) {
            return -1;
        }
    } else {
        // No compression, reset position
        arc_stream_seek(stream, pos, SEEK_SET);
    }
    
    // Now detect format (after decompression if any)
    // Check for ZIP first (more specific signature)
    if (n >= 2 && magic[0] == 'P' && magic[1] == 'K') {
        // Could be ZIP - check for ZIP signatures
        if (n >= 4) {
            uint32_t sig = (uint32_t)magic[0] | ((uint32_t)magic[1] << 8) |
                          ((uint32_t)magic[2] << 16) | ((uint32_t)magic[3] << 24);
            if (sig == 0x04034b50 || sig == 0x06054b50 || sig == 0x02014b50) {
                // ZIP signature found
                arc_stream_seek(stream, pos, SEEK_SET);
                return 1; // ZIP format
            }
        }
    }
    
    // TAR: Check for ustar magic or old TAR format
    // Read first 512 bytes to check TAR header
    uint8_t header[512];
    pos = arc_stream_tell(stream);
    n = arc_stream_read(stream, header, sizeof(header));
    if (n == sizeof(header)) {
        // Check for ustar magic (at offset 257)
        if ((memcmp(header + 257, "ustar", 5) == 0) ||
            (memcmp(header + 257, "USTAR", 5) == 0) ||
            (header[0] != '\0' && isprint((unsigned char)header[0]))) {
            arc_stream_seek(stream, pos, SEEK_SET);
            return 0; // TAR format
        }
    }
    
    arc_stream_seek(stream, pos, SEEK_SET);
    return -1; // Unknown format
}

static ArcReader *create_reader(ArcStream *stream, int format) {
    switch (format) {
        case 0: // TAR
            return arc_tar_open(stream);
        case 1: // ZIP
            return arc_zip_open(stream);
        default:
            return NULL;
    }
}

