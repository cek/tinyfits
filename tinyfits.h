// SPDX-License-Identifier: MIT OR Unlicense

/*
 * tinyfits.h -- single-header FITS image reader/writer.
 *
 * Loads the first image HDU in a FITS file, and writes single-HDU FITS image files.
 *
 * An HDU is considered an image if:
 *   - It is the primary HDU, or an XTENSION HDU of type "IMAGE"
 *   - and NAXIS==2 or ==3
 *   - and NAXIS1 != 0
 *
 * Supports:
 *  - Greyscale, RGB, and multi-plane images
 *  - 8/16/32-bit integer and 32/64-bit floating point format images
 *  - Arbitrary BZERO/BSCALE transformations
 *  - Unsigned-integer conversion when indicated by BSCALE/BZERO
 *  - Optional image data conversion to normalized or physical float32 values
 *  - Keyword reading, setting, addition, and deletion
 *  - Long string values via CONTINUE chains (transparent on read and write)
 *  - HIERARCH long keywords up to 63 chars (transparent on read and write)
 *  - Long HISTORY/COMMENT text auto-split
 *  - Single-HDU image writing, with interleaved-to-planar conversion
 *  - Custom memory allocation
 *
 * Not supported
 *  - Non-image HDUs (tables, 1D arrays, Random Groups, etc.)
 *  - Reading any image HDUs other than the first
 *  - 64-bit integer image data
 *  - Compressed images (e.g., Rice, GZIP, HCOMPRESS)
 *  - Multi-HDU output
 *
 * Including:
 *
 *   #define TINYFITS_IMPLEMENTATION   // in exactly one C/C++ file
 *   #include "tinyfits.h"
 *
 * Loading and saving:
 *
 *   Most library functions take a pointer to a caller-owned TinyFitsHeader, which holds metadata
 *   associated with a FITS file.
 *
 *   tinyfits_load()                    // Load TinyFitsHeader and image data from named FITS file
 *   tinyfits_load_from_memory()        // Load TinyFitsHeader and image data from buffer
 *   tinyfits_load_header()             // Load TinyFitsHeader, but no image data, from named FITS file
 *   tinyfits_load_header_from_memory() // Load TinyFitsHeader, but no image data, from buffer
 *   tinyfits_save()                    // Save FITS image to named file
 *   tinyfits_save_to_memory()          // Save FITS image to buffer
 *   tinyfits_free_header()             // Free contents of a TinyFitsHeader
 *   tinyfits_free_buffer()             // Free buffer returned by tinyfits_load*() / tinyfits_save_to_memory()
 *
 * Error reporting:
 *
 *   All public functions return an integer error code, with TINYFITS_OK (0) indicating success.
 *   On error, one of the non-zero TINYFITS_ERR_* values will be returned, and if a valid TinyFitsHeader
 *   pointer is provided, its last_error field will contain a string describing the cause of the error:
 *
 *      TinyFitsHeader header = {0};
 *      void *data;
 *      int err = tinyfits_load(&header, "file.fits", &data);
 *      if (err != TINYFITS_OK)
 *      {
 *          fprintf(stderr, "Could not open FITS file: %s\n", header.last_error);
 *      }
 *
 *   The last_error pointer is only ever updated by the API when an error occurs. As such, it should not be
 *   used to test for error by itself -- a non-zero value may not be associated with the most recent API call.
 *
 * Pixel datatypes and values:
 *
 *   When reading images, the TinyFitsHeader struct provides image metadata, including
 *   dimensions, datatype, and BZERO/BSCALE values that can be used to convert
 *   stored pixel values to physical values using the transform:
 *
 *       physical = header.bzero + header.bscale * stored.
 *
 *   Usually, BZERO, BSCALE, and pixel values are copied directly from the image HDU without
 *   modification, using the pixel datatype indicated by BITPIX, as detailed below.
 *
 *     BITPIX   pixel_type         C type
 *        8     TINYFITS_UINT8     uint8_t
 *       16     TINYFITS_INT16     int16_t
 *       32     TINYFITS_INT32     int32_t
 *      -32     TINYFITS_FLOAT32   float
 *      -64     TINYFITS_FLOAT64   double
 *
 *   However, two combinations of BITPIX/BZERO/BSCALE imply unsigned-integer conversion,
 *   as per FITS convention. In these cases, the pixel data is converted to the
 *   implied datatype on read, and the returned TinyFitsHeader struct will have bzero=0, bscale=1.
 *
 *     BITPIX  BSCALE  BZERO         pixel_type         C type
 *       16    1       32768         TINYFITS_UINT16    uint16_t
 *       32    1       2147483648    TINYFITS_UINT32    uint32_t
 *
 *   Image data can be converted from stored to physical values using tinyfits_to_float_physical().
 *
 *   When saving an unsigned-integer image, tinyfits writes the corresponding special BZERO/BSCALE values
 *   to the header. Otherwise, the given BSCALE, BZERO, and pixel values are emitted verbatim.
 *
 *   Saving requires bscale != 0. Note that a zero-initialized TinyFitsHeader will fail save
 *   with TINYFITS_ERR_BZERO_BSCALE; callers must set bscale explicitly before saving.
 *
 * Header keywords:
 *
 *   TinyFitsHeader stores parsed keyword cards in the same order as the source file. Each card consists
 *   of a key, a value, and an optional comment.
 *
 *   When constructing the TinyFitsKeyword entries stored in TinyFitsHeader, a card's parsing and
 *   storage method depends on the key itself:
 *
 *      - Generic key: key, value, and optional comment copied verbatim into a new entry
 *      - CONTINUE: value is appended to preceding entry's value; no new entry added
 *      - HISTORY or COMMENT: new entry holds key and value; no comment allowed
 *      - HIERARCH: long or whitespace-containing keys up to 63 chars; values and comments copied verbatim
 *
 *   Keywords may be iterated over and read directly using header.keywords and header.num_keywords.
 *   Keywords should not be modified directly, but instead using an appropriate keyword entry point:
 *
 *      tinyfits_get_keyword()          Get first keyword with matching key
 *      tinyfits_get_keywords()         Get all keywords with matching key
 *      tinyfits_set_keyword()          Set keyword, replacing if key exists, appending if not
 *      tinyfits_append_keyword()       Append keyword to header.keywords
 *      tinyfits_add_history()          Add HISTORY keyword, breaking long values into multiple entries
 *      tinyfits_add_comment()          Add COMMENT keyword, breaking long values into multiple entries
 *      tinyfits_remove_keyword()       Remove first keyword with matching key
 *
 *   Adding, removing, or modifying a keyword (including via tinyfits_set_keyword) invalidates pointers
 *   returned by previous tinyfits_get_keyword* calls and any value/comment strings obtained through them.
 *
 * Custom allocators:
 *
 *   Define TINYFITS_MALLOC, TINYFITS_CALLOC, TINYFITS_REALLOC, and
 *   TINYFITS_FREE before including the implementation to use custom
 *   allocators. All four must be defined together.
 *
 *   #define TINYFITS_MALLOC(sz)        my_malloc(sz)
 *   #define TINYFITS_CALLOC(cnt, sz)   my_calloc(cnt, sz)
 *   #define TINYFITS_REALLOC(p, sz)    my_realloc(p, sz)
 *   #define TINYFITS_FREE(p)           my_free(p)
 *   #define TINYFITS_IMPLEMENTATION
 *   #include "tinyfits.h"
 *
 * Linkage:
 *
 *   By default, public functions are declared extern. To instead make public functions
 *   static, define TINYFITS_STATIC in the same file as TINYFITS_IMPLEMENTATION.
 *
 * Thread safety:
 *
 *   The library maintains no global state, but concurrent access to a single TinyFitsHeader
 *   requires external synchronization.
 *
 * License: see end of file.
 */

#ifndef TINYFITS_H
#define TINYFITS_H

#include <stddef.h>
#include <stdint.h>

#ifdef TINYFITS_STATIC
    #define TFITSDEF static
#else
    #define TFITSDEF extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Error codes

typedef enum
{
    TINYFITS_OK                   =  0, // no error
    TINYFITS_ERR_OPEN             =  1, // error opening file
    TINYFITS_ERR_READ             =  2, // error reading file
    TINYFITS_ERR_NOT_FITS         =  3, // header does not match FITS
    TINYFITS_ERR_INVALID          =  4, // defensive / unexpected internal state
    TINYFITS_ERR_ALLOC            =  5, // memory allocation failure
    TINYFITS_ERR_BITPIX           =  6, // unsupported BITPIX value
    TINYFITS_ERR_BZERO_BSCALE     =  7, // non-standard BZERO/BSCALE
    TINYFITS_ERR_WRITE            =  8, // file I/O failure during save
    TINYFITS_ERR_TRUNCATED        =  9, // file ended before header/data section completed
    TINYFITS_ERR_HEADER_TOO_LARGE = 10, // > TINYFITS_MAX_HEADER_BLOCKS in one HDU header
    TINYFITS_ERR_HEADER_NO_END    = 11, // no END card before EOF
    TINYFITS_ERR_NO_IMAGE         = 12, // no image HDU found anywhere in the file
    TINYFITS_ERR_BAD_DIMENSION    = 13, // width/height/channels <= 0
    TINYFITS_ERR_NULL_ARG         = 14, // required pointer arg was NULL
    TINYFITS_ERR_KEYWORD_LENGTH   = 15, // key longer than limit
    TINYFITS_ERR_RESERVED_KEYWORD = 16, // caller tried to set a reserved key (BITPIX, NAXIS, etc.)
    TINYFITS_ERR_BAD_PIXEL_TYPE   = 17  // pixel_type is UNKNOWN or unrecognized
} TinyFitsError;

#define tf_fail(h, code, msg) ((h)->last_error = (msg), (code))

// Constants

typedef enum 
{
    TINYFITS_UNKNOWN  = 0,
    TINYFITS_UINT8    = 1,
    TINYFITS_INT16    = 2,
    TINYFITS_UINT16   = 3,
    TINYFITS_INT32    = 4,
    TINYFITS_UINT32   = 5,
    TINYFITS_FLOAT32  = 6,
    TINYFITS_FLOAT64  = 7
} TinYFitsPixelType;


#define TINYFITS_BLOCK_SIZE 2880

/* FITS header card layout (FITS 4.0 sec 4.1.2). A header consists of
 * 80-byte cards, packed 36 to a TINYFITS_BLOCK_SIZE block.
 */
#define TINYFITS_CARD_SIZE          80   // total bytes per card
#define TINYFITS_CARD_KEY_LEN        8   // keyword field width (bytes 0..7)
#define TINYFITS_CARD_VALUE_OFFSET  10   // value field begins at byte 10
                                         // (bytes 8..9 are the "= " indicator)
#define TINYFITS_CARD_FIXED_VALUE_LEN 20 // fixed-format value width
                                         // (right-justified in bytes 10..29)

/* Per-field char cap used to size the parser's value/comment scratch
 * buffers for standard value cards. The card's own TINYFITS_CARD_SIZE
 * limit bounds the data naturally; this is a defensive upper bound on
 * the read loop.
 */
#define TINYFITS_CARD_VALUE_MAX_LEN 71

/* Free-form payload size of a HISTORY or COMMENT card: bytes
 * TINYFITS_CARD_KEY_LEN through TINYFITS_CARD_SIZE-1, inclusive.
 * Larger than the value-card cap because these cards have no '= '
 * indicator and no surrounding quotes -- the entire post-key span is
 * data.
 */
#define TINYFITS_HISTORY_PAYLOAD    (TINYFITS_CARD_SIZE - TINYFITS_CARD_KEY_LEN)
#define TINYFITS_CARDS_PER_BLOCK    (TINYFITS_BLOCK_SIZE / TINYFITS_CARD_SIZE)

// Types

/* One parsed FITS keyword card.
 *
 * Keyword pointer members are heap-allocated using TINYFITS_MALLOC.
 * Reading keywords and fields directly is supported, but the keyword API
 * should be used for any write operations.
 *
 * Lifetime: TinyFitsKeyword pointers returned by the get()
 * functions are invalidated by any call that modifies any keyword in the
 * enclosing TinyFitsHeader, or by the TinyFitsHeader being freed.
 *
 * Pointer members of a given TinyFitsKeyword are invalidated by any
 * call that modifies or deletes the keyword, and transitively by the same
 * operations that invalidate TinyFitsKeyword pointers.
 */
typedef struct
{
    char* key;
    char* value;
    char* comment;
} TinyFitsKeyword;

typedef struct
{
    int width;
    int height;
    int num_channels;
    int bitpix;
    int pixel_type;
    double bscale;
    double bzero;
    // Error string set when a tinyfits_* call returns a failing error code. Points
    // to a static string literal owned by tinyfits; do not free or modify.
    // Undefined after a successful call; only meaningful following a failed call.
    const char* last_error;
    TinyFitsKeyword* keywords;
    int num_keywords;
    int _keywords_capacity; // internal; do not modify.
} TinyFitsHeader;

/*
 * Load image from file. *pixels should be freed with tinyfits_free_buffer().
 *
 * Loads the first image HDU in the file. An HDU is considered an image if
 * it is the primary HDU or an XTENSION HDU of type IMAGE, with NAXIS=2 or 3,
 * and NAXIS1 != 0.
 *
 * Pixel data layout is channel-planar, row-major, in the type indicated by
 * header->pixel_type.
 */
TFITSDEF int tinyfits_load(TinyFitsHeader* header, const char* path, void** pixels);

/*
 * Similar to tinyfits_load, but reads from a caller-supplied memory buffer
 * of size bytes. Neither the data nor pixel buffers are retained.
 */
TFITSDEF int tinyfits_load_from_memory(TinyFitsHeader* header, const void* data,
                                       size_t size, void** pixels);

/*
 * Load TinyFitsHeader from given file; no image data is read.
 */
TFITSDEF int tinyfits_load_header(TinyFitsHeader* header, const char* path);

/*
 * Load TinyFitsHeader from given memory buffer.
 */
TFITSDEF int tinyfits_load_header_from_memory(TinyFitsHeader* header, const void* data, size_t size);

