/* SPDX-License-Identifier: MIT OR Unlicense */

/*
 * tinyfits.h -- single-header FITS image reader/writer.
 *
 * Supports:
 *  - Grayscale, RGB, and multi-plane images
 *  - 8/16/32-bit integer and 32/64-bit floating point format images
 *  - Pixel data conversion to float32 via `tinyfits_to_float()`
 *  - Keyword reading, setting, addition, and deletion
 *  - FITS writing, with optional interleaved-to-planar conversion
 *
 * Not supported
 *  - Multi-HDU / extension files (only the primary image is read)
 *  - 64-bit integer data
 *  - FITS images with BSCALE != 1
 *  - FITS images with BZERO other than standard values that indicate unsigned data
 *  - Compressed images (Rice, GZIP, HCOMPRESS)
 *  - Binary or ASCII tables
 *  - Automatic concatenation of CONTINUE long strings
 *  - HIERARCH keywords
 *
 * Pixel datatype inference:
 *
 *   The library maps BITPIX and BZERO/BSCALE to a logical pixel datatype.
 *   BZERO/BSCALE keywords are stripped on load and auto-generated on save.
 *
 *   BITPIX  BZERO         pixel_type         C type
 *     8     0             TINYFITS_UINT8     uint8_t
 *    16     0             TINYFITS_INT16     int16_t
 *    16     32768         TINYFITS_UINT16    uint16_t
 *    32     0             TINYFITS_INT32     int32_t
 *    32     2147483648    TINYFITS_UINT32    uint32_t
 *   -32     0             TINYFITS_FLOAT32   float
 *   -64     0             TINYFITS_FLOAT64   double
 *
 *   Missing BZERO defaults to 0, missing BSCALE defaults to 1.
 *   Any other BZERO/BSCALE combination returns TINYFITS_ERR_BZERO_BSCALE.
 *
 * Reading:
 *
 *   #define TINYFITS_IMPLEMENTATION   // in exactly one .c/.cpp file
 *   #include "tinyfits.h"
 *
 *   TinyFits info = {0};
 *   void* pixels;
 *   int err = tinyfits_load(&info, "image.fits", &pixels);
 *   // info.width, info.height, info.num_channels, info.pixel_type
 *   // pixels contains native-format data (e.g. uint16_t for camera frames)
 *   tinyfits_free_buffer(pixels);
 *   tinyfits_free(&info);
 *
 * Read keywords only:
 *
 *   TinyFits info = {0};
 *   tinyfits_info(&info, "image.fits");
 *   const char* bayer = tinyfits_get_keyword(&info, "BAYERPAT");
 *   tinyfits_free(&info);
 *
 * Writing:
 *
 *   TinyFits info = {0};
 *   info.width = 1024;
 *   info.height = 768;
 *   info.num_channels = 1;
 *   info.pixel_type = TINYFITS_UINT16;
 *   tinyfits_set_keyword(&info, "INSTRUME", "ZWO ASI2600MC Pro", "");
 *   tinyfits_save(&info, pixels, "output.fits", 0);
 *   tinyfits_free(&info);
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
 * Thread safety:
 *
 *   All functions are safe to call concurrently on different TinyFits instances.
 *   Do not modify a TinyFits instance from more than one concurrent thread.
 *
 * License: see end of file.
 */

#ifndef TINYFITS_H
#define TINYFITS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Error codes --- */

#define TINYFITS_OK                0  /* no error */
#define TINYFITS_ERR_OPEN          1  /* error opening file */
#define TINYFITS_ERR_READ          2  /* error reading file */
#define TINYFITS_ERR_NOT_FITS      3  /* header does not match FITS */
#define TINYFITS_ERR_INVALID       4  /* invalid input or image data */
#define TINYFITS_ERR_ALLOC         5  /* memory allocation failure */
#define TINYFITS_ERR_BITPIX        6  /* unsupported BITPIX value */
#define TINYFITS_ERR_BZERO_BSCALE  7  /* non-standard BZERO/BSCALE */
#define TINYFITS_ERR_WRITE         8  /* file I/O failure during save */

/* --- Constants --- */

#define TINYFITS_UNKNOWN   0
#define TINYFITS_UINT8     1
#define TINYFITS_INT16     2
#define TINYFITS_UINT16    3
#define TINYFITS_INT32     4
#define TINYFITS_UINT32    5
#define TINYFITS_FLOAT32   6
#define TINYFITS_FLOAT64   7


#define TINYFITS_BLOCK_SIZE 2880

/* --- Types --- */

typedef struct
{
    char key[9];
    char value[72];
    char comment[72];
} TinyFitsKeyword;

typedef struct
{
    int width;
    int height;
    int num_channels;
    int bitpix;
    int pixel_type;
    TinyFitsKeyword* keywords;
    int num_keywords;
    int keywords_capacity; /* internal -- do not modify */
} TinyFits;

/*
 * Load image from file. Populates info with dimensions, pixel_type,
 * and keywords. Allocates the pixel buffer; free with
 * tinyfits_free_buffer.
 *
 * info must be zero-initialized or previously freed.
 *
 * Pixel data is in the format indicated by info->pixel_type
 * (channel-planar, row-major).
 *
 * On failure, *pixels is set to NULL and info is zeroed.
 */
int tinyfits_load(TinyFits* info, const char* path, void** pixels);
int tinyfits_load_from_memory(TinyFits* info, const void* data,
                              size_t size, void** pixels);

/*
 * Load keywords and dimensions only; no pixel data is read.
 * info->pixel_type is populated.
 */
int tinyfits_info(TinyFits* info, const char* path);
int tinyfits_info_from_memory(TinyFits* info, const void* data, size_t size);

/*
 * Free metadata (keywords). Does not touch pixel buffers.
 * Safe on zero-initialized, info-only, or already-freed structs.
 */
