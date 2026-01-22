#ifndef ARC_FILTER_H
#define ARC_FILTER_H

#include "arc_stream.h"

/**
 * Decompression filter layer.
 * 
 * Filters wrap an underlying ArcStream and expose another ArcStream
 * that decompresses data on-the-fly.
 */

/**
 * Create a gzip decompression filter.
 * 
 * @param underlying Stream to decompress (must remain valid for filter lifetime)
 * @param byte_limit Maximum decompressed bytes to allow (0 = unlimited, not recommended)
 * @return New stream that decompresses gzip data, or NULL on error
 */
ArcStream *arc_filter_gzip(ArcStream *underlying, int64_t byte_limit);

/**
 * Create a bzip2 decompression filter.
 * 
 * @param underlying Stream to decompress (must remain valid for filter lifetime)
 * @param byte_limit Maximum decompressed bytes to allow (0 = unlimited, not recommended)
 * @return New stream that decompresses bzip2 data, or NULL on error
 */
ArcStream *arc_filter_bzip2(ArcStream *underlying, int64_t byte_limit);

/**
 * Create an xz/lzma decompression filter.
 * 
 * @param underlying Stream to decompress (must remain valid for filter lifetime)
 * @param byte_limit Maximum decompressed bytes to allow (0 = unlimited, not recommended)
 * @return New stream that decompresses xz data, or NULL on error
 * 
 * Note: Requires liblzma. Returns NULL if not available.
 */
ArcStream *arc_filter_xz(ArcStream *underlying, int64_t byte_limit);

#endif // ARC_FILTER_H