/*
 * Free header metadata (the header itself is not freed).
 */
TFITSDEF void tinyfits_free_header(TinyFitsHeader* header);

/*
 * Free a library-allocated buffer (pixels from load, data from save_to_memory).
 */
TFITSDEF void tinyfits_free_buffer(void* buf);

/*
 * Return a pointer to the first keyword matching key, or NULL if not found.
 *
 * Lookup is case-insensitive for standard short keys. HIERARCH-class lookups
 * (key containing a space, or length > 8) are case-sensitive and normalized:
 * leading/trailing whitespace is stripped, and runs of internal whitespace
 * are de-duplicated before comparison.
 */
TFITSDEF const TinyFitsKeyword* tinyfits_get_keyword(const TinyFitsHeader* header,
                                                     const char* key);

/*
 * Return the total count of keywords matching key, using the same lookup
 * dispatch as tinyfits_get_keyword. If out is non-NULL, copy up to max
 * matching keyword pointers in source order.
 */
TFITSDEF int tinyfits_get_keywords(const TinyFitsHeader* header, const char* key,
                                   const TinyFitsKeyword** out, int max);

/*
 * Set a keyword, replacing the first match, appending if not found.
 *
 * Lookup uses the same matching algorithm as tinyfits_get_keyword.
 *
 * key, value, and comment must only contain ASCII printable characters.
 *
 * Length limits:
 *   - Standard key: <= 8 chars.
 *   - HIERARCH-class key: <= TINYFITS_HIERARCH_KEY_MAX chars post-canonicalization.
 *   - string value: no length limit. Chains via CONTINUE on save.
 *   - numeric/logical value: truncated to TINYFITS_CARD_FIXED_VALUE_LEN chars
 *   - comment: no length limit, truncated to fit on write
 */
TFITSDEF int tinyfits_set_keyword(TinyFitsHeader* header, const char* key,
                                  const char* value, const char* comment);

/*
 * Append a keyword without replacing existing keys. Intended for keys
 * that can be repeated in a header, similarly to HISTORY and COMMENT,
 * which have dedicated helper functions.
 */
TFITSDEF int tinyfits_append_keyword(TinyFitsHeader* header, const char* key,
                                  const char* value, const char* comment);

/*
 * Append a HISTORY card. Equivalent to
 * tinyfits_append_keyword(header, "HISTORY", text, NULL);
 *
 * Long text auto-splits across multiple HISTORY cards.
 */
TFITSDEF int tinyfits_add_history(TinyFitsHeader* header, const char* text);

/*
 * Append a COMMENT card. Equivalent to
 * tinyfits_append_keyword(header, "COMMENT", text, NULL);
 *
 * Long text auto-splits across multiple COMMENT cards.
 */
TFITSDEF int tinyfits_add_comment(TinyFitsHeader* header, const char* text);

/*
 * Remove the first keyword matching key. No-op if key is not present.
 * Lookup uses the same matching algorithm as tinyfits_get_keyword.
 */
TFITSDEF int tinyfits_remove_keyword(TinyFitsHeader* header, const char* key);

/*
 * Total pixel buffer size in bytes.
 * Returns 0 if dimensions or pixel_type are invalid.
 */
TFITSDEF size_t tinyfits_image_size(const TinyFitsHeader* header);

/*
 * Apply the FITS physical-unit transform to the given array of pixel values:
 *   out[i] = header->bzero + header->bscale * stored[i].
 *
 * Supports all pixel types. For float pixels with a non-identity transform,
 * the transform is applied using fp32 precision; callers needing higher
 * precision must read native pixels and apply the transform in double themselves.
 *
 * out must point to at least width * height * num_channels floats.
 * out and pixels must not alias.
 */
TFITSDEF int tinyfits_to_float_physical(const TinyFitsHeader* header, const void* pixels,
                                        float* out);

/*
 * Map the storage range [type_min, type_max] linearly to [0, 1] using:
 *
 *   out[i] = (stored[i] - type_min) / (type_max - type_min).
 * 
 * Supports integer pixel types only. Callers needing normalization for floats
 * should compute their own min/max from the native pixel values.
 *
 * out must point to at least width * height * num_channels floats.
 * out and pixels must not alias.
 */
TFITSDEF int tinyfits_to_float_normalized(const TinyFitsHeader* header, const void* pixels,
                                          float* out);

/*
 * Write the given image to a FITS file.
 *
 * When interleaved != 0, pixels are assumed to be interleaved (RGBRGB...).
 * When interleaved == 0, pixels are assumed to be planar (RRR...GGG...BBB...);
 */
TFITSDEF int tinyfits_save(TinyFitsHeader* header, const void* pixels,
                           const char* path, int interleaved);

/*
 * Similar to tinyfits_save, but writes to a buffer.
 * Free the returned buffer with tinyfits_free_buffer().
 *
 * On failure, *out_data is set to NULL.
 */
TFITSDEF int tinyfits_save_to_memory(TinyFitsHeader* header, const void* pixels,
                                     void** out_data, size_t* out_size,
                                     int interleaved);

#ifdef __cplusplus
}
#endif

// implementation

#ifdef TINYFITS_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

// Custom allocator: define all four macros, or none.
#if defined(TINYFITS_MALLOC) || defined(TINYFITS_CALLOC) || \
    defined(TINYFITS_REALLOC) || defined(TINYFITS_FREE)
    #if !defined(TINYFITS_MALLOC) || !defined(TINYFITS_CALLOC) || \
        !defined(TINYFITS_REALLOC) || !defined(TINYFITS_FREE)
        #error "Define all of TINYFITS_MALLOC, TINYFITS_CALLOC, TINYFITS_REALLOC, TINYFITS_FREE, or none."
    #endif
#else
    #define TINYFITS_MALLOC(sz)         malloc(sz)
    #define TINYFITS_CALLOC(cnt, sz)    calloc((cnt), (sz))
    #define TINYFITS_REALLOC(p, sz)     realloc((p), (sz))
    #define TINYFITS_FREE(p)            free(p)
#endif

// Runaway-detection guards for malformed files.
#define TINYFITS_MAX_HDUS           128
#define TINYFITS_MAX_HEADER_BLOCKS  1024

#ifdef _MSC_VER
    #define TINYFITS_BSWAP16(x) _byteswap_ushort(x)
    #define TINYFITS_BSWAP32(x) _byteswap_ulong(x)
    #define TINYFITS_BSWAP64(x) _byteswap_uint64(x)
#else
    #define TINYFITS_BSWAP16(x) __builtin_bswap16(x)
    #define TINYFITS_BSWAP32(x) __builtin_bswap32(x)
    #define TINYFITS_BSWAP64(x) __builtin_bswap64(x)
#endif

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    #define TINYFITS_IS_BIG_ENDIAN 1
#elif defined(_MSC_VER)
    #define TINYFITS_IS_BIG_ENDIAN 0
#else
    #define TINYFITS_IS_BIG_ENDIAN 0
#endif

/* TINYFITS_TO_BE* maps to a real byteswap on little-endian, identity on
 * big-endian. Lets the block helpers below be written with no in-body
 * #if scaffolding.
 */
#if TINYFITS_IS_BIG_ENDIAN
    #define TINYFITS_TO_BE16(x) (x)
    #define TINYFITS_TO_BE32(x) (x)
    #define TINYFITS_TO_BE64(x) (x)
#else
    #define TINYFITS_TO_BE16(x) TINYFITS_BSWAP16(x)
    #define TINYFITS_TO_BE32(x) TINYFITS_BSWAP32(x)
    #define TINYFITS_TO_BE64(x) TINYFITS_BSWAP64(x)
#endif

static int tinyfits__bytes_per_sample(int pixel_type)
{
    switch (pixel_type)
    {
        case TINYFITS_UINT8:   return 1;
        case TINYFITS_INT16:   return 2;
        case TINYFITS_UINT16:  return 2;
        case TINYFITS_INT32:   return 4;
        case TINYFITS_UINT32:  return 4;
        case TINYFITS_FLOAT32: return 4;
        case TINYFITS_FLOAT64: return 8;
        default:               return 0;
    }
}

// Maximum length of a HIERARCH long key.
#define TINYFITS_HIERARCH_KEY_MAX 63

/* Buffer size for parser-side key storage. Sized to hold the longest
 * HIERARCH key the parser may produce; standard 8-char keys also fit.
 */
#define TINYFITS_PARSE_KEY_BUF (TINYFITS_HIERARCH_KEY_MAX + 1)

/* Allocator-aware strdup. Routes through TINYFITS_MALLOC so custom
 * allocators apply uniformly to all keyword strings.
 */
static char* tinyfits__strdup(const char* s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* p = (char*)TINYFITS_MALLOC(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* Free every per-keyword allocation and the array itself, leaving
 * header's keyword fields zeroed. Used by tinyfits_free_header and by the HDU
 * walker between non-image and image HDUs.
 */
static void tinyfits__free_keywords(TinyFitsHeader* header)
{
    for (int i = 0; i < header->num_keywords; i++)
    {
        TINYFITS_FREE(header->keywords[i].key);
        TINYFITS_FREE(header->keywords[i].value);
        TINYFITS_FREE(header->keywords[i].comment);
    }
    TINYFITS_FREE(header->keywords);
    header->keywords = NULL;
    header->num_keywords = 0;
    header->_keywords_capacity = 0;
}

/* Validate that s contains only ASCII printable characters.
 * NULL is treated as empty (valid). Returns 1 if valid, 0 otherwise.
 */
static int tinyfits__is_ascii_printable(const char* s)
{
    if (!s) return 1;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++)
    {
        if (*p < 0x20 || *p > 0x7E) return 0;
    }
    return 1;
}

/* Standard 8-char key vs HIERARCH-class key dispatch.
 *
 * A lookup key is HIERARCH-class if it contains a space or is longer
 * than 8 chars. Standard keys match case-insensitively, while HIERARCH
 * keys match case-sensitively.
 */
static int tinyfits__is_hierarch_class(const char* key)
{
    int len = 0;
    for (const char* p = key; *p; p++)
    {
        if (*p == ' ') return 1;
        len++;
    }
    return len > 8;
}

static char tinyfits__upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* Normalize a HIERARCH-class key into the caller's buffer.
 * Reads bytes in [in, in_end). If in_end is NULL, treats `in` as a
 * NULL-terminated string. Trims leading/trailing spaces and collapses
 * runs of internal whitespace to a single space.
 */
static int tinyfits__normalize_hierarch_key(const char* in, const char* in_end,
                                            char* out, size_t out_size)
{
    if (!in_end) in_end = in + strlen(in);
    while (in < in_end && *in == ' ') in++;
    while (in_end > in && *(in_end - 1) == ' ') in_end--;

    size_t oi = 0;
    int last_was_space = 0;
    for (const char* p = in; p < in_end; p++)
    {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c > 0x7E || c == '=' || c == '\'')
            return TINYFITS_ERR_INVALID;
        if (c == ' ')
        {
            if (last_was_space) continue;
            last_was_space = 1;
        }
        else
        {
            last_was_space = 0;
        }
        if (oi + 1 >= out_size) return TINYFITS_ERR_KEYWORD_LENGTH;
        out[oi++] = (char)c;
    }
    if (oi == 0) return TINYFITS_ERR_INVALID;
    out[oi] = '\0';
    return TINYFITS_OK;
}

/* Compare a lookup key against a stored key under the matching rule.
 * Returns 0 on match, non-zero otherwise.
 */
static int tinyfits__keys_match(const char* lookup, const char* stored)
{
    if (tinyfits__is_hierarch_class(lookup))
    {
        char norm[TINYFITS_PARSE_KEY_BUF];
        if (tinyfits__normalize_hierarch_key(lookup, NULL, norm, sizeof(norm))
            != TINYFITS_OK)
            return -1;  // malformed lookup; can't match
        return strcmp(norm, stored);
    }

    while (*lookup && *stored)
    {
        char a = tinyfits__upper(*lookup++);
        char b = tinyfits__upper(*stored++);
        if (a != b) return (int)(unsigned char)a - (int)(unsigned char)b;
    }
    return (int)(unsigned char)*lookup - (int)(unsigned char)*stored;
}

/* Append a new keyword entry. Returns 1 on success, 0 on failure. */
static int tinyfits__add_header(TinyFitsHeader* header, const char* key,
                                const char* value, const char* comment)
{
    if (header->num_keywords >= header->_keywords_capacity)
    {
        if (header->_keywords_capacity > INT_MAX / 2) return 0;
        int new_cap = header->_keywords_capacity ? header->_keywords_capacity * 2 : 32;
        if ((size_t)new_cap > SIZE_MAX / sizeof(TinyFitsKeyword)) return 0;
        TinyFitsKeyword* p = (TinyFitsKeyword*)TINYFITS_REALLOC(
            header->keywords, (size_t)new_cap * sizeof(TinyFitsKeyword));
        if (!p) return 0;
        header->keywords = p;
        header->_keywords_capacity = new_cap;
    }

    char* k = tinyfits__strdup(key);
    char* v = tinyfits__strdup(value ? value : "");
    char* c = tinyfits__strdup(comment ? comment : "");
    if (!k || !v || !c)
    {
        TINYFITS_FREE(k);
        TINYFITS_FREE(v);
        TINYFITS_FREE(c);
        return 0;
    }

    TinyFitsKeyword* h = &header->keywords[header->num_keywords];
    h->key = k;
    h->value = v;
    h->comment = c;
    header->num_keywords++;
    return 1;
}

/* Merge a CONTINUE card's parsed value (and optional comment) into the
 * most recently pushed keyword. Caller must verify the previous
 * keyword's value ends with '&' before calling.
 *
 * Returns 1 on success, 0 on allocation failure.
 */