void tinyfits_free(TinyFits* info);

/*
 * Free a library-allocated buffer (pixels from load, data from
 * save_to_memory).
 */
void tinyfits_free_buffer(void* buf);

/* Return a human-readable string for an error code. */
const char* tinyfits_error_string(int err);

/*
 * Return the value of the first keyword matching key, or NULL if not found.
 *
 * The returned pointer is valid until tinyfits_free or any keyword modification call.
 */
const char* tinyfits_get_keyword(const TinyFits* info, const char* key);

/*
 * Return the count of keywords matching key. If values is non-NULL,
 * fill up to max_values pointers.
 *
 * Returns total count (which may exceed max_values).
 */
int tinyfits_get_keywords(const TinyFits* info, const char* key,
                         const char** values, int max_values);

/*
 * Set a keyword, replacing the first match or appending if not found.
 *
 * Returns ERR_INVALID if key > 8 chars, value > 71 chars, comment > 71
 * chars, or key is a reserved keyword (SIMPLE, BITPIX, NAXIS, NAXISn,
 * EXTEND, END, BZERO, BSCALE).
 */
int tinyfits_set_keyword(TinyFits* info, const char* key,
                        const char* value, const char* comment);

/*
 * Append a keyword without replacing existing keys. Intended for
 * HISTORY, COMMENT, and other repeatable keywords.
 * Applies the same validation as tinyfits_set_keyword.
 */
int tinyfits_add_keyword(TinyFits* info, const char* key,
                        const char* value, const char* comment);

/* Remove the first keyword matching key. No-op if not found. */
int tinyfits_remove_keyword(TinyFits* info, const char* key);

/*
 * Total pixel buffer size in bytes.
 * Returns 0 if dimensions or pixel_type are invalid.
 */
size_t tinyfits_image_size(const TinyFits* info);

/*
 * Convert native-format pixels to float32, normalizing native-integer data.
 * out must hold at least width * height * num_channels floats.
 *
 * Unsigned integer types are scaled to [0,1]. Signed integer types are
 * scaled to (approximately) [-1,1]. Float and double values are not scaled.
 */
int tinyfits_to_float(const TinyFits* info, const void* pixels, float* out);

/*
 * Write image to file.
 *
 * If interleaved is nonzero, pixel data is assumed to be interleaved
 * (RGBRGB...) and is deinterleaved before writing.
 */
int tinyfits_save(const TinyFits* info, const void* pixels,
                  const char* path, int interleaved);

/*
 * Write image to a memory buffer. Free *out_data with tinyfits_free_buffer().
 * On failure, *out_data is set to NULL.
 */
int tinyfits_save_to_memory(const TinyFits* info, const void* pixels,
                            void** out_data, size_t* out_size,
                            int interleaved);

#ifdef __cplusplus
}
#endif

/* ---------- implementation ---------- */

#ifdef TINYFITS_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Custom allocator: define all four macros, or none. */
#if defined(TINYFITS_MALLOC) || defined(TINYFITS_CALLOC) || \
    defined(TINYFITS_REALLOC) || defined(TINYFITS_FREE)
    #if !defined(TINYFITS_MALLOC) || !defined(TINYFITS_CALLOC) || \
        !defined(TINYFITS_REALLOC) || !defined(TINYFITS_FREE)
        #error "Define all of TINYFITS_MALLOC, TINYFITS_CALLOC, TINYFITS_REALLOC, TINYFITS_FREE, or none."
    #endif
#else
    #define TINYFITS_MALLOC(sz)        malloc(sz)
    #define TINYFITS_CALLOC(cnt, sz)   calloc((cnt), (sz))
    #define TINYFITS_REALLOC(p, sz)    realloc((p), (sz))
    #define TINYFITS_FREE(p)           free(p)
#endif

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

static const char* tinyfits__error_strings[] =
{
    "OK",
    "Could not open file",
    "Read error",
    "Not a FITS file",
    "Invalid FITS structure",
    "Memory allocation failed",
    "Unsupported BITPIX value",
    "Non-standard BZERO/BSCALE",
    "Write error",
};

const char* tinyfits_error_string(int err)
{
    if (err < 0 || err > TINYFITS_ERR_WRITE)
        return "Unknown error";
    return tinyfits__error_strings[err];
}

void tinyfits_free(TinyFits* info)
{
    if (!info) return;
    TINYFITS_FREE(info->keywords);
    info->keywords = NULL;
    info->num_keywords = 0;
    info->keywords_capacity = 0;
    info->width = 0;
    info->height = 0;
    info->num_channels = 0;
    info->bitpix = 0;
    info->pixel_type = TINYFITS_UNKNOWN;
}

void tinyfits_free_buffer(void* buf)
{
    TINYFITS_FREE(buf);
}

const char* tinyfits_get_keyword(const TinyFits* info, const char* key)
{
    if (!info || !key) return NULL;
    for (int i = 0; i < info->num_keywords; i++)
    {
        if (strcmp(info->keywords[i].key, key) == 0)
            return info->keywords[i].value;
    }
    return NULL;
}

size_t tinyfits_image_size(const TinyFits* info)
{
    if (!info || info->width <= 0 || info->height <= 0 || info->num_channels <= 0)
        return 0;
    int bps = tinyfits__bytes_per_sample(info->pixel_type);
    if (bps == 0) return 0;
    return (size_t)info->width * (size_t)info->height
         * (size_t)info->num_channels * (size_t)bps;
}

/* --- Internal: byte-swap read helpers --- */

static uint16_t tinyfits__read16(const uint8_t* p)
{
    uint16_t v;
    memcpy(&v, p, 2);
#if !TINYFITS_IS_BIG_ENDIAN
    v = TINYFITS_BSWAP16(v);
#endif
    return v;
}