static int tinyfits__continue_merge(TinyFitsHeader* header, const char* chunk,
                                    const char* comment)
{
    TinyFitsKeyword* last = &header->keywords[header->num_keywords - 1];
    size_t lvlen = strlen(last->value);
    size_t cvlen = strlen(chunk);
    size_t new_len = (lvlen - 1) + cvlen;

    char* new_value = (char*)TINYFITS_MALLOC(new_len + 1);
    if (!new_value) return 0;
    memcpy(new_value, last->value, lvlen - 1);
    memcpy(new_value + lvlen - 1, chunk, cvlen);
    new_value[new_len] = '\0';

    char* new_comment = NULL;
    if (comment && comment[0])
    {
        new_comment = tinyfits__strdup(comment);
        if (!new_comment)
        {
            TINYFITS_FREE(new_value);
            return 0;
        }
    }

    TINYFITS_FREE(last->value);
    last->value = new_value;
    if (new_comment)
    {
        TINYFITS_FREE(last->comment);
        last->comment = new_comment;
    }
    return 1;
}

TFITSDEF void tinyfits_free_header(TinyFitsHeader* header)
{
    if (!header) return;
    tinyfits__free_keywords(header);
    header->width = 0;
    header->height = 0;
    header->num_channels = 0;
    header->bitpix = 0;
    header->pixel_type = TINYFITS_UNKNOWN;
    header->bscale = 0.0;
    header->bzero = 0.0;
}

TFITSDEF void tinyfits_free_buffer(void* buf)
{
    TINYFITS_FREE(buf);
}

TFITSDEF const TinyFitsKeyword* tinyfits_get_keyword(const TinyFitsHeader* header,
                                                     const char* key)
{
    if (!header || !key) return NULL;
    for (int i = 0; i < header->num_keywords; i++)
    {
        if (tinyfits__keys_match(key, header->keywords[i].key) == 0)
            return &header->keywords[i];
    }
    return NULL;
}

TFITSDEF size_t tinyfits_image_size(const TinyFitsHeader* header)
{
    if (!header || header->width <= 0 || header->height <= 0 || header->num_channels <= 0)
        return 0;
    int bps = tinyfits__bytes_per_sample(header->pixel_type);
    if (bps == 0) return 0;
    return (size_t)header->width * (size_t)header->height
         * (size_t)header->num_channels * (size_t)bps;
}

// In-place block byte-swap helpers (no-ops on big-endian).

static void tinyfits__bswap16_block(void* buf, size_t n)
{
    uint16_t* p = (uint16_t*)buf;
    for (size_t i = 0; i < n; i++) p[i] = TINYFITS_TO_BE16(p[i]);
}

static void tinyfits__bswap32_block(void* buf, size_t n)
{
    uint32_t* p = (uint32_t*)buf;
    for (size_t i = 0; i < n; i++) p[i] = TINYFITS_TO_BE32(p[i]);
}

static void tinyfits__bswap64_block(void* buf, size_t n)
{
    uint64_t* p = (uint64_t*)buf;
    for (size_t i = 0; i < n; i++) p[i] = TINYFITS_TO_BE64(p[i]);
}

/* In-place byteswap then XOR sign bit: maps signed big-endian int16 on
 * disk to native uint16.
 */
static void tinyfits__bswap16_xor_block(void* buf, size_t n)
{
    uint16_t* p = (uint16_t*)buf;
    for (size_t i = 0; i < n; i++)
        p[i] = (uint16_t)(TINYFITS_TO_BE16(p[i]) ^ 0x8000u);
}

static void tinyfits__bswap32_xor_block(void* buf, size_t n)
{
    uint32_t* p = (uint32_t*)buf;
    for (size_t i = 0; i < n; i++)
        p[i] = TINYFITS_TO_BE32(p[i]) ^ 0x80000000u;
}

/* In-place XOR sign bit then byteswap to big-endian: maps native uint16
 * to canonical signed-BE int16 disk encoding.
 */
static void tinyfits__xor_bswap16_block(void* buf, size_t n)
{
    uint16_t* p = (uint16_t*)buf;
    for (size_t i = 0; i < n; i++)
        p[i] = TINYFITS_TO_BE16((uint16_t)(p[i] ^ 0x8000u));
}

static void tinyfits__xor_bswap32_block(void* buf, size_t n)
{
    uint32_t* p = (uint32_t*)buf;
    for (size_t i = 0; i < n; i++)
        p[i] = TINYFITS_TO_BE32(p[i] ^ 0x80000000u);
}

/* Parse the value (and optional trailing comment) portion of a card,
 * starting at vstart.
 *
 * value and comment must each be at least TINYFITS_CARD_VALUE_MAX_LEN+1
 * bytes; both are NUL-terminated on return.
 */
static void tinyfits__parse_value(const char* vstart, const char* card_end,
                                  char* value, char* comment)
{
    value[0] = '\0';
    comment[0] = '\0';

    while (vstart < card_end && *vstart == ' ')
        vstart++;
    if (vstart >= card_end)
        return;

    if (*vstart == '\'')
    {
        // String value in single quotes. '' is an escaped quote.
        vstart++;
        int vi = 0;
        while (vstart < card_end && vi < TINYFITS_CARD_VALUE_MAX_LEN)
        {
            if (*vstart == '\'')
            {
                if (vstart + 1 < card_end && *(vstart + 1) == '\'')
                {
                    value[vi++] = '\'';
                    vstart += 2;
                }
                else
                {
                    vstart++;
                    break;
                }
            }
            else
            {
                value[vi++] = *vstart++;
            }
        }
        while (vi > 0 && value[vi - 1] == ' ')
            vi--;
        value[vi] = '\0';

        // Comment: skip spaces and '/' after closing quote
        while (vstart < card_end && *vstart == ' ')
            vstart++;
        if (vstart < card_end && *vstart == '/')
        {
            vstart++;
            while (vstart < card_end && *vstart == ' ')
                vstart++;
            int ci = 0;
            while (vstart < card_end && ci < TINYFITS_CARD_VALUE_MAX_LEN)
                comment[ci++] = *vstart++;
            while (ci > 0 && comment[ci - 1] == ' ')
                ci--;
            comment[ci] = '\0';
        }
    }
    else
    {
        // Numeric or logical value: up to ' /' comment delimiter
        const char* cstart = NULL;
        const char* p = vstart;
        while (p < card_end)
        {
            if (*p == '/' && p > vstart)
            {
                cstart = p + 1;
                break;
            }
            p++;
        }

        int vi = 0;
        const char* vend = cstart ? (cstart - 1) : card_end;
        while (vstart < vend && vi < TINYFITS_CARD_VALUE_MAX_LEN)
            value[vi++] = *vstart++;
        while (vi > 0 && value[vi - 1] == ' ')
            vi--;
        value[vi] = '\0';

        if (cstart)
        {
            while (cstart < card_end && *cstart == ' ')
                cstart++;
            int ci = 0;
            while (cstart < card_end && ci < TINYFITS_CARD_VALUE_MAX_LEN)
                comment[ci++] = *cstart++;
            while (ci > 0 && comment[ci - 1] == ' ')
                ci--;
            comment[ci] = '\0';
        }
    }
}

/* Parse a single header card.
 *
 * key buffer must be at least TINYFITS_PARSE_KEY_BUF bytes.
 * value and comment must each be at least TINYFITS_CARD_VALUE_MAX_LEN+1.
 */
static int tinyfits__parse_card(const char* card, char* key,
                                char* value, char* comment)
{
    const char* card_end = card + TINYFITS_CARD_SIZE;

    // Key: first TINYFITS_CARD_KEY_LEN bytes, trim trailing spaces.
    memcpy(key, card, TINYFITS_CARD_KEY_LEN);
    key[TINYFITS_CARD_KEY_LEN] = '\0';
    int ki = TINYFITS_CARD_KEY_LEN - 1;
    while (ki >= 0 && key[ki] == ' ')
        ki--;
    key[ki + 1] = '\0';

    value[0] = '\0';
    comment[0] = '\0';

    // HIERARCH long-key cards. Convention requires a space immediately
    // after "HIERARCH"; without it, treat as a standard 8-char keyword
    // named "HIERARCH".
    if (strcmp(key, "HIERARCH") == 0 && card[TINYFITS_CARD_KEY_LEN] == ' ')
    {
        // Skip separator spaces between "HIERARCH" and the long key.
        const char* p = card + TINYFITS_CARD_KEY_LEN;
        while (p < card_end && *p == ' ') p++;
        if (p >= card_end)
        {
            key[0] = '\0';  // nothing past HIERARCH; treat as blank
            return TINYFITS_OK;
        }

        // Locate separator. ' = ' (space-equals-space) is canonical;
        // fall back to a bare '=' anywhere on the card.
        const char* sep = NULL;
        int sep_consume = 1;
        for (const char* q = p; q + 2 < card_end; q++)
        {
            if (*q == ' ' && *(q + 1) == '=' && *(q + 2) == ' ')
            {
                sep = q;
                sep_consume = 3;
                break;
            }
        }
        if (!sep)
        {
            for (const char* q = p; q < card_end; q++)
            {
                if (*q == '=')
                {
                    sep = q;
                    sep_consume = 1;
                    break;
                }
            }
        }
        if (!sep) return TINYFITS_ERR_INVALID;

        // Normalize bytes [p, sep) into key[]: collapse internal whitespace
        // runs, validate character class, trim leading/trailing spaces.
        int nerr = tinyfits__normalize_hierarch_key(p, sep, key,
                                                    TINYFITS_PARSE_KEY_BUF);
        if (nerr != TINYFITS_OK) return nerr;
        size_t klen = strlen(key);

        // Reject short-no-space HIERARCH keys (<= 8 chars, no embedded
        // space): such keys are ambiguous with the standard 8-char
        // namespace.
        if (klen <= 8 && !strchr(key, ' '))
            return TINYFITS_ERR_INVALID;

        // Parse value at the post-separator position.
        const char* vstart = sep + sep_consume;
        while (vstart < card_end && *vstart == ' ') vstart++;
        tinyfits__parse_value(vstart, card_end, value, comment);
        return TINYFITS_OK;
    }

    // HISTORY and COMMENT cards: no '= ' separator, free-form text
    // starting at byte CARD_KEY_LEN.
    if (strcmp(key, "HISTORY") == 0 || strcmp(key, "COMMENT") == 0)
    {
        const char* text = card + TINYFITS_CARD_KEY_LEN;
        int vi = 0;
        while (text < card_end && vi < TINYFITS_HISTORY_PAYLOAD)
            value[vi++] = *text++;
        while (vi > 0 && value[vi - 1] == ' ')
            vi--;
        value[vi] = '\0';
        return TINYFITS_OK;
    }

    // CONTINUE cards: no '= ' indicator, but the payload starts at the
    // standard value column with a quoted string.
    if (strcmp(key, "CONTINUE") == 0)
    {
        tinyfits__parse_value(card + TINYFITS_CARD_VALUE_OFFSET, card_end,
                              value, comment);
        return TINYFITS_OK;
    }

    // Value cards: '= ' at bytes 8-9.
    if (card[TINYFITS_CARD_KEY_LEN] == '=')
    {
        tinyfits__parse_value(card + TINYFITS_CARD_VALUE_OFFSET, card_end,
                              value, comment);
        return TINYFITS_OK;
    }

    // No '= ' indicator and the key isn't one of the special branches
    // above. Fall back to the same free-form text parse used for
    // HISTORY/COMMENT, so post-key bytes are preserved as the value
    // rather than silently dropped.
    {
        const char* text = card + TINYFITS_CARD_KEY_LEN;
        int vi = 0;
        while (text < card_end && vi < TINYFITS_CARD_VALUE_MAX_LEN)
            value[vi++] = *text++;
        while (vi > 0 && value[vi - 1] == ' ')
            vi--;
        value[vi] = '\0';
    }
    return TINYFITS_OK;
}

/* Returns 1 if (bitpix, bscale, bzero) matches one of the canonical
 * unsigned-int storage cases (BITPIX=16 with BSCALE=1, BZERO=32768; or
 * BITPIX=32 with BSCALE=1, BZERO=2147483648), 0 otherwise.
 */
static int tinyfits__is_unsigned_conversion(int bitpix, double bscale, double bzero)
{
    if (bscale != 1.0) return 0;
    if (bitpix == 16 && bzero == 32768.0) return 1;
    if (bitpix == 32 && bzero == 2147483648.0) return 1;
    return 0;
}

/* Resolve pixel_type from BITPIX + BZERO/BSCALE
 *
 * Validation order:
 *   1. BITPIX must be one of {8, 16, 32, -32, -64}.
 *   2. BSCALE and BZERO must be finite.
 *   3. Detect unsigned-integer-conversion shortcut.
 *   4. Otherwise pixel_type follows BITPIX literally.
 */
static int tinyfits__resolve_pixel_type(int bitpix, double bzero, double bscale,
                                        int* pixel_type)
{
    switch (bitpix)
    {
        case 8: case 16: case 32: case -32: case -64:
            break;
        default:
            return TINYFITS_ERR_BITPIX;
    }

    if (!isfinite(bscale) || !isfinite(bzero))
        return TINYFITS_ERR_BZERO_BSCALE;

    if (tinyfits__is_unsigned_conversion(bitpix, bscale, bzero))
    {
        *pixel_type = (bitpix == 16) ? TINYFITS_UINT16 : TINYFITS_UINT32;
        return TINYFITS_OK;
    }

    switch (bitpix)
    {
        case 8:    *pixel_type = TINYFITS_UINT8;   break;
        case 16:   *pixel_type = TINYFITS_INT16;   break;
        case 32:   *pixel_type = TINYFITS_INT32;   break;
        case -32:  *pixel_type = TINYFITS_FLOAT32; break;
        case -64:  *pixel_type = TINYFITS_FLOAT64; break;
    }
    return TINYFITS_OK;
}

/* Sentinel returned by tinyfits__hdu_data_block_size when the size cannot
 * be safely computed.
 */
#define TINYFITS_HDU_SIZE_UNKNOWN  ((size_t)-1)

/* Compute header width * height * channels with overflow detection.
 */
static size_t tinyfits__num_samples(const TinyFitsHeader* header)
{
    if (!header || header->width <= 0 || header->height <= 0 || header->num_channels <= 0) return 0;
    size_t a = (size_t)header->width, b = (size_t)header->height, cc = (size_t)header->num_channels;
    if (a > SIZE_MAX / b) return 0;
    size_t ab = a * b;
    if (ab > SIZE_MAX / cc) return 0;
    return ab * cc;
}

/* Parse a FITS keyword integer value, clamping out-of-range or malformed
 * input to INT_MIN/INT_MAX. Avoids the UB that atoi() invokes on overflow.
 */
static int tinyfits__parse_int(const char* s)
{
    long v = strtol(s, NULL, 10);
    if (v > INT_MAX) return INT_MAX;
    if (v < INT_MIN) return INT_MIN;
    return (int)v;
}

// Parsed structural metadata for a single HDU. Internal-only.

typedef struct
{
    int    bitpix;
    int    naxis;
    int    naxis_vals[3];
    size_t naxis_product;       // product of every NAXISn value seen
    int    naxis_seen;          // count of NAXISn cards encountered
    int    naxis_size_unknown;  // set if any NAXISn was negative or product overflowed
    double bzero;
    double bscale;
    int64_t pcount;
    int64_t gcount;
    char   xtension[9];   // empty for primary HDU
} TinyFitsHduMeta;

/* Compute data-block size for an HDU
 *
 * Per the FITS spec, the data unit size in bytes is:
 *   |BITPIX|/8 * GCOUNT * (PCOUNT + NAXIS1 * NAXIS2 * ... * NAXISn)
 *
 * For images, PCOUNT=0, GCOUNT=1, so this reduces to the pixel-product form.
 * For NAXIS=0 the data unit is absent.
 */
static size_t tinyfits__hdu_data_block_size(const TinyFitsHduMeta* meta)
{
    if (meta->naxis == 0) return 0;
    if (meta->naxis < 0) return TINYFITS_HDU_SIZE_UNKNOWN;
    if (meta->naxis_size_unknown) return TINYFITS_HDU_SIZE_UNKNOWN;
    if (meta->naxis_seen != meta->naxis) return TINYFITS_HDU_SIZE_UNKNOWN;

    size_t product = meta->naxis_product;
    if (product == 0) return 0;  // some NAXISn was 0 -> empty data unit

    int64_t bpp = (meta->bitpix < 0
                   ? -(int64_t)meta->bitpix
                   : (int64_t)meta->bitpix) / 8;
    if (bpp <= 0) return TINYFITS_HDU_SIZE_UNKNOWN;
    // Out-of-range PCOUNT/GCOUNT fall back to defaults (0/1).
    int64_t pcount = meta->pcount;
    int64_t gcount = meta->gcount;
    if (gcount <= 0) gcount = 1;
    if (pcount < 0) pcount = 0;

    size_t g = (size_t)gcount;
    size_t p = (size_t)pcount;
    if (product > SIZE_MAX - p) return TINYFITS_HDU_SIZE_UNKNOWN;
    size_t inner = product + p;
    if (inner > SIZE_MAX / g) return TINYFITS_HDU_SIZE_UNKNOWN;
    size_t outer = inner * g;
    if (outer > SIZE_MAX / (size_t)bpp) return TINYFITS_HDU_SIZE_UNKNOWN;
    return outer * (size_t)bpp;
}

/* NAXISn keyword suffix parser
 *
 * Returns n if key has the form "NAXISn" with n in [1, 999]; returns 0
 * for anything else.
 */
static int tinyfits__naxis_suffix(const char* key)
{
    static const size_t prefix_len = sizeof("NAXIS") - 1;
    if (strncmp(key, "NAXIS", prefix_len) != 0) return 0;
    const char* s = key + prefix_len;
    if (*s == '\0') return 0;  // the bare "NAXIS" key, not NAXISn
    int n = 0;
    for (; *s; s++)
    {
        if (*s < '0' || *s > '9') return 0;
        n = n * 10 + (*s - '0');
        if (n > 999) return 0;  // spec maximum is NAXIS999
    }
    return n >= 1 ? n : 0;
}

/* Parse a single HDU header from a contiguous buffer
 *
 * data must point to the start of the HDU header; first card should be
 * SIMPLE for primary, XTENSION for extensions. size is the number of
 * bytes available starting at data.
 */
static int tinyfits__parse_one_hdu_header(const uint8_t* data, size_t size,
                                          TinyFitsHduMeta* meta,
                                          TinyFitsHeader* header,
                                          size_t* header_bytes)
{
    memset(meta, 0, sizeof(*meta));
    meta->bscale = 1.0;
    meta->gcount = 1;
    meta->naxis_product = 1;
    *header_bytes = 0;

    const uint8_t* ptr = data;
    const uint8_t* end = data + size;
    int header_done = 0;
    int block_count = 0;

    while (ptr < end && !header_done)
    {
        if (++block_count > TINYFITS_MAX_HEADER_BLOCKS)
            return tf_fail(header, TINYFITS_ERR_HEADER_TOO_LARGE, "Header is larger than allowed by the spec");

        if ((size_t)(end - ptr) < TINYFITS_BLOCK_SIZE)
            return tf_fail(header, TINYFITS_ERR_TRUNCATED, "Header is truncated");

        for (int c = 0; c < TINYFITS_CARDS_PER_BLOCK; c++)
        {
            const char* card = (const char*)ptr + c * TINYFITS_CARD_SIZE;

            if (memcmp(card, "END     ", TINYFITS_CARD_KEY_LEN) == 0)
            {
                header_done = 1;
                break;
            }

            char key    [TINYFITS_PARSE_KEY_BUF]          = {0};
            char value  [TINYFITS_HISTORY_PAYLOAD + 1]    = {0};
            char comment[TINYFITS_CARD_VALUE_MAX_LEN + 1] = {0};
            int pc_err = tinyfits__parse_card(card, key, value, comment);
            if (pc_err != TINYFITS_OK)
                return tf_fail(header, pc_err, "Malformed header card");

            if (key[0] == '\0')
                continue;

            // CONTINUE chain assembly. If the previous keyword's value
            // ends with '&', merge this card's content into it.
            if (strcmp(key, "CONTINUE") == 0)
            {
                if (header && header->num_keywords > 0)
                {
                    TinyFitsKeyword* last =
                        &header->keywords[header->num_keywords - 1];
                    size_t lvlen = strlen(last->value);
                    if (lvlen > 0 && last->value[lvlen - 1] == '&')
                    {
                        if (!tinyfits__continue_merge(header, value, comment))
                            return tf_fail(header, TINYFITS_ERR_ALLOC, "Failed to allocate keyword value string.");
                    }
                }
                continue;
            }

            int naxis_n = tinyfits__naxis_suffix(key);

            if (strcmp(key, "BITPIX") == 0)
                meta->bitpix = tinyfits__parse_int(value);
            else if (strcmp(key, "NAXIS") == 0)
                meta->naxis = tinyfits__parse_int(value);
            else if (naxis_n > 0)
            {
                // NAXISn for n in [1, 999]. Per-axis values are stored
                // only for the first three (width, height, channels).
                //
                // NAXIS1=0 is the deprecated Random Groups sentinel; excluding it
                // here makes naxis_product equal the per-group sample count.
                int v = tinyfits__parse_int(value);
                meta->naxis_seen++;
                int random_groups_marker = (naxis_n == 1 && v == 0);
                if (v < 0)
                    meta->naxis_size_unknown = 1;
                else if (v > 0
                         && meta->naxis_product > SIZE_MAX / (size_t)v)
                    meta->naxis_size_unknown = 1;
                else if (!random_groups_marker)
                    meta->naxis_product *= (size_t)v;
                if (naxis_n <= 3) meta->naxis_vals[naxis_n - 1] = v;
            }
            else if (strcmp(key, "PCOUNT") == 0)
                meta->pcount = strtoll(value, NULL, 10);
            else if (strcmp(key, "GCOUNT") == 0)
                meta->gcount = strtoll(value, NULL, 10);
            else if (strcmp(key, "XTENSION") == 0)
            {
                int xl = 0;
                while (xl < 8 && value[xl] != '\0' && value[xl] != ' ')
                {
                    meta->xtension[xl] = value[xl];
                    xl++;
                }
                meta->xtension[xl] = '\0';
            }
            else if (strcmp(key, "BZERO") == 0)
            {
                meta->bzero = strtod(value, NULL);
                continue; // strip from keywords
            }
            else if (strcmp(key, "BSCALE") == 0)
            {
                meta->bscale = strtod(value, NULL);
                continue; // strip from keywords
            }

            // Fall-through: structural keywords are also added to the keyword array here.
            if (header && !tinyfits__add_header(header, key, value, comment))
                return tf_fail(header, TINYFITS_ERR_ALLOC, "Failed to allocate header keyword");
        }

        ptr += TINYFITS_BLOCK_SIZE;
    }

    if (!header_done)
        return tf_fail(header, TINYFITS_ERR_HEADER_NO_END, "Header does not contain an END card");

    // Note: a value still ending with '&' at HDU end is left as-is. The '&'
    // chain marker is consumed by continue_merge only when an actual CONTINUE
    // card follows; without one, the trailing byte is treated as data.

    *header_bytes = (size_t)(ptr - data);
    return TINYFITS_OK;
}

/* Finalize a parsed image HDU, populating header from meta.
 */
static int tinyfits__finalize_image_hdu(TinyFitsHeader* header,
                                        const TinyFitsHduMeta* meta)
{
    header->bitpix = meta->bitpix;
    header->width = meta->naxis_vals[0];
    header->height = meta->naxis_vals[1];
    header->num_channels = (meta->naxis == 3) ? meta->naxis_vals[2] : 1;

    if (header->width <= 0 || header->height <= 0 || header->num_channels <= 0)
        return tf_fail(header, TINYFITS_ERR_BAD_DIMENSION, "Header dimensions are invalid");

    int err = tinyfits__resolve_pixel_type(header->bitpix, meta->bzero,
                                           meta->bscale, &header->pixel_type);
    if (err == TINYFITS_ERR_BITPIX)
        return tf_fail(header, err, "BITPIX value is not a supported FITS type");
    if (err == TINYFITS_ERR_BZERO_BSCALE)
        return tf_fail(header, err, "BSCALE or BZERO is not finite");
    if (err != TINYFITS_OK)
        return tf_fail(header, err, "Failed to resolve pixel type");

    // Unsigned-conversion shortcut bakes the offset into the in-memory
    // pixels, so record 1.0/0.0 to signal no further transform needed.
    int unsigned_shortcut = tinyfits__is_unsigned_conversion(
        header->bitpix, meta->bscale, meta->bzero);
    header->bscale = unsigned_shortcut ? 1.0 : meta->bscale;
    header->bzero  = unsigned_shortcut ? 0.0 : meta->bzero;

    return TINYFITS_OK;
}

/* Byte-source abstraction for the HDU walker
 *
 * Abstracts how bytes are obtained for the in-memory and from-file HDU walkers.
 */
typedef enum { TINYFITS__READER_MEM, TINYFITS__READER_FILE } TinyFitsReaderKind;

typedef struct
{
    TinyFitsReaderKind kind;
    union
    {
        struct { const uint8_t* cur; const uint8_t* end; } mem;
        FILE* file;
    } src;
} TinyFitsReader;

/* Read up to n bytes into dst. Returns the number of bytes actually read. */
static size_t tinyfits__reader_read(TinyFitsReader* r, void* dst, size_t n)
{
    if (r->kind == TINYFITS__READER_MEM)
    {
        size_t avail = (size_t)(r->src.mem.end - r->src.mem.cur);
        size_t got = (avail < n) ? avail : n;
        memcpy(dst, r->src.mem.cur, got);
        r->src.mem.cur += got;
        return got;
    }
    return fread(dst, 1, n, r->src.file);
}

/* Skip n bytes without reading them. Returns 1 on success, 0 on failure. */
static int tinyfits__reader_skip(TinyFitsReader* r, size_t n)
{
    if (r->kind == TINYFITS__READER_MEM)
    {
        size_t avail = (size_t)(r->src.mem.end - r->src.mem.cur);
        if (n > avail) return 0;
        r->src.mem.cur += n;
        return 1;
    }
#ifdef _MSC_VER
    return _fseeki64(r->src.file, (int64_t)n, SEEK_CUR) == 0;
#else
    return fseeko(r->src.file, (off_t)n, SEEK_CUR) == 0;
#endif
}

/* Walk HDUs via a reader, returning the first image HDU.
 *
 * On success, header is populated with the image HDU's metadata and the
 * reader is positioned at the first byte of that HDU's pixel data.
 */