static uint32_t tinyfits__read32(const uint8_t* p)
{
    uint32_t v;
    memcpy(&v, p, 4);
#if !TINYFITS_IS_BIG_ENDIAN
    v = TINYFITS_BSWAP32(v);
#endif
    return v;
}

static uint64_t tinyfits__read64(const uint8_t* p)
{
    uint64_t v;
    memcpy(&v, p, 8);
#if !TINYFITS_IS_BIG_ENDIAN
    v = TINYFITS_BSWAP64(v);
#endif
    return v;
}

/* --- Internal: keyword storage --- */

static int tinyfits__add_header(TinyFits* info, const char* key,
                                const char* value, const char* comment)
{
    if (info->num_keywords >= info->keywords_capacity)
    {
        int new_cap = info->keywords_capacity ? info->keywords_capacity * 2 : 32;
        TinyFitsKeyword* p = (TinyFitsKeyword*)TINYFITS_REALLOC(
            info->keywords, (size_t)new_cap * sizeof(TinyFitsKeyword));
        if (!p) return 0;
        info->keywords = p;
        info->keywords_capacity = new_cap;
    }
    TinyFitsKeyword* h = &info->keywords[info->num_keywords];
    strncpy(h->key, key, 8);
    h->key[8] = '\0';
    strncpy(h->value, value ? value : "", 71);
    h->value[71] = '\0';
    strncpy(h->comment, comment ? comment : "", 71);
    h->comment[71] = '\0';
    info->num_keywords++;
    return 1;
}

/* --- Internal: parse a single 80-byte header card --- */

static void tinyfits__parse_card(const char* card, char* key,
                                 char* value, char* comment)
{
    /* Key: bytes 0-7, trim trailing spaces */
    memcpy(key, card, 8);
    key[8] = '\0';
    int ki = 7;
    while (ki >= 0 && key[ki] == ' ')
        ki--;
    key[ki + 1] = '\0';

    value[0] = '\0';
    comment[0] = '\0';

    /* HISTORY, COMMENT, and CONTINUE cards: no '= ' separator */
    if (strcmp(key, "HISTORY") == 0 || strcmp(key, "COMMENT") == 0 ||
        strcmp(key, "CONTINUE") == 0)
    {
        const char* text = card + 8;
        /* Skip one leading space if present */
        if (*text == ' ') text++;
        int vi = 0;
        while (text < card + 80 && vi < 71)
            value[vi++] = *text++;
        while (vi > 0 && value[vi - 1] == ' ')
            vi--;
        value[vi] = '\0';
        return;
    }

    /* Value cards: '= ' at bytes 8-9 */
    if (card[8] != '=')
        return;

    const char* vstart = card + 10;
    while (*vstart == ' ' && vstart < card + 80)
        vstart++;

    if (*vstart == '\'')
    {
        /* String value in single quotes. '' is an escaped quote. */
        vstart++;
        int vi = 0;
        while (vstart < card + 80 && vi < 71)
        {
            if (*vstart == '\'')
            {
                if (vstart + 1 < card + 80 && *(vstart + 1) == '\'')
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

        /* Comment: skip spaces and '/' after closing quote */
        while (vstart < card + 80 && *vstart == ' ')
            vstart++;
        if (vstart < card + 80 && *vstart == '/')
        {
            vstart++;
            while (vstart < card + 80 && *vstart == ' ')
                vstart++;
            int ci = 0;
            while (vstart < card + 80 && ci < 71)
                comment[ci++] = *vstart++;
            while (ci > 0 && comment[ci - 1] == ' ')
                ci--;
            comment[ci] = '\0';
        }
    }
    else
    {
        /* Numeric or logical value: up to ' /' comment delimiter */
        const char* cstart = NULL;
        const char* p = vstart;
        while (p < card + 80)
        {
            if (*p == '/' && p > vstart)
            {
                cstart = p + 1;
                break;
            }
            p++;
        }

        int vi = 0;
        const char* vend = cstart ? (cstart - 1) : (card + 80);
        while (vstart < vend && vi < 71)
            value[vi++] = *vstart++;
        while (vi > 0 && value[vi - 1] == ' ')
            vi--;
        value[vi] = '\0';

        if (cstart)
        {
            while (cstart < card + 80 && *cstart == ' ')
                cstart++;
            int ci = 0;
            while (cstart < card + 80 && ci < 71)
                comment[ci++] = *cstart++;
            while (ci > 0 && comment[ci - 1] == ' ')
                ci--;
            comment[ci] = '\0';
        }
    }
}

/* --- Internal: resolve pixel_type from BITPIX + BZERO/BSCALE --- */

static int tinyfits__resolve_pixel_type(int bitpix, double bzero, double bscale,
                                        int* pixel_type)
{
    if (bscale != 1.0)
        return TINYFITS_ERR_BZERO_BSCALE;

    switch (bitpix)
    {
        case 8:
            if (bzero != 0.0) return TINYFITS_ERR_BZERO_BSCALE;
            *pixel_type = TINYFITS_UINT8;
            return TINYFITS_OK;
        case 16:
            if (bzero == 0.0)       { *pixel_type = TINYFITS_INT16;  return TINYFITS_OK; }
            if (bzero == 32768.0)   { *pixel_type = TINYFITS_UINT16; return TINYFITS_OK; }
            return TINYFITS_ERR_BZERO_BSCALE;
        case 32:
            if (bzero == 0.0)          { *pixel_type = TINYFITS_INT32;  return TINYFITS_OK; }
            if (bzero == 2147483648.0) { *pixel_type = TINYFITS_UINT32; return TINYFITS_OK; }
            return TINYFITS_ERR_BZERO_BSCALE;
        case -32:
            if (bzero != 0.0) return TINYFITS_ERR_BZERO_BSCALE;
            *pixel_type = TINYFITS_FLOAT32;
            return TINYFITS_OK;
        case -64:
            if (bzero != 0.0) return TINYFITS_ERR_BZERO_BSCALE;
            *pixel_type = TINYFITS_FLOAT64;
            return TINYFITS_OK;
        default:
            return TINYFITS_ERR_BITPIX;
    }
}

/* --- Internal: parse keywords from a memory buffer --- */

static const uint8_t* tinyfits__parse_headers(TinyFits* info,
                                              const void* data, size_t size,
                                              int* err)
{
    memset(info, 0, sizeof(TinyFits));

    const uint8_t* ptr = (const uint8_t*)data;
    const uint8_t* end = ptr + size;

    if (size < TINYFITS_BLOCK_SIZE || memcmp(ptr, "SIMPLE  =", 9) != 0)
    {
        *err = TINYFITS_ERR_NOT_FITS;
        return NULL;
    }

    int naxis = 0;
    int naxis_vals[8] = {0};
    double bzero = 0.0;
    double bscale = 1.0;
    int have_bzero = 0;
    int have_bscale = 0;
    int header_done = 0;
    int block_count = 0;

    while (ptr < end && !header_done)
    {
        if (++block_count > 1024)
        {
            *err = TINYFITS_ERR_INVALID;
            return NULL;
        }

        if ((size_t)(end - ptr) < TINYFITS_BLOCK_SIZE)
        {
            *err = TINYFITS_ERR_READ;
            return NULL;
        }

        for (int c = 0; c < 36; c++)
        {
            const char* card = (const char*)ptr + c * 80;

            if (memcmp(card, "END     ", 8) == 0)
            {
                header_done = 1;
                break;
            }

            char key[9] = {0}, value[72] = {0}, comment[72] = {0};
            tinyfits__parse_card(card, key, value, comment);

            if (key[0] == '\0')
                continue;

            /* Parse structural keywords */
            if (strcmp(key, "BITPIX") == 0)
                info->bitpix = atoi(value);
            else if (strcmp(key, "NAXIS") == 0)
                naxis = atoi(value);
            else if (strcmp(key, "NAXIS1") == 0)
                naxis_vals[0] = atoi(value);
            else if (strcmp(key, "NAXIS2") == 0)
                naxis_vals[1] = atoi(value);
            else if (strcmp(key, "NAXIS3") == 0)
                naxis_vals[2] = atoi(value);
            else if (strcmp(key, "BZERO") == 0)
            {
                bzero = atof(value);
                have_bzero = 1;
                continue; /* strip from keywords */
            }
            else if (strcmp(key, "BSCALE") == 0)
            {
                bscale = atof(value);
                have_bscale = 1;
                continue; /* strip from keywords */
            }

            /* Store all other keywords (including SIMPLE, BITPIX, NAXIS, etc.) */
            if (!tinyfits__add_header(info, key, value, comment))
            {
                *err = TINYFITS_ERR_ALLOC;
                return NULL;
            }
        }

        ptr += TINYFITS_BLOCK_SIZE;
    }

    if (!header_done)
    {
        *err = TINYFITS_ERR_INVALID;
        return NULL;
    }

    /* Validate NAXIS */
    if (naxis != 2 && naxis != 3)
    {
        *err = TINYFITS_ERR_INVALID;
        return NULL;
    }

    info->width = naxis_vals[0];
    info->height = naxis_vals[1];
    info->num_channels = (naxis == 3) ? naxis_vals[2] : 1;

    if (info->width <= 0 || info->height <= 0 || info->num_channels <= 0)
    {
        *err = TINYFITS_ERR_INVALID;
        return NULL;
    }

    /* Default BZERO/BSCALE if not present */
    if (!have_bzero) bzero = 0.0;
    if (!have_bscale) bscale = 1.0;

    /* Resolve pixel_type */
    *err = tinyfits__resolve_pixel_type(info->bitpix, bzero, bscale,
                                        &info->pixel_type);
    if (*err != TINYFITS_OK)
        return NULL;

    *err = TINYFITS_OK;
    return ptr;
}

/* --- Internal: read entire file into memory --- */

static int tinyfits__read_file(const char* path, void** out_data,
                               size_t* out_size)
{
    FILE* f = fopen(path, "rb");
    if (!f) return TINYFITS_ERR_OPEN;

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
        return TINYFITS_ERR_READ;
    }

    void* data = TINYFITS_MALLOC((size_t)file_size);
    if (!data)
    {
        fclose(f);
        return TINYFITS_ERR_ALLOC;
    }

    if (fread(data, 1, (size_t)file_size, f) != (size_t)file_size)
    {
        TINYFITS_FREE(data);
        fclose(f);
        return TINYFITS_ERR_READ;
    }
    fclose(f);

    *out_data = data;
    *out_size = (size_t)file_size;
    return TINYFITS_OK;
}

int tinyfits_info_from_memory(TinyFits* info, const void* data, size_t size)
{
    if (!info || !data) return TINYFITS_ERR_INVALID;

    int err;
    tinyfits__parse_headers(info, data, size, &err);
    if (err != TINYFITS_OK)
        tinyfits_free(info);
    return err;
}

int tinyfits_info(TinyFits* info, const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return TINYFITS_ERR_OPEN;

    size_t capacity = TINYFITS_BLOCK_SIZE;
    size_t total = 0;
    uint8_t* buf = (uint8_t*)TINYFITS_MALLOC(capacity);
    if (!buf)
    {
        fclose(f);
        return TINYFITS_ERR_ALLOC;
    }

    int found_end = 0;
    int block_count = 0;
    while (!found_end)
    {
        if (++block_count > 1024)
        {
            TINYFITS_FREE(buf);
            fclose(f);
            return TINYFITS_ERR_INVALID;
        }

        if (total + TINYFITS_BLOCK_SIZE > capacity)
        {
            capacity *= 2;
            uint8_t* newbuf = (uint8_t*)TINYFITS_REALLOC(buf, capacity);
            if (!newbuf)
            {
                TINYFITS_FREE(buf);
                fclose(f);
                return TINYFITS_ERR_ALLOC;
            }
            buf = newbuf;
        }

        size_t nread = fread(buf + total, 1, TINYFITS_BLOCK_SIZE, f);
        if (nread != TINYFITS_BLOCK_SIZE)
        {
            TINYFITS_FREE(buf);
            fclose(f);
            return TINYFITS_ERR_READ;
        }
        total += TINYFITS_BLOCK_SIZE;

        const char* block = (const char*)(buf + total - TINYFITS_BLOCK_SIZE);
        for (int c = 0; c < 36; c++)
        {
            if (memcmp(block + c * 80, "END     ", 8) == 0)
            {
                found_end = 1;
                break;
            }
        }
    }
    fclose(f);

    int result = tinyfits_info_from_memory(info, buf, total);
    TINYFITS_FREE(buf);
    return result;
}

int tinyfits_get_keywords(const TinyFits* info, const char* key,
                         const char** values, int max_values)
{
    if (!info || !key) return 0;
    int count = 0;
    for (int i = 0; i < info->num_keywords; i++)
    {
        if (strcmp(info->keywords[i].key, key) == 0)
        {
            if (values && count < max_values)
                values[count] = info->keywords[i].value;
            count++;
        }
    }
    return count;
}

/* --- Internal: check if a key is reserved (auto-generated by writer) --- */

static int tinyfits__is_reserved_key(const char* key)
{
    if (strcmp(key, "SIMPLE") == 0) return 1;
    if (strcmp(key, "BITPIX") == 0) return 1;
    if (strcmp(key, "NAXIS") == 0) return 1;
    if (strcmp(key, "EXTEND") == 0) return 1;
    if (strcmp(key, "END") == 0) return 1;
    if (strcmp(key, "BZERO") == 0) return 1;
    if (strcmp(key, "BSCALE") == 0) return 1;
    /* NAXIS1, NAXIS2, ... NAXIS9 */
    if (strlen(key) == 6 && strncmp(key, "NAXIS", 5) == 0
        && key[5] >= '1' && key[5] <= '9')
        return 1;
    return 0;
}

/* --- Internal: validate keyword field lengths --- */

static int tinyfits__validate_header_fields(const char* key, const char* value,
                                            const char* comment)
{
    if (!key || strlen(key) > 8) return TINYFITS_ERR_INVALID;
    if (value && strlen(value) > 71) return TINYFITS_ERR_INVALID;
    if (comment && strlen(comment) > 71) return TINYFITS_ERR_INVALID;
    if (tinyfits__is_reserved_key(key)) return TINYFITS_ERR_INVALID;
    return TINYFITS_OK;
}

int tinyfits_set_keyword(TinyFits* info, const char* key,
                        const char* value, const char* comment)
{
    if (!info) return TINYFITS_ERR_INVALID;
    int err = tinyfits__validate_header_fields(key, value, comment);
    if (err != TINYFITS_OK) return err;

    /* Replace first match */
    for (int i = 0; i < info->num_keywords; i++)
    {
        if (strcmp(info->keywords[i].key, key) == 0)
        {
            strncpy(info->keywords[i].value, value ? value : "", 71);
            info->keywords[i].value[71] = '\0';
            strncpy(info->keywords[i].comment, comment ? comment : "", 71);
            info->keywords[i].comment[71] = '\0';
            return TINYFITS_OK;
        }
    }

    /* Not found -- append */
    if (!tinyfits__add_header(info, key, value, comment))
        return TINYFITS_ERR_ALLOC;
    return TINYFITS_OK;
}

int tinyfits_add_keyword(TinyFits* info, const char* key,
                        const char* value, const char* comment)
{
    if (!info) return TINYFITS_ERR_INVALID;
    int err = tinyfits__validate_header_fields(key, value, comment);
    if (err != TINYFITS_OK) return err;

    if (!tinyfits__add_header(info, key, value, comment))
        return TINYFITS_ERR_ALLOC;
    return TINYFITS_OK;
}

int tinyfits_remove_keyword(TinyFits* info, const char* key)
{
    if (!info || !key) return TINYFITS_OK;
    for (int i = 0; i < info->num_keywords; i++)
    {
        if (strcmp(info->keywords[i].key, key) == 0)
        {
            /* Shift remaining keywords down */
            for (int j = i; j < info->num_keywords - 1; j++)
                info->keywords[j] = info->keywords[j + 1];
            info->num_keywords--;
            return TINYFITS_OK;
        }
    }
    return TINYFITS_OK;
}

int tinyfits_to_float(const TinyFits* info, const void* pixels, float* out)
{
    if (!info || !pixels || !out)
        return TINYFITS_ERR_INVALID;
    if (info->pixel_type == TINYFITS_UNKNOWN)
        return TINYFITS_ERR_INVALID;

    size_t n = (size_t)info->width * (size_t)info->height
             * (size_t)info->num_channels;

    switch (info->pixel_type)
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
            const int16_t* src = (const int16_t*)pixels;
            for (size_t i = 0; i < n; i++)
                out[i] = (float)src[i] / 32767.0f;
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
            for (size_t i = 0; i < n; i++)
                out[i] = (float)src[i] / 2147483647.0f;
            break;
        }
        case TINYFITS_UINT32:
        {
            const uint32_t* src = (const uint32_t*)pixels;
            for (size_t i = 0; i < n; i++)
                out[i] = (float)src[i] / 4294967295.0f;
            break;
        }
        case TINYFITS_FLOAT32:
        {
            memcpy(out, pixels, n * sizeof(float));
            break;
        }
        case TINYFITS_FLOAT64:
        {
            const double* src = (const double*)pixels;
            for (size_t i = 0; i < n; i++)
                out[i] = (float)src[i];
            break;
        }
        default:
            return TINYFITS_ERR_INVALID;
    }

    return TINYFITS_OK;
}