static int tinyfits__walk_to_image(TinyFitsReader* reader, TinyFitsHeader* header)
{
    memset(header, 0, sizeof(TinyFitsHeader));

    // Seed the per-HDU header buffer at 4 blocks.
    size_t hdr_capacity = TINYFITS_BLOCK_SIZE * 4;
    uint8_t* hdr_buf = (uint8_t*)TINYFITS_MALLOC(hdr_capacity);
    if (!hdr_buf) return tf_fail(header, TINYFITS_ERR_ALLOC, "Failed to allocate header buffer");

    int err = TINYFITS_OK;

    for (int hdu = 0; hdu < TINYFITS_MAX_HDUS; hdu++)
    {
        // Read this HDU's header blocks until END is seen.
        size_t hdr_len = 0;
        int found_end = 0;
        int block_count = 0;

        while (!found_end)
        {
            if (++block_count > TINYFITS_MAX_HEADER_BLOCKS)
            {
                err = tf_fail(header, TINYFITS_ERR_HEADER_TOO_LARGE, "Header exceeds maximum block count");
                goto done;
            }

            if (hdr_len + TINYFITS_BLOCK_SIZE > hdr_capacity)
            {
                size_t new_cap = hdr_capacity * 2;
                uint8_t* new_buf = (uint8_t*)TINYFITS_REALLOC(hdr_buf, new_cap);
                if (!new_buf)
                {
                    err = tf_fail(header, TINYFITS_ERR_ALLOC, "Failed to grow header buffer");
                    goto done;
                }
                hdr_buf = new_buf;
                hdr_capacity = new_cap;
            }

            size_t nread = tinyfits__reader_read(reader,
                                                 hdr_buf + hdr_len,
                                                 TINYFITS_BLOCK_SIZE);
            if (nread != TINYFITS_BLOCK_SIZE)
            {
                if (hdu == 0 && hdr_len == 0)
                    err = tf_fail(header, TINYFITS_ERR_NOT_FITS, "Input is shorter than one FITS block");
                else if (nread == 0)
                    err = tf_fail(header, TINYFITS_ERR_NO_IMAGE, "No image HDU found");
                else
                    err = tf_fail(header, TINYFITS_ERR_TRUNCATED, "Input ends mid-block");
                goto done;
            }

            if (hdr_len == 0)
            {
                if (hdu == 0)
                {
                    if (memcmp(hdr_buf, "SIMPLE  =", 9) != 0)
                    {
                        err = tf_fail(header, TINYFITS_ERR_NOT_FITS, "Primary HDU does not begin with SIMPLE");
                        goto done;
                    }
                }
                else
                {
                    if (memcmp(hdr_buf, "XTENSION=", 9) != 0)
                    {
                        err = tf_fail(header, TINYFITS_ERR_NO_IMAGE, "Extension HDU does not begin with XTENSION");
                        goto done;
                    }
                }
            }
            hdr_len += TINYFITS_BLOCK_SIZE;

            const char* block = (const char*)(hdr_buf + hdr_len
                                              - TINYFITS_BLOCK_SIZE);
            for (int c = 0; c < TINYFITS_CARDS_PER_BLOCK; c++)
            {
                if (memcmp(block + c * TINYFITS_CARD_SIZE,
                           "END     ", TINYFITS_CARD_KEY_LEN) == 0)
                {
                    found_end = 1;
                    break;
                }
            }
        }

        // Reset accumulated keyword state before parsing this HDU's header.
        tinyfits__free_keywords(header);
        header->bitpix = 0;

        TinyFitsHduMeta meta;
        size_t header_bytes;
        err = tinyfits__parse_one_hdu_header(hdr_buf, hdr_len, &meta,
                                             header, &header_bytes);
        if (err != TINYFITS_OK) goto done;

        int is_image = (meta.naxis == 2 || meta.naxis == 3) &&
                       meta.naxis_vals[0] != 0 &&
                       (hdu == 0 || strcmp(meta.xtension, "IMAGE") == 0);
        if (is_image)
        {
            err = tinyfits__finalize_image_hdu(header, &meta);
            goto done;
        }

        // Not an image: skip past this HDU's data block.
        size_t data_size = tinyfits__hdu_data_block_size(&meta);
        if (data_size == TINYFITS_HDU_SIZE_UNKNOWN)
        {
            err = tf_fail(header, TINYFITS_ERR_INVALID, "Cannot determine non-image HDU data block size");
            goto done;
        }
        size_t padded = ((data_size + TINYFITS_BLOCK_SIZE - 1)
                         / TINYFITS_BLOCK_SIZE) * TINYFITS_BLOCK_SIZE;
        if (padded > 0 && !tinyfits__reader_skip(reader, padded))
        {
            err = tf_fail(header, TINYFITS_ERR_TRUNCATED, "Non-image HDU is truncated");
            goto done;
        }
    }

    err = tf_fail(header, TINYFITS_ERR_NO_IMAGE, "Maximum HDU count reached without finding an image");

done:
    TINYFITS_FREE(hdr_buf);
    if (err != TINYFITS_OK) tinyfits_free_header(header);
    return err;
}

/* Walk HDUs in an in-memory buffer.
 *
 * On success, header is populated with the image HDU's metadata and the
 * returned pointer points at the first byte of pixel data.
 */
static const uint8_t* tinyfits__parse_headers(TinyFitsHeader* header,
                                              const void* data, size_t size,
                                              int* err)
{
    TinyFitsReader reader;
    reader.kind = TINYFITS__READER_MEM;
    reader.src.mem.cur = (const uint8_t*)data;
    reader.src.mem.end = (const uint8_t*)data + size;

    *err = tinyfits__walk_to_image(&reader, header);
    if (*err != TINYFITS_OK) return NULL;
    // Walker advanced reader.src.mem.cur past all consumed header
    // blocks; it now points at the first byte of pixel data.
    return reader.src.mem.cur;
}

// Read entire file into memory

static int tinyfits__read_file(TinyFitsHeader* header, const char* path,
                               void** out_data, size_t* out_size)
{
    FILE* f = fopen(path, "rb");
    if (!f) return tf_fail(header, TINYFITS_ERR_OPEN, "Failed to open input file");

#ifdef _MSC_VER
    _fseeki64(f, 0, SEEK_END);
    int64_t file_size = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
#else
    fseeko(f, 0, SEEK_END);
    int64_t file_size = (int64_t)ftello(f);
    fseeko(f, 0, SEEK_SET);
#endif

    if (file_size <= 0)
    {
        fclose(f);
        return tf_fail(header, TINYFITS_ERR_READ, "Input file is empty or unreadable");
    }
    if ((uint64_t)file_size > (uint64_t)SIZE_MAX)
    {
        fclose(f);
        return tf_fail(header, TINYFITS_ERR_READ, "Input file too large for address space");
    }

    void* data = TINYFITS_MALLOC((size_t)file_size);
    if (!data)
    {
        fclose(f);
        return tf_fail(header, TINYFITS_ERR_ALLOC, "Failed to allocate input file buffer");
    }

    if (fread(data, 1, (size_t)file_size, f) != (size_t)file_size)
    {
        TINYFITS_FREE(data);
        fclose(f);
        return tf_fail(header, TINYFITS_ERR_READ, "Failed to read input file contents");
    }
    fclose(f);

    *out_data = data;
    *out_size = (size_t)file_size;
    return TINYFITS_OK;
}

TFITSDEF int tinyfits_load_header_from_memory(TinyFitsHeader* header, const void* data, size_t size)
{
    if (!header || !data) return TINYFITS_ERR_NULL_ARG;

    int err;
    tinyfits__parse_headers(header, data, size, &err);
    if (err != TINYFITS_OK)
        tinyfits_free_header(header);
    return err;
}

TFITSDEF int tinyfits_load_header(TinyFitsHeader* header, const char* path)
{
    if (!header || !path) return TINYFITS_ERR_NULL_ARG;

    // Streams HDUs incrementally via fread for headers + fseek past
    // data blocks. Cost is O(headers); pixel data is never read.
    FILE* f = fopen(path, "rb");
    if (!f) return tf_fail(header, TINYFITS_ERR_OPEN, "Failed to open input file");

    TinyFitsReader reader;
    reader.kind = TINYFITS__READER_FILE;
    reader.src.file = f;

    int err = tinyfits__walk_to_image(&reader, header);
    fclose(f);
    return err;
}

TFITSDEF int tinyfits_get_keywords(const TinyFitsHeader* header, const char* key,
                                   const TinyFitsKeyword** out, int max)
{
    if (!header || !key) return 0;
    int count = 0;
    for (int i = 0; i < header->num_keywords; i++)
    {
        if (tinyfits__keys_match(key, header->keywords[i].key) == 0)
        {
            if (out && count < max)
                out[count] = &header->keywords[i];
            count++;
        }
    }
    return count;
}

/* Return true if key is reserved. */
static int tinyfits__is_reserved_key(const char* key)
{
    if (strcmp(key, "SIMPLE") == 0) return 1;
    if (strcmp(key, "BITPIX") == 0) return 1;
    if (strcmp(key, "NAXIS") == 0) return 1;
    if (strcmp(key, "EXTEND") == 0) return 1;
    if (strcmp(key, "END") == 0) return 1;
    if (strcmp(key, "BZERO") == 0) return 1;
    if (strcmp(key, "BSCALE") == 0) return 1;
    if (strcmp(key, "XTENSION") == 0) return 1;
    if (strcmp(key, "PCOUNT") == 0) return 1;
    if (strcmp(key, "GCOUNT") == 0) return 1;
    if (tinyfits__naxis_suffix(key) > 0) return 1;
    return 0;
}

/* Validate keyword fields. On success emits the canonical key into
 * out_buf (HIERARCH normalized; standard as-is) and writes its address
 * to *out_canonical. out_buf size must be at least TINYFITS_PARSE_KEY_BUF.
 */
static int tinyfits__validate_header_fields(TinyFitsHeader* header,
                                            const char* key, const char* value,
                                            const char* comment,
                                            const char** out_canonical,
                                            char* out_buf, size_t out_buf_size)
{
    if (!key) return tf_fail(header, TINYFITS_ERR_NULL_ARG, "Keyword name is null");
    if (!tinyfits__is_ascii_printable(key))
        return tf_fail(header, TINYFITS_ERR_INVALID, "Keyword name contains non-printable characters");
    if (!tinyfits__is_ascii_printable(value))
        return tf_fail(header, TINYFITS_ERR_INVALID, "Keyword value contains non-printable characters");
    if (!tinyfits__is_ascii_printable(comment))
        return tf_fail(header, TINYFITS_ERR_INVALID, "Keyword comment contains non-printable characters");

    // "HIERARCH " prefix is reserved for the writer.
    if (strncmp(key, "HIERARCH ", 9) == 0)
        return tf_fail(header, TINYFITS_ERR_KEYWORD_LENGTH, "Keyword name with literal HIERARCH prefix is reserved");

    // The reserved-key set (SIMPLE, BITPIX, NAXIS, NAXISn, ...) lives in
    // the standard 8-char namespace. HIERARCH-class keys (containing a
    // space OR length > 8) are a different namespace and can never
    // collide on disk.
    if (tinyfits__is_hierarch_class(key))
    {
        // HIERARCH-class. Reject '=' or "'" anywhere in the raw input;
        // those are reserved (separator and quote).
        for (const char* p = key; *p; p++)
            if (*p == '=' || *p == '\'')
                return tf_fail(header, TINYFITS_ERR_INVALID, "HIERARCH keyword name contains reserved character");

        // Normalize into the caller's buffer (sized so that "won't fit"
        // matches "exceeds the HIERARCH cap": PARSE_KEY_BUF == MAX + 1).
        int nerr = tinyfits__normalize_hierarch_key(key, NULL,
                                                    out_buf, out_buf_size);
        if (nerr == TINYFITS_ERR_KEYWORD_LENGTH)
            return tf_fail(header, nerr, "HIERARCH keyword name exceeds length limit");
        if (nerr != TINYFITS_OK)
            return tf_fail(header, TINYFITS_ERR_INVALID, "HIERARCH keyword name is malformed");
        if (out_canonical) *out_canonical = out_buf;
    }
    else
    {
        if (strlen(key) > TINYFITS_CARD_KEY_LEN)
            return tf_fail(header, TINYFITS_ERR_KEYWORD_LENGTH, "Keyword name exceeds 8-character limit");
        if (tinyfits__is_reserved_key(key))
            return tf_fail(header, TINYFITS_ERR_RESERVED_KEYWORD, "Keyword name is reserved");
        if (out_canonical) *out_canonical = key;
    }
    return TINYFITS_OK;
}

TFITSDEF int tinyfits_set_keyword(TinyFitsHeader* header, const char* key,
                                  const char* value, const char* comment)
{
    if (!header) return TINYFITS_ERR_NULL_ARG;
    char norm[TINYFITS_PARSE_KEY_BUF];
    const char* canonical = NULL;
    int err = tinyfits__validate_header_fields(header, key, value, comment,
                                               &canonical, norm, sizeof(norm));
    if (err != TINYFITS_OK) return err;

    // HISTORY/COMMENT: replace-first semantics, but the underlying card
    // is free-form (no '= ' indicator, no per-card comment slot).
    int is_history_or_comment =
        strcmp(key, "HISTORY") == 0 || strcmp(key, "COMMENT") == 0;
    if (is_history_or_comment)
    {
        if (comment && comment[0])
            return tf_fail(header, TINYFITS_ERR_INVALID, "HISTORY/COMMENT cards do not support per-card comments");
        if (value && strlen(value) > TINYFITS_HISTORY_PAYLOAD)
            return tf_fail(header, TINYFITS_ERR_KEYWORD_LENGTH, "HISTORY/COMMENT value exceeds single-card limit");
    }

    // Replace first match.
    for (int i = 0; i < header->num_keywords; i++)
    {
        if (tinyfits__keys_match(canonical, header->keywords[i].key) == 0)
        {
            char* new_value   = tinyfits__strdup(value   ? value   : "");
            char* new_comment = tinyfits__strdup(comment ? comment : "");
            if (!new_value || !new_comment)
            {
                TINYFITS_FREE(new_value);
                TINYFITS_FREE(new_comment);
                return tf_fail(header, TINYFITS_ERR_ALLOC, "Failed to allocate keyword value/comment");
            }
            TINYFITS_FREE(header->keywords[i].value);
            TINYFITS_FREE(header->keywords[i].comment);
            header->keywords[i].value = new_value;
            header->keywords[i].comment = new_comment;
            return TINYFITS_OK;
        }
    }

    // Not found -- append
    if (!tinyfits__add_header(header, canonical, value, comment))
        return tf_fail(header, TINYFITS_ERR_ALLOC, "Failed to allocate keyword entry");
    return TINYFITS_OK;
}

TFITSDEF int tinyfits_append_keyword(TinyFitsHeader* header, const char* key,
                                  const char* value, const char* comment)
{
    if (!header) return TINYFITS_ERR_NULL_ARG;
    char norm[TINYFITS_PARSE_KEY_BUF];
    const char* canonical = NULL;
    int err = tinyfits__validate_header_fields(header, key, value, comment,
                                               &canonical, norm, sizeof(norm));
    if (err != TINYFITS_OK) return err;

    // HISTORY/COMMENT auto-split: long text is chunked into multiple
    // cards. Each chunk becomes its own keyword entry. Splits before the
    // last whitespace in the per-card budget so the whitespace leads the
    // next chunk; combined with the parser preserving leading spaces on
    // HISTORY/COMMENT cards, the original byte sequence is recoverable.
    if (strcmp(key, "HISTORY") == 0 || strcmp(key, "COMMENT") == 0)
    {
        if (comment && comment[0])
            return tf_fail(header, TINYFITS_ERR_INVALID, "HISTORY/COMMENT cards do not support per-card comments");

        size_t text_len = value ? strlen(value) : 0;
        if (text_len == 0)
        {
            if (!tinyfits__add_header(header, key, "", ""))
                return tf_fail(header, TINYFITS_ERR_ALLOC, "Failed to allocate keyword entry");
            return TINYFITS_OK;
        }
        size_t pos = 0;
        while (pos < text_len)
        {
            size_t rem = text_len - pos;
            size_t chunk_len = rem < TINYFITS_HISTORY_PAYLOAD
                               ? rem : TINYFITS_HISTORY_PAYLOAD;
            if (chunk_len < rem)
            {
                size_t boundary = 0;
                for (size_t i = chunk_len; i > 0; i--)
                {
                    if (value[pos + i - 1] == ' ') { boundary = i - 1; break; }
                }
                if (boundary > 0) chunk_len = boundary;
                // else: hard-split (no whitespace in budget).
            }
            char buf[TINYFITS_HISTORY_PAYLOAD + 1];
            memcpy(buf, value + pos, chunk_len);
            buf[chunk_len] = '\0';
            if (!tinyfits__add_header(header, key, buf, ""))
                return tf_fail(header, TINYFITS_ERR_ALLOC, "Failed to allocate keyword entry");
            pos += chunk_len;
        }
        return TINYFITS_OK;
    }

    if (!tinyfits__add_header(header, canonical, value, comment))
        return tf_fail(header, TINYFITS_ERR_ALLOC, "Failed to allocate keyword entry");
    return TINYFITS_OK;
}

TFITSDEF int tinyfits_add_history(TinyFitsHeader* header, const char* text)
{
    return tinyfits_append_keyword(header, "HISTORY", text, NULL);
}

TFITSDEF int tinyfits_add_comment(TinyFitsHeader* header, const char* text)
{
    return tinyfits_append_keyword(header, "COMMENT", text, NULL);
}

TFITSDEF int tinyfits_remove_keyword(TinyFitsHeader* header, const char* key)
{
    if (!header || !key) return TINYFITS_ERR_NULL_ARG;
    for (int i = 0; i < header->num_keywords; i++)
    {
        if (tinyfits__keys_match(key, header->keywords[i].key) == 0)
        {
            TINYFITS_FREE(header->keywords[i].key);
            TINYFITS_FREE(header->keywords[i].value);
            TINYFITS_FREE(header->keywords[i].comment);
            int tail = header->num_keywords - i - 1;
            if (tail > 0)
                memmove(&header->keywords[i], &header->keywords[i + 1],
                        (size_t)tail * sizeof(TinyFitsKeyword));
            header->num_keywords--;
            return TINYFITS_OK;
        }
    }
    return TINYFITS_OK;
}

/* Apply physical-unit transform to integer-typed pixel data.
 *   physical = bzero + bscale * stored.
 */
static void tinyfits__physical_int(const void* pixels, int pixel_type, size_t n,
                                   double bscale, double bzero, float* out)
{
    // Identity-transform fast path
    int identity_transform = (bscale == 1.0 && bzero == 0.0);

#define TF_PHYS_CASE(tag, T) \
    case tag: { \
        const T* src = (const T*)pixels; \
        if (identity_transform) \
            for (size_t i = 0; i < n; i++) out[i] = (float)src[i]; \
        else \
            for (size_t i = 0; i < n; i++) \
                out[i] = (float)(bzero + bscale * (double)src[i]); \
        break; \
    }

    switch (pixel_type)
    {
        TF_PHYS_CASE(TINYFITS_UINT8,  uint8_t)
        TF_PHYS_CASE(TINYFITS_INT16,  int16_t)
        TF_PHYS_CASE(TINYFITS_UINT16, uint16_t)
        TF_PHYS_CASE(TINYFITS_INT32,  int32_t)
        TF_PHYS_CASE(TINYFITS_UINT32, uint32_t)
    }

#undef TF_PHYS_CASE
}

/* Linearly map the storage range [type_min, type_max] to [0, 1].
 */
static void tinyfits__normalize_int(const void* pixels, int pixel_type,
                                    size_t n, float* out)
{
    switch (pixel_type)
    {
        case TINYFITS_UINT8:
        {
            const uint8_t* src = (const uint8_t*)pixels;
            for (size_t i = 0; i < n; i++)
                out[i] = (float)src[i] / 255.0f;
            break;
        }
        case TINYFITS_INT16:
        {
            // Map [INT16_MIN, INT16_MAX] linearly to [0, 1].
            const int16_t* src = (const int16_t*)pixels;
            for (size_t i = 0; i < n; i++)
                out[i] = (float)((int)src[i] + 32768) / 65535.0f;
            break;
        }
        case TINYFITS_UINT16:
        {
            const uint16_t* src = (const uint16_t*)pixels;
            for (size_t i = 0; i < n; i++)
                out[i] = (float)src[i] / 65535.0f;
            break;
        }
        case TINYFITS_INT32:
        {
            const int32_t* src = (const int32_t*)pixels;
            // Map [INT32_MIN, INT32_MAX] linearly to [0, 1].
            const uint32_t range = UINT32_MAX;
            for (size_t i = 0; i < n; i++)
            {
                uint32_t shifted = (uint32_t)src[i] - (uint32_t)INT32_MIN;
                out[i] = (float)shifted / (float)range;
            }
            break;
        }
        case TINYFITS_UINT32:
        {
            const uint32_t* src = (const uint32_t*)pixels;
            for (size_t i = 0; i < n; i++)
                out[i] = (float)src[i] / (float)UINT32_MAX;
            break;
        }
    }
}

TFITSDEF int tinyfits_to_float_physical(const TinyFitsHeader* header, const void* pixels,
                                        float* out)
{
    if (!header || !pixels || !out)
        return TINYFITS_ERR_NULL_ARG;

    size_t n = (size_t)header->width * (size_t)header->height
             * (size_t)header->num_channels;

    switch (header->pixel_type)
    {
        case TINYFITS_UINT8:
        case TINYFITS_INT16:
        case TINYFITS_UINT16:
        case TINYFITS_INT32:
        case TINYFITS_UINT32:
            tinyfits__physical_int(pixels, header->pixel_type, n,
                                   header->bscale, header->bzero, out);
            return TINYFITS_OK;

        case TINYFITS_FLOAT32:
        {
            const float* src = (const float*)pixels;
            if (header->bscale == 1.0 && header->bzero == 0.0)
            {
                memcpy(out, src, n * sizeof(float));
            }
            else
            {
                for (size_t i = 0; i < n; i++)
                    out[i] = (float)(header->bzero + header->bscale * (double)src[i]);
            }
            return TINYFITS_OK;
        }

        case TINYFITS_FLOAT64:
        {
            const double* src = (const double*)pixels;
            for (size_t i = 0; i < n; i++)
                out[i] = (float)(header->bzero + header->bscale * src[i]);
            return TINYFITS_OK;
        }

        default:
            return TINYFITS_ERR_BAD_PIXEL_TYPE;
    }
}

TFITSDEF int tinyfits_to_float_normalized(const TinyFitsHeader* header, const void* pixels,
                                          float* out)
{
    if (!header || !pixels || !out)
        return TINYFITS_ERR_NULL_ARG;

    switch (header->pixel_type)
    {
        case TINYFITS_UINT8:
        case TINYFITS_INT16:
        case TINYFITS_UINT16:
        case TINYFITS_INT32:
        case TINYFITS_UINT32:
            break;
        default:
            return TINYFITS_ERR_BAD_PIXEL_TYPE;
    }

    size_t n = (size_t)header->width * (size_t)header->height
             * (size_t)header->num_channels;
    tinyfits__normalize_int(pixels, header->pixel_type, n, out);
    return TINYFITS_OK;
}

TFITSDEF int tinyfits_load_from_memory(TinyFitsHeader* header, const void* data,
                                       size_t size, void** pixels)
{
    if (!header || !data || !pixels) return TINYFITS_ERR_NULL_ARG;
    *pixels = NULL;

    int err;
    const uint8_t* ptr = tinyfits__parse_headers(header, data, size, &err);
    if (!ptr)
    {
        tinyfits_free_header(header);
        return err;
    }

    const uint8_t* end = (const uint8_t*)data + size;
    size_t img_size = tinyfits_image_size(header);
    if (img_size == 0)
    {
        tinyfits_free_header(header);
        return tf_fail(header, TINYFITS_ERR_BAD_DIMENSION, "Image size is zero or overflows");
    }
    size_t num_samples = tinyfits__num_samples(header);

    if (img_size > (size_t)(end - ptr))
    {
        tinyfits_free_header(header);
        return tf_fail(header, TINYFITS_ERR_TRUNCATED, "Input data is shorter than declared image size");
    }

    void* buf = TINYFITS_MALLOC(img_size);
    if (!buf)
    {
        tinyfits_free_header(header);
        return tf_fail(header, TINYFITS_ERR_ALLOC, "Failed to allocate image buffer");
    }

    // Bulk copy disk bytes, then byte-swap in place. UINT16/UINT32 fold
    // the canonical BZERO sign-flip into the same pass.
    memcpy(buf, ptr, img_size);
    switch (header->pixel_type)
    {
        case TINYFITS_UINT8:
            break;
        case TINYFITS_INT16:
            tinyfits__bswap16_block(buf, num_samples);
            break;
        case TINYFITS_UINT16:
            tinyfits__bswap16_xor_block(buf, num_samples);
            break;
        case TINYFITS_INT32:
        case TINYFITS_FLOAT32:
            tinyfits__bswap32_block(buf, num_samples);
            break;
        case TINYFITS_UINT32:
            tinyfits__bswap32_xor_block(buf, num_samples);
            break;
        case TINYFITS_FLOAT64:
            tinyfits__bswap64_block(buf, num_samples);
            break;
        default:
            TINYFITS_FREE(buf);
            tinyfits_free_header(header);
            return tf_fail(header, TINYFITS_ERR_BAD_PIXEL_TYPE, "Unsupported pixel type");
    }

    *pixels = buf;
    return TINYFITS_OK;
}

TFITSDEF int tinyfits_load(TinyFitsHeader* header, const char* path, void** pixels)
{
    if (!header || !path || !pixels) return TINYFITS_ERR_NULL_ARG;
    *pixels = NULL;

    void* file_data;
    size_t file_size;
    int err = tinyfits__read_file(header, path, &file_data, &file_size);
    if (err != TINYFITS_OK) return err;

    err = tinyfits_load_from_memory(header, file_data, file_size, pixels);
    TINYFITS_FREE(file_data);
    return err;
}

// Write helpers

static void tinyfits__write_be16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static void tinyfits__write_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static void tinyfits__write_be64(uint8_t* p, uint64_t v)
{
    p[0] = (uint8_t)(v >> 56);
    p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40);
    p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24);
    p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);
    p[7] = (uint8_t)(v);
}

/* Write a standard fixed-format card:
 *   key (truncated to 8 chars) + "= " + right-justified value + optional " / comment"
 *
 * This is the canonical layout for structural cards and user-supplied
 * numeric/logical keywords with standard 8-char names.
 */
static void tinyfits__write_card(uint8_t** p, const char* key,
                                 const char* value, const char* comment)
{
    char card[TINYFITS_CARD_SIZE];
    memset(card, ' ', TINYFITS_CARD_SIZE);

    size_t klen = strlen(key);
    if (klen > TINYFITS_CARD_KEY_LEN) klen = TINYFITS_CARD_KEY_LEN;
    memcpy(card, key, klen);
    card[TINYFITS_CARD_KEY_LEN]     = '=';
    card[TINYFITS_CARD_KEY_LEN + 1] = ' ';

    // Right-justify value in bytes 10..29 (truncate from the left if too long).
    size_t vlen = strlen(value);
    if (vlen > TINYFITS_CARD_FIXED_VALUE_LEN) vlen = TINYFITS_CARD_FIXED_VALUE_LEN;
    size_t vpos = TINYFITS_CARD_VALUE_OFFSET
                + (TINYFITS_CARD_FIXED_VALUE_LEN - vlen);
    memcpy(card + vpos, value, vlen);

    if (comment && comment[0])
    {
        // Comment starts after the 20-char fixed value field + a space.
        size_t cpos = TINYFITS_CARD_VALUE_OFFSET + TINYFITS_CARD_FIXED_VALUE_LEN + 1;
        card[cpos++] = '/';
        card[cpos++] = ' ';
        size_t clen = strlen(comment);
        if (clen > TINYFITS_CARD_SIZE - cpos) clen = TINYFITS_CARD_SIZE - cpos;
        memcpy(card + cpos, comment, clen);
    }

    memcpy(*p, card, TINYFITS_CARD_SIZE);
    *p += TINYFITS_CARD_SIZE;
}

static void tinyfits__write_card_int(uint8_t** p, const char* key, int value,
                                     const char* comment)
{
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%d", value);
    tinyfits__write_card(p, key, vbuf, comment);
}

static void tinyfits__write_card_float(uint8_t** p, const char* key, double value,
                                       const char* comment)
{
    // %.17g preserves IEEE-754 doubles across save/reload for BSCALE/BZERO.
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%.17g", value);
    tinyfits__write_card(p, key, vbuf, comment);
}

// Card kinds for the chunk-budget helper.
enum
{
    TINYFITS_VC_HEAD     = 0,  // standard 8-char-key head card
    TINYFITS_VC_CONTINUE = 1,  // CONTINUE intermediate or final card
    TINYFITS_VC_HIERARCH = 2,  // HIERARCH head card
};

/* Encoded byte count for the FITS string body of s: each "'" counts as
 * 2 bytes (quote-doubling); other characters count as 1 byte.
 */
static size_t tinyfits__encoded_length(const char* s)
{
    size_t n = 0;
    for (const char* p = s; *p; p++)
        n += (*p == '\'') ? 2 : 1;
    return n;
}