int tinyfits_load_from_memory(TinyFits* info, const void* data,
                              size_t size, void** pixels)
{
    if (!info || !data || !pixels) return TINYFITS_ERR_INVALID;
    *pixels = NULL;

    int err;
    const uint8_t* ptr = tinyfits__parse_headers(info, data, size, &err);
    if (!ptr)
    {
        tinyfits_free(info);
        return err;
    }

    const uint8_t* end = (const uint8_t*)data + size;
    size_t img_size = tinyfits_image_size(info);
    if (img_size == 0)
    {
        tinyfits_free(info);
        return TINYFITS_ERR_INVALID;
    }

    size_t num_samples = (size_t)info->width * (size_t)info->height
                       * (size_t)info->num_channels;

    /* On-disk bytes per sample (from bitpix, before XOR) */
    int disk_bps;
    switch (info->bitpix)
    {
        case 8:   disk_bps = 1; break;
        case 16:  disk_bps = 2; break;
        case 32:  disk_bps = 4; break;
        case -32: disk_bps = 4; break;
        case -64: disk_bps = 8; break;
        default:  tinyfits_free(info); return TINYFITS_ERR_BITPIX;
    }

    size_t disk_bytes = num_samples * (size_t)disk_bps;

    /* Overflow check */
    if (disk_bps > 0 && disk_bytes / (size_t)disk_bps != num_samples)
    {
        tinyfits_free(info);
        return TINYFITS_ERR_INVALID;
    }
    int logical_bps = tinyfits__bytes_per_sample(info->pixel_type);
    if (logical_bps > 0 && img_size / (size_t)logical_bps != num_samples)
    {
        tinyfits_free(info);
        return TINYFITS_ERR_INVALID;
    }

    if (disk_bytes > (size_t)(end - ptr))
    {
        tinyfits_free(info);
        return TINYFITS_ERR_READ;
    }

    void* buf = TINYFITS_MALLOC(img_size);
    if (!buf)
    {
        tinyfits_free(info);
        return TINYFITS_ERR_ALLOC;
    }

    switch (info->pixel_type)
    {
        case TINYFITS_UINT8:
        {
            memcpy(buf, ptr, num_samples);
            break;
        }
        case TINYFITS_INT16:
        {
            int16_t* out = (int16_t*)buf;
            for (size_t i = 0; i < num_samples; i++)
                out[i] = (int16_t)tinyfits__read16(ptr + i * 2);
            break;
        }
        case TINYFITS_UINT16:
        {
            /* Byte-swap then XOR sign bit (the XOR IS the BZERO offset) */
            uint16_t* out = (uint16_t*)buf;
            for (size_t i = 0; i < num_samples; i++)
                out[i] = tinyfits__read16(ptr + i * 2) ^ 0x8000;
            break;
        }
        case TINYFITS_INT32:
        {
            int32_t* out = (int32_t*)buf;
            for (size_t i = 0; i < num_samples; i++)
                out[i] = (int32_t)tinyfits__read32(ptr + i * 4);
            break;
        }
        case TINYFITS_UINT32:
        {
            uint32_t* out = (uint32_t*)buf;
            for (size_t i = 0; i < num_samples; i++)
                out[i] = tinyfits__read32(ptr + i * 4) ^ 0x80000000u;
            break;
        }
        case TINYFITS_FLOAT32:
        {
            uint32_t* out = (uint32_t*)buf;
            for (size_t i = 0; i < num_samples; i++)
                out[i] = tinyfits__read32(ptr + i * 4);
            break;
        }
        case TINYFITS_FLOAT64:
        {
            uint64_t* out = (uint64_t*)buf;
            for (size_t i = 0; i < num_samples; i++)
                out[i] = tinyfits__read64(ptr + i * 8);
            break;
        }
        default:
        {
            TINYFITS_FREE(buf);
            tinyfits_free(info);
            return TINYFITS_ERR_INVALID;
        }
    }

    *pixels = buf;
    return TINYFITS_OK;
}