/* Returns the maximum number of keyword-value bytes that fit between the
 * opening and closing quotes on one card. The prefix that precedes the
 * opening quote differs by card kind:
 *
 *   HEAD:           <8-char key> "= "       (10 bytes)
 *   CONTINUE:       "CONTINUE  "            (10 bytes, no '=')
 *   HIERARCH:       "HIERARCH " <key> " = " (9 + klen + 3 bytes)
 *
 * Continuing cards add a trailing '&' (1 byte). Final cards may carry a
 * trailing " / <comment>" (3 + comment_len bytes). Returns 0 if the
 * configuration leaves no room for source bytes.
 */
static int tinyfits__chunk_budget(int card_kind, int has_continuation,
                                  int comment_len, int hierarch_key_len)
{
    enum {
        QUOTES              = 2,        // opening + closing '
        AMP                 = 1,        // trailing '&' on continuing cards
        COMMENT_SEP         = sizeof(" / ") - 1,
        HIERARCH_PREFIX     = sizeof("HIERARCH ") - 1,
        HIERARCH_SEP        = sizeof(" = ") - 1,
        HEAD_FINAL_BUDGET   = TINYFITS_CARD_SIZE - TINYFITS_CARD_VALUE_OFFSET - QUOTES,
        HEAD_CONT_BUDGET    = HEAD_FINAL_BUDGET - AMP,
        HIERARCH_FIXED      = HIERARCH_PREFIX + HIERARCH_SEP + QUOTES,
        HIERARCH_FINAL_BASE = TINYFITS_CARD_SIZE - HIERARCH_FIXED,
        HIERARCH_CONT_BASE  = HIERARCH_FINAL_BASE - AMP
    };

    int n;
    if (card_kind == TINYFITS_VC_HEAD || card_kind == TINYFITS_VC_CONTINUE)
    {
        if (has_continuation) return HEAD_CONT_BUDGET;
        n = HEAD_FINAL_BUDGET;
        if (comment_len > 0) n -= COMMENT_SEP + comment_len;
        return n < 0 ? 0 : n;
    }
    if (card_kind == TINYFITS_VC_HIERARCH)
    {
        if (has_continuation)
        {
            n = HIERARCH_CONT_BASE - hierarch_key_len;
            return n < 0 ? 0 : n;
        }
        n = HIERARCH_FINAL_BASE - hierarch_key_len;
        if (comment_len > 0) n -= COMMENT_SEP + comment_len;
        return n < 0 ? 0 : n;
    }
    return 0;
}

/* Pack src[pos..src_len) into the encoded-byte budget, returning chars
 * consumed. Won't split a "'" -> "''" escape pair across cards. May
 * return 0 (budget == 0, or budget == 1 with "'" next) -- callers must
 * guard against no progress.
 */
static size_t tinyfits__chunk_consume(const char* src, size_t pos,
                                      size_t src_len, int budget)
{
    size_t consumed = 0;
    int used = 0;
    while (pos + consumed < src_len)
    {
        char c = src[pos + consumed];
        int needed = (c == '\'') ? 2 : 1;
        if (used + needed > budget) break;
        used += needed;
        consumed++;
    }
    return consumed;
}

/* Count cards required to emit the given string value as a chain of one
 * head card (kind = head_kind) and zero or more CONTINUE intermediate
 * and final cards. Empty strings consume one card. Returns 0 if no sources
 * bytes can be packed onto a card.
 *
 * head_kind: TINYFITS_VC_HEAD or TINYFITS_VC_HIERARCH.
 * hierarch_key_len: length of encoded key, when head_kind == HIERARCH.
 */
static int tinyfits__count_string_chain(int head_kind, int hierarch_key_len,
                                        const char* value, int comment_len)
{
    size_t value_len = strlen(value);
    size_t pos = 0;
    int cards = 0;
    int first = 1;

    while (pos < value_len)
    {
        int kind = first ? head_kind : TINYFITS_VC_CONTINUE;
        int klen_for_budget = first ? hierarch_key_len : 0;
        int final_budget =
            tinyfits__chunk_budget(kind, 0, comment_len, klen_for_budget);
        size_t rem = tinyfits__encoded_length(value + pos);
        if (rem <= (size_t)final_budget)
        {
            cards++;
            break;
        }
        int cont_budget =
            tinyfits__chunk_budget(kind, 1, 0, klen_for_budget);
        size_t consumed = tinyfits__chunk_consume(value, pos, value_len, cont_budget);
        if (consumed == 0) return 0;
        pos += consumed;
        cards++;
        first = 0;
    }
    if (cards == 0) cards = 1;  // empty value still gets one card
    return cards;
}

/* Determine if a stored keyword's value should be written as a quoted
 * string or as a fixed-format numeric/logical literal.
 *
 * Returns 0 if the value parses fully as a FITS numeric or logical literal
 * and is <= TINYFITS_CARD_FIXED_VALUE_LENGTH in size.
 * Otherwise returns 1, preserving the value verbatim via the CONTINUE-capable writer.
 */
static int tinyfits__value_is_string(const char* v)
{
    if (!v || !v[0]) return 1;
    if ((v[0] == 'T' || v[0] == 'F') && v[1] == '\0')
        return 0;
    if (strlen(v) > TINYFITS_CARD_FIXED_VALUE_LEN) return 1;
    if (v[0] == ' ') return 1;  // leading space wouldn't survive numeric encoding
    char* end;
    (void)strtod(v, &end);
    return !(end != v && *end == '\0');
}

/* Count cards required to emit a single non-structural keyword.
 *
 * Strings dispatch to count_string_chain (1 or more cards), returning 0
 * if the layout can't pack any source bytes onto a card.
 * HISTORY, COMMENT, CONTINUE, and numeric/logical values each require
 * exactly one card.
 */
static int tinyfits__count_keyword_cards(const TinyFitsKeyword* h)
{
    if (strcmp(h->key, "HISTORY") == 0 ||
        strcmp(h->key, "COMMENT") == 0 ||
        strcmp(h->key, "CONTINUE") == 0)
        return 1;

    // Numerics, logicals, and HIERARCH values each occupy a single card.
    if (!tinyfits__value_is_string(h->value))
        return 1;

    int hierarch = tinyfits__is_hierarch_class(h->key);
    int kind = hierarch ? TINYFITS_VC_HIERARCH : TINYFITS_VC_HEAD;
    int klen = hierarch ? (int)strlen(h->key) : 0;
    int comment_len = h->comment ? (int)strlen(h->comment) : 0;
    return tinyfits__count_string_chain(kind, klen, h->value, comment_len);
}

/* Write a (possibly multi-card) chain for a string-valued keyword.
 *
 * head_kind must be one of:
 *  TINYFITS_VC_HEAD (standard 8-char key)
 *  TINYFITS_VC_HIERARCH (long key)
 *
 * Long comments that won't fit in the final card's remaining space are
 * silently truncated.
 */
static int tinyfits__write_string_chain(uint8_t** p, int head_kind,
                                        const char* key, const char* value,
                                        const char* comment)
{
    int klen = (int)strlen(key);
    int comment_len = comment ? (int)strlen(comment) : 0;

    size_t value_len = strlen(value);
    size_t pos = 0;
    int first = 1;

    while (1)
    {
        int kind = first ? head_kind : TINYFITS_VC_CONTINUE;
        int klen_for_budget = first ? klen : 0;
        size_t rem = tinyfits__encoded_length(value + pos);
        int final_budget =
            tinyfits__chunk_budget(kind, 0, comment_len, klen_for_budget);

        int is_final;
        size_t consumed;
        if (rem <= (size_t)final_budget)
        {
            is_final = 1;
            consumed = value_len - pos;
        }
        else
        {
            is_final = 0;
            int cont_budget =
                tinyfits__chunk_budget(kind, 1, 0, klen_for_budget);
            consumed = tinyfits__chunk_consume(value, pos, value_len, cont_budget);
            if (consumed == 0) return TINYFITS_ERR_KEYWORD_LENGTH;
        }

        char card[TINYFITS_CARD_SIZE];
        memset(card, ' ', TINYFITS_CARD_SIZE);
        int cpos;

        if (kind == TINYFITS_VC_HEAD)
        {
            int k = klen > TINYFITS_CARD_KEY_LEN ? TINYFITS_CARD_KEY_LEN : klen;
            memcpy(card, key, k);
            card[TINYFITS_CARD_KEY_LEN]     = '=';
            card[TINYFITS_CARD_KEY_LEN + 1] = ' ';
            cpos = TINYFITS_CARD_VALUE_OFFSET;
        }
        else if (kind == TINYFITS_VC_HIERARCH)
        {
            memcpy(card, "HIERARCH ", 9);
            memcpy(card + 9, key, klen);
            cpos = 9 + klen;
            card[cpos++] = ' ';
            card[cpos++] = '=';
            card[cpos++] = ' ';
        }
        else  // TINYFITS_VC_CONTINUE
        {
            memcpy(card, "CONTINUE  ", TINYFITS_CARD_VALUE_OFFSET);
            cpos = TINYFITS_CARD_VALUE_OFFSET;
        }

        card[cpos++] = '\'';
        for (size_t i = 0; i < consumed; i++)
        {
            char c = value[pos + i];
            if (c == '\'')
            {
                card[cpos++] = '\'';
                card[cpos++] = '\'';
            }
            else
            {
                card[cpos++] = c;
            }
        }
        if (!is_final) card[cpos++] = '&';
        card[cpos++] = '\'';

        if (is_final && comment_len > 0 && cpos + 4 <= TINYFITS_CARD_SIZE)
        {
            card[cpos++] = ' ';
            card[cpos++] = '/';
            card[cpos++] = ' ';
            int copy = comment_len;
            int max  = TINYFITS_CARD_SIZE - cpos;
            if (copy > max) copy = max;
            memcpy(card + cpos, comment, copy);
        }

        memcpy(*p, card, TINYFITS_CARD_SIZE);
        *p += TINYFITS_CARD_SIZE;

        pos += consumed;
        first = 0;
        if (is_final) break;
    }
    return TINYFITS_OK;
}

/* Write a HIERARCH numeric or logical keyword. The value bytes
 * are copied verbatim after 'HIERARCH <key> = ', with optional
 * '[space]/[space]<comment>' trailing.
 */
static int tinyfits__write_hierarch_nonstring(uint8_t** p,
                                              const TinyFitsKeyword* h)
{
    char card[TINYFITS_CARD_SIZE];
    memset(card, ' ', TINYFITS_CARD_SIZE);

    int klen = (int)strlen(h->key);
    int comment_len = h->comment && h->comment[0]
                      ? (int)strlen(h->comment) : 0;

    memcpy(card, "HIERARCH ", 9);
    memcpy(card + 9, h->key, klen);
    int pos = 9 + klen;
    card[pos++] = ' ';
    card[pos++] = '=';
    card[pos++] = ' ';

    int vlen = (int)strlen(h->value);
    int avail = TINYFITS_CARD_SIZE - pos;
    int vcopy = vlen < avail ? vlen : avail;
    memcpy(card + pos, h->value, vcopy);
    pos += vcopy;

    if (comment_len > 0 && pos + 4 <= TINYFITS_CARD_SIZE)
    {
        card[pos++] = ' ';
        card[pos++] = '/';
        card[pos++] = ' ';
        int max  = TINYFITS_CARD_SIZE - pos;
        int copy = comment_len < max ? comment_len : max;
        memcpy(card + pos, h->comment, copy);
    }

    memcpy(*p, card, TINYFITS_CARD_SIZE);
    *p += TINYFITS_CARD_SIZE;
    return TINYFITS_OK;
}

/* Write a single keyword card. Strings dispatch to the chain writer;
 * HISTORY/COMMENT/CONTINUE keep their free-form layout; numerics and
 * logicals use the fixed-format value field.
 */
static int tinyfits__write_card_full(uint8_t** p, const TinyFitsKeyword* h)
{
    if (strcmp(h->key, "HISTORY") == 0 || strcmp(h->key, "COMMENT") == 0 ||
        strcmp(h->key, "CONTINUE") == 0)
    {
        char card[TINYFITS_CARD_SIZE];
        memset(card, ' ', TINYFITS_CARD_SIZE);
        size_t klen = strlen(h->key);
        if (klen > TINYFITS_CARD_KEY_LEN) klen = TINYFITS_CARD_KEY_LEN;
        memcpy(card, h->key, klen);
        size_t vlen = strlen(h->value);
        size_t freeform_max = TINYFITS_CARD_SIZE - TINYFITS_CARD_KEY_LEN;
        if (vlen > freeform_max) vlen = freeform_max;
        memcpy(card + TINYFITS_CARD_KEY_LEN, h->value, vlen);
        memcpy(*p, card, TINYFITS_CARD_SIZE);
        *p += TINYFITS_CARD_SIZE;
        return TINYFITS_OK;
    }

    int hierarch = tinyfits__is_hierarch_class(h->key);

    if (hierarch && strlen(h->key) > TINYFITS_HIERARCH_KEY_MAX)
        return TINYFITS_ERR_KEYWORD_LENGTH;
    if (strlen(h->value) > (size_t)INT_MAX)
        return TINYFITS_ERR_KEYWORD_LENGTH;
    if (h->comment && strlen(h->comment) > (size_t)INT_MAX)
        return TINYFITS_ERR_KEYWORD_LENGTH;

    // String values: dispatch to chain writer.
    if (tinyfits__value_is_string(h->value))
    {
        int head_kind = hierarch ? TINYFITS_VC_HIERARCH : TINYFITS_VC_HEAD;
        return tinyfits__write_string_chain(p, head_kind, h->key,
                                            h->value, h->comment);
    }

    // HIERARCH numeric/logical: free-format value placement.
    if (hierarch)
        return tinyfits__write_hierarch_nonstring(p, h);

    // Standard 8-char numeric / logical: fixed-format value field.
    tinyfits__write_card(p, h->key, h->value, h->comment);
    return TINYFITS_OK;
}

static void tinyfits__write_end(uint8_t** p)
{
    char card[TINYFITS_CARD_SIZE];
    memset(card, ' ', TINYFITS_CARD_SIZE);
    memcpy(card, "END", 3);
    memcpy(*p, card, TINYFITS_CARD_SIZE);
    *p += TINYFITS_CARD_SIZE;
}

TFITSDEF int tinyfits_save_to_memory(TinyFitsHeader* header, const void* pixels,
                                     void** out_data, size_t* out_size,
                                     int interleaved)
{
    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;

    if (!header) return TINYFITS_ERR_NULL_ARG;
    if (!pixels || !out_data || !out_size)
        return tf_fail(header, TINYFITS_ERR_NULL_ARG, "Required argument is null");
    if (header->width <= 0 || header->height <= 0 || header->num_channels <= 0)
        return tf_fail(header, TINYFITS_ERR_BAD_DIMENSION, "Header dimensions are invalid");

    int bps = tinyfits__bytes_per_sample(header->pixel_type);
    if (bps == 0) return tf_fail(header, TINYFITS_ERR_BAD_PIXEL_TYPE, "Pixel type is unsupported");

    // Reject bscale=0, which is degenerate and almost certainly a caller bug.
    if (header->bscale == 0.0)
        return tf_fail(header, TINYFITS_ERR_BZERO_BSCALE, "Bscale is zero");

    // Reject non-default bscale/bzero on unsigned pixel types: UINT8/16/32
    // imply the canonical unsigned-integer-conversion transform and have no valid
    // file encoding for additional BSCALE/BZERO.
    if ((header->pixel_type == TINYFITS_UINT8 ||
         header->pixel_type == TINYFITS_UINT16 ||
         header->pixel_type == TINYFITS_UINT32) &&
        (header->bscale != 1.0 || header->bzero != 0.0))
        return tf_fail(header, TINYFITS_ERR_BZERO_BSCALE, "Unsigned integer pixel type has non-default bzero/bscale");

    // The struct's bscale/bzero fields are the single source of truth for
    // those quantities. set_keyword/append_keyword already reject these
    // keywords, but `keywords` is publicly accessible, so a caller could
    // mutate it directly.
    for (int i = 0; i < header->num_keywords; i++)
    {
        if (strcmp(header->keywords[i].key, "BSCALE") == 0 ||
            strcmp(header->keywords[i].key, "BZERO") == 0)
            return tf_fail(header, TINYFITS_ERR_RESERVED_KEYWORD, "Header attempts to override bzero/bscale");
    }

    size_t num_samples = tinyfits__num_samples(header);
    if (num_samples == 0)
        return tf_fail(header, TINYFITS_ERR_BAD_DIMENSION, "Header dimensions are invalid");
    if (num_samples > SIZE_MAX / (size_t)bps)
        return tf_fail(header, TINYFITS_ERR_BAD_DIMENSION, "Number of samples too large");
    size_t data_bytes = num_samples * (size_t)bps;

    // Determine BITPIX and the BSCALE/BZERO values to emit to the header.
    int bitpix;
    double save_bzero  = header->bzero;
    double save_bscale = header->bscale;
    switch (header->pixel_type)
    {
        case TINYFITS_UINT8:   bitpix = 8;   save_bzero = 0.0;          save_bscale = 1.0; break;
        case TINYFITS_INT16:   bitpix = 16;  break;
        case TINYFITS_UINT16:  bitpix = 16;  save_bzero = 32768.0;      save_bscale = 1.0; break;
        case TINYFITS_INT32:   bitpix = 32;  break;
        case TINYFITS_UINT32:  bitpix = 32;  save_bzero = 2147483648.0; save_bscale = 1.0; break;
        case TINYFITS_FLOAT32: bitpix = -32; break;
        case TINYFITS_FLOAT64: bitpix = -64; break;
        default: return tf_fail(header, TINYFITS_ERR_BAD_PIXEL_TYPE, "Unsupported pixel type");
    }

    // Header size: mandatory cards + non-structural cards (with CONTINUE expansion) + END, rounded up to
    // a block. BSCALE/BZERO are always emitted.
    int mandatory_cards = 5; // SIMPLE, BITPIX, NAXIS, NAXIS1, NAXIS2
    if (header->num_channels > 1) mandatory_cards++; // NAXIS3
    mandatory_cards += 2; // BZERO, BSCALE -- always emitted
    mandatory_cards++; // EXTEND

    // Count non-structural keyword cards. The length caps mirror
    // validate_header_fields and catch direct keywords[] mutation.
    int user_cards = 0;
    for (int i = 0; i < header->num_keywords; i++)
    {
        const TinyFitsKeyword* kw = &header->keywords[i];
        if (tinyfits__is_reserved_key(kw->key)) continue;
        int hierarch = tinyfits__is_hierarch_class(kw->key);
        if (hierarch && strlen(kw->key) > TINYFITS_HIERARCH_KEY_MAX)
            return tf_fail(header, TINYFITS_ERR_KEYWORD_LENGTH, "HIERARCH keyword name exceeds length limit");
        if (strlen(kw->value) > (size_t)INT_MAX)
            return tf_fail(header, TINYFITS_ERR_KEYWORD_LENGTH, "Keyword value exceeds length limit");
        if (kw->comment && strlen(kw->comment) > (size_t)INT_MAX)
            return tf_fail(header, TINYFITS_ERR_KEYWORD_LENGTH, "Keyword comment exceeds length limit");
        int c = tinyfits__count_keyword_cards(kw);
        if (c == 0) return tf_fail(header, TINYFITS_ERR_KEYWORD_LENGTH, "Malformed keyword card");
        user_cards += c;
    }
    int total_cards = mandatory_cards + user_cards + 1; // +1 for END

    if (total_cards > TINYFITS_MAX_HEADER_BLOCKS * TINYFITS_CARDS_PER_BLOCK)
        return tf_fail(header, TINYFITS_ERR_KEYWORD_LENGTH, "Total card size exceeds maximum header size.");

    int header_blocks = (total_cards + TINYFITS_CARDS_PER_BLOCK - 1)
                        / TINYFITS_CARDS_PER_BLOCK;
    size_t header_bytes = (size_t)header_blocks * TINYFITS_BLOCK_SIZE;

    // Data padded to block boundary
    size_t data_padded = ((data_bytes + TINYFITS_BLOCK_SIZE - 1)
                         / TINYFITS_BLOCK_SIZE) * TINYFITS_BLOCK_SIZE;

    size_t total_size = header_bytes + data_padded;
    uint8_t* buf = (uint8_t*)TINYFITS_CALLOC(1, total_size);
    if (!buf) return tf_fail(header, TINYFITS_ERR_ALLOC, "Failed to allocate header");

    // Write header.
    uint8_t* p = buf;

    // Structural keywords come first, in specific order. The comment
    // strings are conventional (cfitsio), not required by the spec.
    const char* bzero_comment = "data offset";
    if (header->pixel_type == TINYFITS_UINT16)
        bzero_comment = "offset data range to that of unsigned short";
    else if (header->pixel_type == TINYFITS_UINT32)
        bzero_comment = "offset data range to that of unsigned int";

    tinyfits__write_card(&p, "SIMPLE", "T", "file does conform to FITS standard");
    tinyfits__write_card_int(&p, "BITPIX", bitpix, "number of bits per data pixel");
    tinyfits__write_card_int(&p, "NAXIS", (header->num_channels > 1) ? 3 : 2, "number of data axes");
    tinyfits__write_card_int(&p, "NAXIS1", header->width, "length of data axis 1");
    tinyfits__write_card_int(&p, "NAXIS2", header->height, "length of data axis 2");
    if (header->num_channels > 1)
        tinyfits__write_card_int(&p, "NAXIS3", header->num_channels, "length of data axis 3");
    tinyfits__write_card_float(&p, "BZERO", save_bzero, bzero_comment);
    tinyfits__write_card_float(&p, "BSCALE", save_bscale, "default scaling factor");
    tinyfits__write_card(&p, "EXTEND", "T", "FITS dataset may contain extensions");

    // Non-structural keywords.
    for (int i = 0; i < header->num_keywords; i++)
    {
        if (tinyfits__is_reserved_key(header->keywords[i].key))
            continue;
        int werr = tinyfits__write_card_full(&p, &header->keywords[i]);
        if (werr != TINYFITS_OK)
        {
            TINYFITS_FREE(buf);
            return tf_fail(header, werr, "Failed to write header card");
        }
    }

    tinyfits__write_end(&p);

    // Write pixel data
    uint8_t* data_start = buf + header_bytes;
    int ch = header->num_channels;
    size_t es = (size_t)bps;

    if (!(interleaved && ch > 1))
    {
        // Fast path: source layout already matches the planar destination.
        memcpy(data_start, pixels, num_samples * es);
        switch (header->pixel_type)
        {
            case TINYFITS_UINT8:
                break;
            case TINYFITS_INT16:
                tinyfits__bswap16_block(data_start, num_samples);
                break;
            case TINYFITS_UINT16:
                tinyfits__xor_bswap16_block(data_start, num_samples);
                break;
            case TINYFITS_INT32:
            case TINYFITS_FLOAT32:
                tinyfits__bswap32_block(data_start, num_samples);
                break;
            case TINYFITS_UINT32:
                tinyfits__xor_bswap32_block(data_start, num_samples);
                break;
            case TINYFITS_FLOAT64:
                tinyfits__bswap64_block(data_start, num_samples);
                break;
        }
    }
    else
    {
        // Interleaved multi-channel: deinterleave element-by-element.
        size_t plane_samples = (size_t)header->width * (size_t)header->height;
        size_t src_stride_c = es;
        size_t src_stride_j = (size_t)ch * es;
        const uint8_t* src = (const uint8_t*)pixels;

        switch (header->pixel_type)
        {
            case TINYFITS_UINT8:
                for (int c = 0; c < ch; c++)
                    for (size_t j = 0; j < plane_samples; j++)
                        data_start[(size_t)c * plane_samples + j] =
                            src[(size_t)c * src_stride_c + j * src_stride_j];
                break;
            case TINYFITS_INT16:
                for (int c = 0; c < ch; c++)
                {
                    for (size_t j = 0; j < plane_samples; j++)
                    {
                        size_t so = (size_t)c * src_stride_c + j * src_stride_j;
                        size_t di = (size_t)c * plane_samples + j;
                        uint16_t u;
                        memcpy(&u, src + so, 2);
                        tinyfits__write_be16(data_start + di * 2, u);
                    }
                }
                break;
            case TINYFITS_UINT16:
                for (int c = 0; c < ch; c++)
                {
                    for (size_t j = 0; j < plane_samples; j++)
                    {
                        size_t so = (size_t)c * src_stride_c + j * src_stride_j;
                        size_t di = (size_t)c * plane_samples + j;
                        uint16_t v;
                        memcpy(&v, src + so, 2);
                        tinyfits__write_be16(data_start + di * 2, v ^ 0x8000);
                    }
                }
                break;
            case TINYFITS_INT32:
                for (int c = 0; c < ch; c++)
                {
                    for (size_t j = 0; j < plane_samples; j++)
                    {
                        size_t so = (size_t)c * src_stride_c + j * src_stride_j;
                        size_t di = (size_t)c * plane_samples + j;
                        uint32_t u;
                        memcpy(&u, src + so, 4);
                        tinyfits__write_be32(data_start + di * 4, u);
                    }
                }
                break;
            case TINYFITS_UINT32:
                for (int c = 0; c < ch; c++)
                {
                    for (size_t j = 0; j < plane_samples; j++)
                    {
                        size_t so = (size_t)c * src_stride_c + j * src_stride_j;
                        size_t di = (size_t)c * plane_samples + j;
                        uint32_t v;
                        memcpy(&v, src + so, 4);
                        tinyfits__write_be32(data_start + di * 4, v ^ 0x80000000u);
                    }
                }
                break;
            case TINYFITS_FLOAT32:
                for (int c = 0; c < ch; c++)
                {
                    for (size_t j = 0; j < plane_samples; j++)
                    {
                        size_t so = (size_t)c * src_stride_c + j * src_stride_j;
                        size_t di = (size_t)c * plane_samples + j;
                        uint32_t v;
                        memcpy(&v, src + so, 4);
                        tinyfits__write_be32(data_start + di * 4, v);
                    }
                }
                break;
            case TINYFITS_FLOAT64:
                for (int c = 0; c < ch; c++)
                {
                    for (size_t j = 0; j < plane_samples; j++)
                    {
                        size_t so = (size_t)c * src_stride_c + j * src_stride_j;
                        size_t di = (size_t)c * plane_samples + j;
                        uint64_t v;
                        memcpy(&v, src + so, 8);
                        tinyfits__write_be64(data_start + di * 8, v);
                    }
                }
                break;
        }
    }

    *out_data = buf;
    *out_size = total_size;
    return TINYFITS_OK;
}

TFITSDEF int tinyfits_save(TinyFitsHeader* header, const void* pixels,
                           const char* path, int interleaved)
{
    if (!header || !path) return TINYFITS_ERR_NULL_ARG;
    void* data;
    size_t size;
    int err = tinyfits_save_to_memory(header, pixels, &data, &size, interleaved);
    if (err != TINYFITS_OK)
        return err;

    FILE* f = fopen(path, "wb");
    if (!f)
    {
        tinyfits_free_buffer(data);
        return tf_fail(header, TINYFITS_ERR_OPEN, "Unable to open output file");
    }

    size_t written = fwrite(data, 1, size, f);
    int close_err = fclose(f);
    tinyfits_free_buffer(data);

    if (written != size || close_err != 0)
        return tf_fail(header, TINYFITS_ERR_WRITE, "Error writing to output file");

    return TINYFITS_OK;
}

#endif // TINYFITS_IMPLEMENTATION

#endif // TINYFITS_H

/*
 * ----------------------------------------------------------------------
 * This software is available under 2 licenses -- choose whichever you prefer.
 * ----------------------------------------------------------------------
 * ALTERNATIVE A - MIT License
 *
 * Copyright (c) 2026 Craig Kolb
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ----------------------------------------------------------------------
 * ALTERNATIVE B - Public Domain (www.unlicense.org)
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any means.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND. THE
 * AUTHORS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING
 * ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY CLAIM FOR DAMAGES WHATSOEVER.
 * ----------------------------------------------------------------------
 */