int tinyfits_load(TinyFits* info, const char* path, void** pixels)
{
    if (!pixels) return TINYFITS_ERR_INVALID;
    *pixels = NULL;

    void* file_data;
    size_t file_size;
    int err = tinyfits__read_file(path, &file_data, &file_size);
    if (err != TINYFITS_OK)
        return err;

    err = tinyfits_load_from_memory(info, file_data, file_size, pixels);
    TINYFITS_FREE(file_data);
    return err;
}

/* --- Internal: write helpers --- */

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

static void tinyfits__write_card(uint8_t** p, const char* key, const char* value)
{
    char card[80];
    memset(card, ' ', 80);
    size_t klen = strlen(key);
    if (klen > 8) klen = 8;
    memcpy(card, key, klen);
    card[8] = '=';
    card[9] = ' ';
    size_t vlen = strlen(value);
    if (vlen > 70) vlen = 70;
    memcpy(card + 10, value, vlen);
    memcpy(*p, card, 80);
    *p += 80;
}

static void tinyfits__write_card_int(uint8_t** p, const char* key, int value)
{
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%20d", value);
    tinyfits__write_card(p, key, vbuf);
}

static void tinyfits__write_card_float(uint8_t** p, const char* key, double value)
{
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%20.10g", value);
    tinyfits__write_card(p, key, vbuf);
}

static void tinyfits__write_card_full(uint8_t** p, const TinyFitsKeyword* h)
{
    char card[80];
    memset(card, ' ', 80);

    size_t klen = strlen(h->key);
    if (klen > 8) klen = 8;
    memcpy(card, h->key, klen);

    /* HISTORY, COMMENT, and CONTINUE cards have no '= ' */
    if (strcmp(h->key, "HISTORY") == 0 || strcmp(h->key, "COMMENT") == 0 ||
        strcmp(h->key, "CONTINUE") == 0)
    {
        size_t vlen = strlen(h->value);
        if (vlen > 72) vlen = 72;
        memcpy(card + 8, h->value, vlen);
    }
    else
    {
        card[8] = '=';
        card[9] = ' ';

        /* Determine if value looks numeric or is a string.
         * FITS logical values are exactly "T" or "F". */
        const char* v = h->value;
        int is_string = 1;
        if (v[0] == '-' || v[0] == '+' || v[0] == '.' ||
            (v[0] >= '0' && v[0] <= '9'))
            is_string = 0;
        if ((v[0] == 'T' || v[0] == 'F') && v[1] == '\0')
            is_string = 0;

        if (is_string)
        {
            /* Write as quoted string */
            int pos = 10;
            card[pos++] = '\'';
            for (const char* s = v; *s && pos < 78; s++)
            {
                if (*s == '\'')
                {
                    if (pos < 77) { card[pos++] = '\''; card[pos++] = '\''; }
                }
                else
                {
                    card[pos++] = *s;
                }
            }
            card[pos++] = '\'';
            /* Comment after string */
            if (h->comment[0] && pos < 76)
            {
                card[pos++] = ' ';
                card[pos++] = '/';
                card[pos++] = ' ';
                size_t clen = strlen(h->comment);
                if (clen > (size_t)(80 - pos)) clen = (size_t)(80 - pos);
                memcpy(card + pos, h->comment, clen);
            }
        }
        else
        {
            /* Write numeric/logical value right-justified in columns 11-30 */
            size_t vlen = strlen(v);
            if (vlen > 20) vlen = 20;
            int vpos = 10 + (20 - (int)vlen);
            memcpy(card + vpos, v, vlen);
            /* Comment */
            if (h->comment[0])
            {
                int cpos = 31;
                card[cpos++] = '/';
                card[cpos++] = ' ';
                size_t clen = strlen(h->comment);
                if (clen > (size_t)(80 - cpos)) clen = (size_t)(80 - cpos);
                memcpy(card + cpos, h->comment, clen);
            }
        }
    }

    memcpy(*p, card, 80);
    *p += 80;
}

static void tinyfits__write_end(uint8_t** p)
{
    char card[80];
    memset(card, ' ', 80);
    memcpy(card, "END", 3);
    memcpy(*p, card, 80);
    *p += 80;
}

int tinyfits_save_to_memory(const TinyFits* info, const void* pixels,
                            void** out_data, size_t* out_size,
                            int interleaved)
{
    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;

    if (!info || !pixels || !out_data || !out_size)
        return TINYFITS_ERR_INVALID;
    if (info->width <= 0 || info->height <= 0 || info->num_channels <= 0)
        return TINYFITS_ERR_INVALID;

    int bps = tinyfits__bytes_per_sample(info->pixel_type);
    if (bps == 0) return TINYFITS_ERR_INVALID;

    size_t num_samples = (size_t)info->width * (size_t)info->height
                       * (size_t)info->num_channels;
    size_t data_bytes = num_samples * (size_t)bps;

    /* Determine BITPIX and BZERO */
    int bitpix;
    double bzero = 0.0;
    int need_bzero = 0;
    switch (info->pixel_type)
    {
        case TINYFITS_UINT8:   bitpix = 8;   break;
        case TINYFITS_INT16:   bitpix = 16;  break;
        case TINYFITS_UINT16:  bitpix = 16;  bzero = 32768.0;     need_bzero = 1; break;
        case TINYFITS_INT32:   bitpix = 32;  break;
        case TINYFITS_UINT32:  bitpix = 32;  bzero = 2147483648.0; need_bzero = 1; break;
        case TINYFITS_FLOAT32: bitpix = -32; break;
        case TINYFITS_FLOAT64: bitpix = -64; break;
        default: return TINYFITS_ERR_INVALID;
    }

    /* Estimate header size: mandatory cards + user cards + END, rounded up */
    int mandatory_cards = 5; /* SIMPLE, BITPIX, NAXIS, NAXIS1, NAXIS2 */
    if (info->num_channels > 1) mandatory_cards++; /* NAXIS3 */
    if (need_bzero) mandatory_cards += 2; /* BZERO, BSCALE */
    mandatory_cards++; /* EXTEND */
    int total_cards = mandatory_cards + info->num_keywords + 1; /* +1 for END */
    int header_blocks = (total_cards + 35) / 36;
    size_t header_bytes = (size_t)header_blocks * TINYFITS_BLOCK_SIZE;

    /* Data padded to block boundary */
    size_t data_padded = ((data_bytes + TINYFITS_BLOCK_SIZE - 1)
                         / TINYFITS_BLOCK_SIZE) * TINYFITS_BLOCK_SIZE;

    size_t total_size = header_bytes + data_padded;
    uint8_t* buf = (uint8_t*)TINYFITS_CALLOC(1, total_size);
    if (!buf) return TINYFITS_ERR_ALLOC;

    /* Write header */
    uint8_t* p = buf;

    tinyfits__write_card(&p, "SIMPLE", "                   T");
    tinyfits__write_card_int(&p, "BITPIX", bitpix);
    tinyfits__write_card_int(&p, "NAXIS", (info->num_channels > 1) ? 3 : 2);
    tinyfits__write_card_int(&p, "NAXIS1", info->width);
    tinyfits__write_card_int(&p, "NAXIS2", info->height);
    if (info->num_channels > 1)
        tinyfits__write_card_int(&p, "NAXIS3", info->num_channels);
    if (need_bzero)
    {
        tinyfits__write_card_float(&p, "BZERO", bzero);
        tinyfits__write_card_float(&p, "BSCALE", 1.0);
    }
    tinyfits__write_card(&p, "EXTEND", "                   T");

    /* User keywords (skip any that collide with auto-generated keys) */
    for (int i = 0; i < info->num_keywords; i++)
    {
        if (tinyfits__is_reserved_key(info->keywords[i].key))
            continue;
        tinyfits__write_card_full(&p, &info->keywords[i]);
    }

    tinyfits__write_end(&p);

    /* Write pixel data */
    uint8_t* data_start = buf + header_bytes;

    if (interleaved && info->num_channels > 1)
    {
        /* Deinterleave: RGBRGB... -> RRR...GGG...BBB... */
        int w = info->width;
        int h = info->height;
        int ch = info->num_channels;
        size_t plane_samples = (size_t)w * (size_t)h;
        const uint8_t* src = (const uint8_t*)pixels;

        for (int c = 0; c < ch; c++)
        {
            for (size_t j = 0; j < plane_samples; j++)
            {
                size_t src_idx = j * (size_t)ch + (size_t)c;
                size_t dst_idx = (size_t)c * plane_samples + j;

                switch (info->pixel_type)
                {
                    case TINYFITS_UINT8:
                        data_start[dst_idx] = src[src_idx];
                        break;
                    case TINYFITS_INT16:
                    {
                        int16_t v;
                        memcpy(&v, src + src_idx * 2, 2);
                        uint16_t u;
                        memcpy(&u, &v, 2);
                        tinyfits__write_be16(data_start + dst_idx * 2, u);
                        break;
                    }
                    case TINYFITS_UINT16:
                    {
                        uint16_t v;
                        memcpy(&v, src + src_idx * 2, 2);
                        tinyfits__write_be16(data_start + dst_idx * 2, v ^ 0x8000);
                        break;
                    }
                    case TINYFITS_INT32:
                    {
                        int32_t v;
                        memcpy(&v, src + src_idx * 4, 4);
                        uint32_t u;
                        memcpy(&u, &v, 4);
                        tinyfits__write_be32(data_start + dst_idx * 4, u);
                        break;
                    }
                    case TINYFITS_UINT32:
                    {
                        uint32_t v;
                        memcpy(&v, src + src_idx * 4, 4);
                        tinyfits__write_be32(data_start + dst_idx * 4, v ^ 0x80000000u);
                        break;
                    }
                    case TINYFITS_FLOAT32:
                    {
                        uint32_t v;
                        memcpy(&v, src + src_idx * 4, 4);
                        tinyfits__write_be32(data_start + dst_idx * 4, v);
                        break;
                    }
                    case TINYFITS_FLOAT64:
                    {
                        uint64_t v;
                        memcpy(&v, src + src_idx * 8, 8);
                        tinyfits__write_be64(data_start + dst_idx * 8, v);
                        break;
                    }
                }
            }
        }
    }
    else
    {
        /* Planar: just byte-swap and XOR */
        switch (info->pixel_type)
        {
            case TINYFITS_UINT8:
                memcpy(data_start, pixels, num_samples);
                break;
            case TINYFITS_INT16:
            {
                const int16_t* src = (const int16_t*)pixels;
                for (size_t i = 0; i < num_samples; i++)
                {
                    uint16_t u;
                    memcpy(&u, &src[i], 2);
                    tinyfits__write_be16(data_start + i * 2, u);
                }
                break;
            }
            case TINYFITS_UINT16:
            {
                const uint16_t* src = (const uint16_t*)pixels;
                for (size_t i = 0; i < num_samples; i++)
                    tinyfits__write_be16(data_start + i * 2, src[i] ^ 0x8000);
                break;
            }
            case TINYFITS_INT32:
            {
                const int32_t* src = (const int32_t*)pixels;
                for (size_t i = 0; i < num_samples; i++)
                {
                    uint32_t u;
                    memcpy(&u, &src[i], 4);
                    tinyfits__write_be32(data_start + i * 4, u);
                }
                break;
            }
            case TINYFITS_UINT32:
            {
                const uint32_t* src = (const uint32_t*)pixels;
                for (size_t i = 0; i < num_samples; i++)
                    tinyfits__write_be32(data_start + i * 4, src[i] ^ 0x80000000u);
                break;
            }
            case TINYFITS_FLOAT32:
            {
                const uint32_t* src = (const uint32_t*)pixels;
                for (size_t i = 0; i < num_samples; i++)
                    tinyfits__write_be32(data_start + i * 4, src[i]);
                break;
            }
            case TINYFITS_FLOAT64:
            {
                const uint64_t* src = (const uint64_t*)pixels;
                for (size_t i = 0; i < num_samples; i++)
                    tinyfits__write_be64(data_start + i * 8, src[i]);
                break;
            }
        }
    }

    *out_data = buf;
    *out_size = total_size;
    return TINYFITS_OK;
}

int tinyfits_save(const TinyFits* info, const void* pixels,
                  const char* path, int interleaved)
{
    void* data;
    size_t size;
    int err = tinyfits_save_to_memory(info, pixels, &data, &size, interleaved);
    if (err != TINYFITS_OK)
        return err;

    FILE* f = fopen(path, "wb");
    if (!f)
    {
        tinyfits_free_buffer(data);
        return TINYFITS_ERR_WRITE;
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    tinyfits_free_buffer(data);

    if (written != size)
        return TINYFITS_ERR_WRITE;

    return TINYFITS_OK;
}

#endif /* TINYFITS_IMPLEMENTATION */

#endif /* TINYFITS_H */

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
