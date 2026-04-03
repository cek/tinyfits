/* SPDX-License-Identifier: MIT OR Unlicense */

/*
 * test_tinyfits.c -- self-contained test suite for tinyfits.h
 *
 * Tests are added incrementally as API functions are implemented.
 * Each test generates its own FITS data in memory or on disk.
 */

#define TINYFITS_IMPLEMENTATION
#include "tinyfits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int tests_run = 0;
static int tests_passed = 0;

/* CHECK / CHECK_CLOSE: assertion macros for use inside void test functions.
 * On failure, prints the message and returns from the enclosing function so
 * subsequent code (which often depends on the assertion holding) can't run
 * into undefined behavior.
 */
#define CHECK(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        return; \
    } \
    tests_passed++; \
} while (0)

#define CHECK_CLOSE(a, b, tol, msg) do { \
    tests_run++; \
    if (fabs((double)(a) - (double)(b)) > (tol)) { \
        printf("  FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b)); \
        return; \
    } \
    tests_passed++; \
} while (0)

/* --- Basic declaration and utility tests --- */

static void test_last_error(void)
{
    printf("Testing last_error ...\n");

    /* Zero-initialized header: last_error is NULL. */
    TinyFitsHeader h = {0};
    CHECK(h.last_error == NULL, "last_error NULL on zero-init");

    /* Validation failure populates last_error with a message that
     * reflects the specific cause.
     */
    int err = tinyfits_set_keyword(&h, "BITPIX", "16", "");
    CHECK(err == TINYFITS_ERR_RESERVED_KEYWORD, "set reserved key returns code");
    CHECK(h.last_error != NULL, "reserved-key failure sets last_error");
    CHECK(strstr(h.last_error, "reserved") != NULL, "message mentions 'reserved'");
    const char* prev_msg = h.last_error;

    /* 65 'A's: HIERARCH-class (length > 8), exceeds 63-char HIERARCH cap. */
    err = tinyfits_set_keyword(&h,
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        "v", "");
    CHECK(err == TINYFITS_ERR_KEYWORD_LENGTH, "over-cap key returns code");
    CHECK(h.last_error != NULL, "length failure sets last_error");
    CHECK(h.last_error != prev_msg, "second failure replaces last_error");

    /* Successful call does NOT clear last_error (errno-style: only
     * meaningful after a non-OK return).
     */
    err = tinyfits_set_keyword(&h, "OBJECT", "M51", "");
    CHECK(err == TINYFITS_OK, "set_keyword OK");
    CHECK(h.last_error != NULL, "last_error persists after successful call");

    tinyfits_free_header(&h);

    /* Load failure populates last_error. */
    TinyFitsHeader h2 = {0};
    const char* garbage = "not a fits file at all";
    err = tinyfits_load_header_from_memory(&h2, garbage, strlen(garbage));
    CHECK(err != TINYFITS_OK, "load fails on garbage");
    CHECK(h2.last_error != NULL, "load failure sets last_error");

    /* Save failure populates last_error with a dimension-specific message. */
    TinyFitsHeader h3 = {0};
    h3.width = 0; h3.height = 4; h3.num_channels = 1;
    h3.pixel_type = TINYFITS_UINT8; h3.bscale = 1.0; h3.bzero = 0.0;
    void* fdata; size_t fsize;
    uint8_t pixel = 0;
    err = tinyfits_save_to_memory(&h3, &pixel, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_BAD_DIMENSION, "save fails on zero width");
    CHECK(h3.last_error != NULL, "save failure sets last_error");
    CHECK(strstr(h3.last_error, "dimension") != NULL ||
          strstr(h3.last_error, "Dimension") != NULL,
          "message mentions 'dimension'");

    tinyfits_free_header(&h2);
    tinyfits_free_header(&h3);
}

/* Sweep every public API entry point with NULL for each pointer arg and
 * verify it returns NULL_ARG (or the documented count=0 / size=0 for the
 * non-error-returning queries) instead of crashing. Catches the bug class
 * where an internal helper deref's a header pointer the caller passed as
 * NULL.
 */
static void test_null_args(void)
{
    printf("Testing NULL-arg handling on every public API ...\n");

    TinyFitsHeader h = {0};
    void* px = NULL;
    void* fdata = NULL;
    size_t fsize = 0;
    uint8_t pixel_buf[4] = {0};
    float fbuf[4] = {0};
    const TinyFitsKeyword* kws[4];

    /* Loaders */
    CHECK(tinyfits_load(NULL, "x", &px) == TINYFITS_ERR_NULL_ARG, "load NULL header");
    CHECK(tinyfits_load(&h, NULL, &px) == TINYFITS_ERR_NULL_ARG, "load NULL path");
    CHECK(tinyfits_load(&h, "x", NULL) == TINYFITS_ERR_NULL_ARG, "load NULL pixels");

    CHECK(tinyfits_load_from_memory(NULL, "x", 1, &px) == TINYFITS_ERR_NULL_ARG, "load_from_memory NULL header");
    CHECK(tinyfits_load_from_memory(&h, NULL, 1, &px) == TINYFITS_ERR_NULL_ARG, "load_from_memory NULL data");
    CHECK(tinyfits_load_from_memory(&h, "x", 1, NULL) == TINYFITS_ERR_NULL_ARG, "load_from_memory NULL pixels");

    CHECK(tinyfits_load_header(NULL, "x") == TINYFITS_ERR_NULL_ARG, "load_header NULL header");
    CHECK(tinyfits_load_header(&h, NULL) == TINYFITS_ERR_NULL_ARG, "load_header NULL path");

    CHECK(tinyfits_load_header_from_memory(NULL, "x", 1) == TINYFITS_ERR_NULL_ARG, "load_header_from_memory NULL header");
    CHECK(tinyfits_load_header_from_memory(&h, NULL, 1) == TINYFITS_ERR_NULL_ARG, "load_header_from_memory NULL data");

    /* Savers */
    CHECK(tinyfits_save(NULL, pixel_buf, "x", 0) == TINYFITS_ERR_NULL_ARG, "save NULL header");
    CHECK(tinyfits_save(&h, pixel_buf, NULL, 0) == TINYFITS_ERR_NULL_ARG, "save NULL path");

    CHECK(tinyfits_save_to_memory(NULL, pixel_buf, &fdata, &fsize, 0) == TINYFITS_ERR_NULL_ARG, "save_to_memory NULL header");
    CHECK(tinyfits_save_to_memory(&h, NULL, &fdata, &fsize, 0) == TINYFITS_ERR_NULL_ARG, "save_to_memory NULL pixels");
    CHECK(tinyfits_save_to_memory(&h, pixel_buf, NULL, &fsize, 0) == TINYFITS_ERR_NULL_ARG, "save_to_memory NULL out_data");
    CHECK(tinyfits_save_to_memory(&h, pixel_buf, &fdata, NULL, 0) == TINYFITS_ERR_NULL_ARG, "save_to_memory NULL out_size");

    /* Keyword mutators */
    CHECK(tinyfits_set_keyword(NULL, "K", "v", "") == TINYFITS_ERR_NULL_ARG, "set_keyword NULL header");
    CHECK(tinyfits_set_keyword(&h, NULL, "v", "") == TINYFITS_ERR_NULL_ARG, "set_keyword NULL key");

    CHECK(tinyfits_append_keyword(NULL, "K", "v", "") == TINYFITS_ERR_NULL_ARG, "append_keyword NULL header");
    CHECK(tinyfits_append_keyword(&h, NULL, "v", "") == TINYFITS_ERR_NULL_ARG, "append_keyword NULL key");

    CHECK(tinyfits_add_history(NULL, "text") == TINYFITS_ERR_NULL_ARG, "add_history NULL header");
    CHECK(tinyfits_add_comment(NULL, "text") == TINYFITS_ERR_NULL_ARG, "add_comment NULL header");

    CHECK(tinyfits_remove_keyword(NULL, "K") == TINYFITS_ERR_NULL_ARG, "remove_keyword NULL header");
    CHECK(tinyfits_remove_keyword(&h, NULL) == TINYFITS_ERR_NULL_ARG, "remove_keyword NULL key");

    /* Queries (return NULL/0 on bad input rather than an error code) */
    CHECK(tinyfits_get_keyword(NULL, "K") == NULL, "get_keyword NULL header -> NULL");
    CHECK(tinyfits_get_keyword(&h, NULL) == NULL, "get_keyword NULL key -> NULL");

    CHECK(tinyfits_get_keywords(NULL, "K", kws, 4) == 0, "get_keywords NULL header -> 0");
    CHECK(tinyfits_get_keywords(&h, NULL, kws, 4) == 0, "get_keywords NULL key -> 0");
    /* out=NULL with max=0 is the documented "size first" pattern; should not crash */
    CHECK(tinyfits_get_keywords(&h, "K", NULL, 0) == 0, "get_keywords NULL out, max=0");

    CHECK(tinyfits_image_size(NULL) == 0, "image_size NULL -> 0");

    /* Pixel converters */
    CHECK(tinyfits_to_float_physical(NULL, pixel_buf, fbuf) == TINYFITS_ERR_NULL_ARG, "to_float_physical NULL header");
    CHECK(tinyfits_to_float_physical(&h, NULL, fbuf) == TINYFITS_ERR_NULL_ARG, "to_float_physical NULL pixels");
    CHECK(tinyfits_to_float_physical(&h, pixel_buf, NULL) == TINYFITS_ERR_NULL_ARG, "to_float_physical NULL out");

    CHECK(tinyfits_to_float_normalized(NULL, pixel_buf, fbuf) == TINYFITS_ERR_NULL_ARG, "to_float_normalized NULL header");
    CHECK(tinyfits_to_float_normalized(&h, NULL, fbuf) == TINYFITS_ERR_NULL_ARG, "to_float_normalized NULL pixels");
    CHECK(tinyfits_to_float_normalized(&h, pixel_buf, NULL) == TINYFITS_ERR_NULL_ARG, "to_float_normalized NULL out");

    /* Free functions are documented as safe on NULL */
    tinyfits_free_header(NULL);
    tinyfits_free_buffer(NULL);
    CHECK(1, "free_header(NULL) and free_buffer(NULL) are no-ops");
}

static void test_free_safety(void)
{
    printf("Testing free safety ...\n");

    /* Zero-initialized struct */
    TinyFitsHeader header = {0};
    tinyfits_free_header(&header);
    CHECK(header.keywords == NULL, "keywords NULL after free of zero struct");
    CHECK(header.num_keywords == 0, "num_keywords 0 after free of zero struct");
    CHECK(header.pixel_type == TINYFITS_UNKNOWN, "pixel_type UNKNOWN after free");

    /* Double free */
    tinyfits_free_header(&header);
    CHECK(header.keywords == NULL, "keywords still NULL after double free");

    /* free_buffer with NULL */
    tinyfits_free_buffer(NULL);
    CHECK(1, "free_buffer(NULL) did not crash");
}

static void test_image_size(void)
{
    printf("Testing image_size ...\n");

    TinyFitsHeader header = {0};
    CHECK(tinyfits_image_size(&header) == 0, "zero-initialized returns 0");

    header.width = 100;
    header.height = 200;
    header.num_channels = 1;
    header.pixel_type = TINYFITS_UINT16;
    CHECK(tinyfits_image_size(&header) == 100 * 200 * 1 * 2, "uint16 mono");

    header.pixel_type = TINYFITS_FLOAT32;
    CHECK(tinyfits_image_size(&header) == 100 * 200 * 1 * 4, "float32 mono");

    header.num_channels = 3;
    header.pixel_type = TINYFITS_FLOAT64;
    CHECK(tinyfits_image_size(&header) == 100 * 200 * 3 * 8, "float64 rgb");

    header.pixel_type = TINYFITS_UNKNOWN;
    CHECK(tinyfits_image_size(&header) == 0, "unknown type returns 0");
}

static void test_get_header_empty(void)
{
    printf("Testing get_header on empty struct ...\n");
    TinyFitsHeader header = {0};
    CHECK(tinyfits_get_keyword(&header, "BITPIX") == NULL, "no keywords returns NULL");
}

/* --- FITS generation helpers --- */

typedef struct
{
    uint8_t* data;
    size_t size;
    size_t capacity;
} FitsBuf;

static void fitsbuf_init(FitsBuf* b)
{
    b->capacity = 65536;
    b->data = (uint8_t*)malloc(b->capacity);
    b->size = 0;
}

static void fitsbuf_append(FitsBuf* b, const void* src, size_t n)
{
    while (b->size + n > b->capacity)
    {
        b->capacity *= 2;
        b->data = (uint8_t*)realloc(b->data, b->capacity);
    }
    memcpy(b->data + b->size, src, n);
    b->size += n;
}

static void fitsbuf_pad_to_block(FitsBuf* b)
{
    size_t rem = b->size % TINYFITS_BLOCK_SIZE;
    if (rem != 0)
    {
        size_t pad = TINYFITS_BLOCK_SIZE - rem;
        while (b->size + pad > b->capacity)
        {
            b->capacity *= 2;
            b->data = (uint8_t*)realloc(b->data, b->capacity);
        }
        memset(b->data + b->size, 0, pad);
        b->size += pad;
    }
}

static void fitsbuf_card(FitsBuf* b, const char* keyword, const char* value)
{
    char card[TINYFITS_CARD_SIZE];
    memset(card, ' ', TINYFITS_CARD_SIZE);
    size_t klen = strlen(keyword);
    if (klen > TINYFITS_CARD_KEY_LEN) klen = TINYFITS_CARD_KEY_LEN;
    memcpy(card, keyword, klen);
    card[TINYFITS_CARD_KEY_LEN]     = '=';
    card[TINYFITS_CARD_KEY_LEN + 1] = ' ';
    size_t vlen = strlen(value);
    size_t value_field_max = TINYFITS_CARD_SIZE - TINYFITS_CARD_VALUE_OFFSET;
    if (vlen > value_field_max) vlen = value_field_max;
    memcpy(card + TINYFITS_CARD_VALUE_OFFSET, value, vlen);
    fitsbuf_append(b, card, TINYFITS_CARD_SIZE);
}

static void fitsbuf_card_str(FitsBuf* b, const char* keyword, const char* value)
{
    char vbuf[TINYFITS_CARD_VALUE_MAX_LEN + 1];
    snprintf(vbuf, sizeof(vbuf), "'%s'", value);
    fitsbuf_card(b, keyword, vbuf);
}

static void fitsbuf_card_int(FitsBuf* b, const char* keyword, int value)
{
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%20d", value);
    fitsbuf_card(b, keyword, vbuf);
}

static void fitsbuf_card_float(FitsBuf* b, const char* keyword, double value)
{
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%20.10g", value);
    fitsbuf_card(b, keyword, vbuf);
}

static void fitsbuf_card_with_comment(FitsBuf* b, const char* keyword,
                                      const char* value, const char* comment)
{
    char card[TINYFITS_CARD_SIZE];
    memset(card, ' ', TINYFITS_CARD_SIZE);
    size_t klen = strlen(keyword);
    if (klen > TINYFITS_CARD_KEY_LEN) klen = TINYFITS_CARD_KEY_LEN;
    memcpy(card, keyword, klen);
    card[TINYFITS_CARD_KEY_LEN]     = '=';
    card[TINYFITS_CARD_KEY_LEN + 1] = ' ';
    size_t vlen = strlen(value);
    /* Test-helper-internal cap: leaves comfortable room for the
     * "/ <comment>" tail. Not a FITS-spec limit.
     */
    if (vlen > 30) vlen = 30;
    memcpy(card + TINYFITS_CARD_VALUE_OFFSET, value, vlen);
    size_t cpos = TINYFITS_CARD_VALUE_OFFSET + vlen + 1;
    /* Need at least 2 bytes left for "/ " before the comment text. */
    if (cpos < TINYFITS_CARD_SIZE - 2 && comment && comment[0])
    {
        card[cpos] = '/';
        card[cpos + 1] = ' ';
        size_t clen = strlen(comment);
        if (clen > TINYFITS_CARD_SIZE - cpos - 2)
            clen = TINYFITS_CARD_SIZE - cpos - 2;
        memcpy(card + cpos + 2, comment, clen);
    }
    fitsbuf_append(b, card, TINYFITS_CARD_SIZE);
}

static void fitsbuf_history(FitsBuf* b, const char* text)
{
    char card[TINYFITS_CARD_SIZE];
    memset(card, ' ', TINYFITS_CARD_SIZE);
    memcpy(card, "HISTORY ", TINYFITS_CARD_KEY_LEN);
    size_t tlen = strlen(text);
    /* Free-form payload spans bytes CARD_KEY_LEN..CARD_SIZE-1. */
    size_t freeform_max = TINYFITS_CARD_SIZE - TINYFITS_CARD_KEY_LEN;
    if (tlen > freeform_max) tlen = freeform_max;
    memcpy(card + TINYFITS_CARD_KEY_LEN, text, tlen);
    fitsbuf_append(b, card, TINYFITS_CARD_SIZE);
}

/* Append a CONTINUE card. payload_str is a NUL-terminated byte sequence
 * starting at column 10 (the open quote, value chunk, close quote, and
 * optional " / comment"). Caller is responsible for spec-compliant
 * quote-doubling and the trailing '&' continuation marker.
 */
static void fitsbuf_continue(FitsBuf* b, const char* payload_str)
{
    char card[TINYFITS_CARD_SIZE];
    memset(card, ' ', TINYFITS_CARD_SIZE);
    memcpy(card, "CONTINUE", 8);
    size_t plen = strlen(payload_str);
    size_t max  = TINYFITS_CARD_SIZE - TINYFITS_CARD_VALUE_OFFSET;
    if (plen > max) plen = max;
    memcpy(card + TINYFITS_CARD_VALUE_OFFSET, payload_str, plen);
    fitsbuf_append(b, card, TINYFITS_CARD_SIZE);
}

/* Variant that takes an explicit length, so callers can build payloads
 * with embedded spaces or non-printable bytes that strlen would mis-count.
 */
static void fitsbuf_continue_n(FitsBuf* b, const char* payload, size_t plen)
{
    char card[TINYFITS_CARD_SIZE];
    memset(card, ' ', TINYFITS_CARD_SIZE);
    memcpy(card, "CONTINUE", 8);
    size_t max = TINYFITS_CARD_SIZE - TINYFITS_CARD_VALUE_OFFSET;
    if (plen > max) plen = max;
    memcpy(card + TINYFITS_CARD_VALUE_OFFSET, payload, plen);
    fitsbuf_append(b, card, TINYFITS_CARD_SIZE);
}

/* Append a raw HIERARCH card. The caller writes the entire content,
 * starting with the literal "HIERARCH " prefix; this helper just pads
 * to 80 bytes. Useful for building cards with non-canonical spacing or
 * intentionally malformed structure that the higher-level helpers would
 * canonicalize away.
 */
static void fitsbuf_raw_card(FitsBuf* b, const char* content)
{
    char card[TINYFITS_CARD_SIZE];
    memset(card, ' ', TINYFITS_CARD_SIZE);
    size_t n = strlen(content);
    if (n > TINYFITS_CARD_SIZE) n = TINYFITS_CARD_SIZE;
    memcpy(card, content, n);
    fitsbuf_append(b, card, TINYFITS_CARD_SIZE);
}

static void fitsbuf_end(FitsBuf* b)
{
    char card[TINYFITS_CARD_SIZE];
    memset(card, ' ', TINYFITS_CARD_SIZE);
    memcpy(card, "END", 3);
    fitsbuf_append(b, card, TINYFITS_CARD_SIZE);
    fitsbuf_pad_to_block(b);
}

static void fitsbuf_write(FitsBuf* b, const char* path)
{
    FILE* f = fopen(path, "wb");
    fwrite(b->data, 1, b->size, f);
    fclose(f);
}

static void fitsbuf_free(FitsBuf* b)
{
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

/* --- Standard FITS header helpers --- */

static void fitsbuf_standard_header(FitsBuf* b, int bitpix, int w, int h)
{
    fitsbuf_init(b);
    fitsbuf_card(b, "SIMPLE", "                   T");
    fitsbuf_card_int(b, "BITPIX", bitpix);
    fitsbuf_card_int(b, "NAXIS", 2);
    fitsbuf_card_int(b, "NAXIS1", w);
    fitsbuf_card_int(b, "NAXIS2", h);
}

static void fitsbuf_standard_header_3d(FitsBuf* b, int bitpix, int w, int h, int ch)
{
    fitsbuf_init(b);
    fitsbuf_card(b, "SIMPLE", "                   T");
    fitsbuf_card_int(b, "BITPIX", bitpix);
    fitsbuf_card_int(b, "NAXIS", 3);
    fitsbuf_card_int(b, "NAXIS1", w);
    fitsbuf_card_int(b, "NAXIS2", h);
    fitsbuf_card_int(b, "NAXIS3", ch);
}

/* --- Big-endian write helpers --- */

static void write_be16(uint8_t* p, int16_t v)
{
    uint16_t u;
    memcpy(&u, &v, 2);
    p[0] = (uint8_t)(u >> 8);
    p[1] = (uint8_t)(u);
}

static void write_be32_int(uint8_t* p, int32_t v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    p[0] = (uint8_t)(u >> 24);
    p[1] = (uint8_t)(u >> 16);
    p[2] = (uint8_t)(u >> 8);
    p[3] = (uint8_t)(u);
}

static void write_be32_float(uint8_t* p, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    p[0] = (uint8_t)(u >> 24);
    p[1] = (uint8_t)(u >> 16);
    p[2] = (uint8_t)(u >> 8);
    p[3] = (uint8_t)(u);
}

static void write_be64_double(uint8_t* p, double v)
{
    uint64_t u;
    memcpy(&u, &v, 8);
    p[0] = (uint8_t)(u >> 56);
    p[1] = (uint8_t)(u >> 48);
    p[2] = (uint8_t)(u >> 40);
    p[3] = (uint8_t)(u >> 32);
    p[4] = (uint8_t)(u >> 24);
    p[5] = (uint8_t)(u >> 16);
    p[6] = (uint8_t)(u >> 8);
    p[7] = (uint8_t)(u);
}

/* --- Header parsing and header tests --- */

static void test_info_uint16(void)
{
    printf("Testing header on uint16 file ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 64, 48);
    fitsbuf_card_float(&b, "BZERO", 32768.0);
    fitsbuf_card_float(&b, "BSCALE", 1.0);
    fitsbuf_card_str(&b, "INSTRUME", "TestCam");
    fitsbuf_end(&b);
    /* No pixel data needed for header */

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_OK, "info_from_memory succeeds");
    CHECK(header.width == 64, "width");
    CHECK(header.height == 48, "height");
    CHECK(header.num_channels == 1, "num_channels");
    CHECK(header.bitpix == 16, "bitpix");
    CHECK(header.pixel_type == TINYFITS_UINT16, "pixel_type is UINT16");

    /* BZERO/BSCALE should be stripped */
    CHECK(tinyfits_get_keyword(&header, "BZERO") == NULL, "BZERO stripped");
    CHECK(tinyfits_get_keyword(&header, "BSCALE") == NULL, "BSCALE stripped");

    /* Other keywords should be present */
    const TinyFitsKeyword* instrume = tinyfits_get_keyword(&header, "INSTRUME");
    CHECK(instrume != NULL, "INSTRUME present");
    if (instrume->value) CHECK(strcmp(instrume->value, "TestCam") == 0, "INSTRUME value");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_info_float32(void)
{
    printf("Testing header on float32 file ...\n");

    FitsBuf b;
    fitsbuf_standard_header_3d(&b, -32, 100, 200, 3);
    fitsbuf_end(&b);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_OK, "header succeeds");
    CHECK(header.width == 100, "width");
    CHECK(header.height == 200, "height");
    CHECK(header.num_channels == 3, "num_channels");
    CHECK(header.pixel_type == TINYFITS_FLOAT32, "pixel_type is FLOAT32");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_info_missing_bzero(void)
{
    printf("Testing header with missing BZERO/BSCALE ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 10, 10);
    fitsbuf_end(&b);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_OK, "header succeeds with no BZERO/BSCALE");
    CHECK(header.pixel_type == TINYFITS_INT16, "pixel_type is INT16 (default BZERO=0)");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_info_nonstandard_bscale(void)
{
    printf("Testing header with non-standard BSCALE ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 10, 10);
    fitsbuf_card_float(&b, "BSCALE", 0.5);
    fitsbuf_end(&b);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_OK, "non-standard BSCALE accepted");
    CHECK(header.pixel_type == TINYFITS_INT16, "BITPIX=16 + non-default BSCALE -> INT16");
    CHECK(header.bscale == 0.5, "bscale recorded on struct");
    CHECK(header.bzero == 0.0, "bzero default recorded on struct");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_info_nonstandard_bzero(void)
{
    printf("Testing header with non-standard BZERO ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 10, 10);
    fitsbuf_card_float(&b, "BZERO", 100.0);
    fitsbuf_end(&b);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_OK, "non-standard BZERO accepted");
    CHECK(header.pixel_type == TINYFITS_INT16, "BITPIX=16 + non-canonical BZERO -> INT16");
    CHECK(header.bscale == 1.0, "bscale=1 recorded on struct");
    CHECK(header.bzero == 100.0, "bzero recorded on struct");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_info_invalid_naxis(void)
{
    printf("Testing header with NAXIS=1 ...\n");

    /* NAXIS=1 file with the declared data block (100 int16 values) so
     * the walker can correctly skip past it. NAXIS=1 is not image-shaped
     * so the walker reports ERR_NO_IMAGE rather than rejecting the
     * file outright.
     */
    FitsBuf b;
    fitsbuf_init(&b);
    fitsbuf_card(&b, "SIMPLE", "                   T");
    fitsbuf_card_int(&b, "BITPIX", 16);
    fitsbuf_card_int(&b, "NAXIS", 1);
    fitsbuf_card_int(&b, "NAXIS1", 100);
    fitsbuf_end(&b);
    /* 100 int16 = 200 bytes of pixel data */
    char zeros[200] = {0};
    fitsbuf_append(&b, zeros, sizeof(zeros));
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_ERR_NO_IMAGE, "NAXIS=1 walked past, no image found");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_info_header_comments(void)
{
    printf("Testing header comment parsing ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 4, 4);
    fitsbuf_card_with_comment(&b, "EXPTIME", "              180.0", "Exposure time in seconds");
    fitsbuf_end(&b);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_OK, "header succeeds");

    /* Find EXPTIME and check comment */
    int found = 0;
    for (int i = 0; i < header.num_keywords; i++)
    {
        if (strcmp(header.keywords[i].key, "EXPTIME") == 0)
        {
            found = 1;
            CHECK(strcmp(header.keywords[i].comment, "Exposure time in seconds") == 0,
                  "comment parsed correctly");
            break;
        }
    }
    CHECK(found, "EXPTIME header found");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_info_history(void)
{
    printf("Testing HISTORY card parsing ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 4, 4);
    fitsbuf_history(&b, "Calibrated with master dark");
    fitsbuf_history(&b, "Stacked 54 frames");
    fitsbuf_end(&b);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_OK, "header succeeds");

    /* Count HISTORY entries */
    int count = 0;
    for (int i = 0; i < header.num_keywords; i++)
    {
        if (strcmp(header.keywords[i].key, "HISTORY") == 0)
        {
            if (count == 0)
                CHECK(strcmp(header.keywords[i].value, "Calibrated with master dark") == 0,
                      "first HISTORY value");
            if (count == 1)
                CHECK(strcmp(header.keywords[i].value, "Stacked 54 frames") == 0,
                      "second HISTORY value");
            CHECK(header.keywords[i].comment[0] == '\0', "HISTORY comment is empty");
            count++;
        }
    }
    CHECK(count == 2, "two HISTORY entries");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_info_struct_reuse(void)
{
    printf("Testing struct reuse without free ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 4, 4);
    fitsbuf_card_str(&b, "OBJECT", "First");
    fitsbuf_end(&b);

    FitsBuf b2;
    fitsbuf_standard_header(&b2, -32, 8, 8);
    fitsbuf_card_str(&b2, "OBJECT", "Second");
    fitsbuf_end(&b2);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_OK, "first header succeeds");
    CHECK(header.width == 4, "first width");

    /* Reuse -- must free first to avoid leaking keywords */
    tinyfits_free_header(&header);
    err = tinyfits_load_header_from_memory(&header, b2.data, b2.size);
    CHECK(err == TINYFITS_OK, "second header succeeds");
    CHECK(header.width == 8, "second width");
    CHECK(header.pixel_type == TINYFITS_FLOAT32, "second pixel_type");

    const TinyFitsKeyword* obj = tinyfits_get_keyword(&header, "OBJECT");
    CHECK(obj != NULL && strcmp(obj->value, "Second") == 0, "second OBJECT value");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
    fitsbuf_free(&b2);
}

static void test_info_not_fits(void)
{
    printf("Testing header on non-FITS data ...\n");

    const char* garbage = "This is not a FITS file at all";
    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, garbage, strlen(garbage));
    CHECK(err == TINYFITS_ERR_NOT_FITS, "non-FITS rejected");
}

static void test_info_uint32(void)
{
    printf("Testing header on uint32 (BZERO=2147483648) ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 32, 10, 10);
    fitsbuf_card_float(&b, "BZERO", 2147483648.0);
    fitsbuf_card_float(&b, "BSCALE", 1.0);
    fitsbuf_end(&b);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_OK, "header succeeds");
    CHECK(header.pixel_type == TINYFITS_UINT32, "pixel_type is UINT32");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_info_all_pixel_types(void)
{
    printf("Testing pixel_type resolution for all types ...\n");

    struct { int bitpix; double bzero; int expected; } cases[] = {
        {   8,          0.0, TINYFITS_UINT8   },
        {  16,          0.0, TINYFITS_INT16   },
        {  16,      32768.0, TINYFITS_UINT16  },
        {  32,          0.0, TINYFITS_INT32   },
        {  32, 2147483648.0, TINYFITS_UINT32  },
        { -32,          0.0, TINYFITS_FLOAT32 },
        { -64,          0.0, TINYFITS_FLOAT64 },
    };
    int ncases = sizeof(cases) / sizeof(cases[0]);

    for (int t = 0; t < ncases; t++)
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, cases[t].bitpix, 4, 4);
        if (cases[t].bzero != 0.0)
            fitsbuf_card_float(&b, "BZERO", cases[t].bzero);
        fitsbuf_end(&b);

        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
        CHECK(err == TINYFITS_OK, "header succeeds");
        CHECK(header.pixel_type == cases[t].expected, "pixel_type matches");

        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }
}

/* --- Header mutation and utility tests --- */

static void test_set_header(void)
{
    printf("Testing set_header ...\n");

    TinyFitsHeader header = {0};

    int err = tinyfits_set_keyword(&header, "OBJECT", "M31", "Andromeda Galaxy");
    CHECK(err == TINYFITS_OK, "set new header");
    CHECK(header.num_keywords == 1, "one header");

    const TinyFitsKeyword* val = tinyfits_get_keyword(&header, "OBJECT");
    CHECK(val != NULL && strcmp(val->value, "M31") == 0, "value is M31");
    CHECK(strcmp(header.keywords[0].comment, "Andromeda Galaxy") == 0, "comment set");

    /* Replace existing */
    err = tinyfits_set_keyword(&header, "OBJECT", "M42", "Orion Nebula");
    CHECK(err == TINYFITS_OK, "replace succeeds");
    CHECK(header.num_keywords == 1, "still one header");
    val = tinyfits_get_keyword(&header, "OBJECT");
    CHECK(val != NULL && strcmp(val->value, "M42") == 0, "value replaced");

    /* Add another */
    err = tinyfits_set_keyword(&header, "FILTER", "Ha", "");
    CHECK(err == TINYFITS_OK, "add second header");
    CHECK(header.num_keywords == 2, "two keywords");

    tinyfits_free_header(&header);
}

static void test_add_header(void)
{
    printf("Testing add_header ...\n");

    TinyFitsHeader header = {0};

    tinyfits_append_keyword(&header, "HISTORY", "Step 1: calibrate", "");
    tinyfits_append_keyword(&header, "HISTORY", "Step 2: stack", "");
    tinyfits_append_keyword(&header, "HISTORY", "Step 3: stretch", "");

    CHECK(header.num_keywords == 3, "three HISTORY entries");

    /* All three should be retrievable */
    int count = 0;
    for (int i = 0; i < header.num_keywords; i++)
    {
        if (strcmp(header.keywords[i].key, "HISTORY") == 0)
            count++;
    }
    CHECK(count == 3, "three HISTORY keys found");

    tinyfits_free_header(&header);
}

static void test_remove_header(void)
{
    printf("Testing remove_header ...\n");

    TinyFitsHeader header = {0};
    tinyfits_set_keyword(&header, "AAA", "1", "");
    tinyfits_set_keyword(&header, "BBB", "2", "");
    tinyfits_set_keyword(&header, "CCC", "3", "");
    CHECK(header.num_keywords == 3, "three keywords");

    tinyfits_remove_keyword(&header, "BBB");
    CHECK(header.num_keywords == 2, "two keywords after remove");
    CHECK(tinyfits_get_keyword(&header, "BBB") == NULL, "BBB removed");
    CHECK(tinyfits_get_keyword(&header, "AAA") != NULL, "AAA still present");
    CHECK(tinyfits_get_keyword(&header, "CCC") != NULL, "CCC still present");

    /* Remove nonexistent -- no-op */
    tinyfits_remove_keyword(&header, "ZZZ");
    CHECK(header.num_keywords == 2, "still two keywords");

    tinyfits_free_header(&header);
}

static void test_get_headers(void)
{
    printf("Testing get_headers ...\n");

    TinyFitsHeader header = {0};
    tinyfits_append_keyword(&header, "HISTORY", "first", "");
    tinyfits_append_keyword(&header, "OBJECT", "M31", "");
    tinyfits_append_keyword(&header, "HISTORY", "second", "");
    tinyfits_append_keyword(&header, "HISTORY", "third", "");

    /* Count only */
    int count = tinyfits_get_keywords(&header, "HISTORY", NULL, 0);
    CHECK(count == 3, "3 HISTORY entries");

    /* Retrieve */
    const TinyFitsKeyword* vals[4];
    int n = tinyfits_get_keywords(&header, "HISTORY", vals, 4);
    CHECK(n == 3, "returns 3");
    CHECK(strcmp(vals[0]->value, "first") == 0, "first value");
    CHECK(strcmp(vals[1]->value, "second") == 0, "second value");
    CHECK(strcmp(vals[2]->value, "third") == 0, "third value");

    /* Partial retrieve */
    const TinyFitsKeyword* vals2[2];
    n = tinyfits_get_keywords(&header, "HISTORY", vals2, 2);
    CHECK(n == 3, "total count is 3 even with max_values=2");
    CHECK(strcmp(vals2[0]->value, "first") == 0, "partial first");
    CHECK(strcmp(vals2[1]->value, "second") == 0, "partial second");

    /* No matches */
    n = tinyfits_get_keywords(&header, "MISSING", NULL, 0);
    CHECK(n == 0, "0 for missing key");

    tinyfits_free_header(&header);
}

static void test_reserved_key_rejection(void)
{
    printf("Testing reserved key rejection ...\n");

    TinyFitsHeader header = {0};

    CHECK(tinyfits_set_keyword(&header, "SIMPLE", "T", "") == TINYFITS_ERR_RESERVED_KEYWORD, "SIMPLE rejected");
    CHECK(tinyfits_set_keyword(&header, "BITPIX", "16", "") == TINYFITS_ERR_RESERVED_KEYWORD, "BITPIX rejected");
    CHECK(tinyfits_set_keyword(&header, "NAXIS", "2", "") == TINYFITS_ERR_RESERVED_KEYWORD, "NAXIS rejected");
    CHECK(tinyfits_set_keyword(&header, "NAXIS1", "100", "") == TINYFITS_ERR_RESERVED_KEYWORD, "NAXIS1 rejected");
    CHECK(tinyfits_set_keyword(&header, "NAXIS3", "3", "") == TINYFITS_ERR_RESERVED_KEYWORD, "NAXIS3 rejected");
    CHECK(tinyfits_set_keyword(&header, "BZERO", "32768", "") == TINYFITS_ERR_RESERVED_KEYWORD, "BZERO rejected");
    CHECK(tinyfits_set_keyword(&header, "BSCALE", "1", "") == TINYFITS_ERR_RESERVED_KEYWORD, "BSCALE rejected");
    CHECK(tinyfits_set_keyword(&header, "EXTEND", "T", "") == TINYFITS_ERR_RESERVED_KEYWORD, "EXTEND rejected");
    CHECK(tinyfits_set_keyword(&header, "END", "", "") == TINYFITS_ERR_RESERVED_KEYWORD, "END rejected");

    /* add_header should also reject */
    CHECK(tinyfits_append_keyword(&header, "BZERO", "0", "") == TINYFITS_ERR_RESERVED_KEYWORD, "add BZERO rejected");

    /* Non-reserved should succeed */
    CHECK(tinyfits_set_keyword(&header, "OBJECT", "M31", "") == TINYFITS_OK, "OBJECT ok");
    CHECK(tinyfits_set_keyword(&header, "HISTORY", "test", "") == TINYFITS_OK, "HISTORY ok via set");
    CHECK(tinyfits_append_keyword(&header, "COMMENT", "test", "") == TINYFITS_OK, "COMMENT ok via add");

    CHECK(header.num_keywords == 3, "three non-reserved keywords");

    tinyfits_free_header(&header);
}

static void test_header_field_validation(void)
{
    printf("Testing header field length validation ...\n");

    TinyFitsHeader header = {0};

    /* Standard 8-char namespace: keys with no space and length <= 8 are
     * standard. Keys with no space and length > 8 are HIERARCH-class
     * and accepted up to TINYFITS_HIERARCH_KEY_MAX chars.
     */
    char over_cap[TINYFITS_HIERARCH_KEY_MAX + 2];
    memset(over_cap, 'A', TINYFITS_HIERARCH_KEY_MAX + 1);
    over_cap[TINYFITS_HIERARCH_KEY_MAX + 1] = '\0';
    CHECK(tinyfits_set_keyword(&header, over_cap, "v", "")
          == TINYFITS_ERR_KEYWORD_LENGTH,
          "over-cap HIERARCH-class key rejected");

    char at_cap[TINYFITS_HIERARCH_KEY_MAX + 1];
    memset(at_cap, 'A', TINYFITS_HIERARCH_KEY_MAX);
    at_cap[TINYFITS_HIERARCH_KEY_MAX] = '\0';
    CHECK(tinyfits_set_keyword(&header, at_cap, "v", "") == TINYFITS_OK,
          "at-cap HIERARCH-class key accepted");

    /* Caller passing the literal "HIERARCH " prefix: rejected. */
    CHECK(tinyfits_set_keyword(&header, "HIERARCH FOO BAR", "v", "")
          == TINYFITS_ERR_KEYWORD_LENGTH,
          "explicit 'HIERARCH ' prefix rejected");

    /* String value lengths are not capped by set_keyword; long values are
     * chained via CONTINUE at save time.
     */
    char longval[201];
    memset(longval, 'x', 200);
    longval[200] = '\0';
    CHECK(tinyfits_set_keyword(&header, "TEST", longval, "") == TINYFITS_OK,
          "long value accepted (CONTINUE chain on write)");

    char longcmt[201];
    memset(longcmt, 'y', 200);
    longcmt[200] = '\0';
    CHECK(tinyfits_set_keyword(&header, "TEST", "v", longcmt) == TINYFITS_OK,
          "long comment accepted (writer truncates)");

    char ok_max[TINYFITS_CARD_VALUE_MAX_LEN + 1];
    memset(ok_max, 'z', TINYFITS_CARD_VALUE_MAX_LEN);
    ok_max[TINYFITS_CARD_VALUE_MAX_LEN] = '\0';
    CHECK(tinyfits_set_keyword(&header, "TEST", ok_max, "") == TINYFITS_OK,
          "max-length-1-card value accepted");

    tinyfits_free_header(&header);
}

static void test_to_float(void)
{
    printf("Testing to_float ...\n");

    /* Build a uint16 image and load it */
    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 3, 1);
    fitsbuf_card_float(&b, "BZERO", 32768.0);
    fitsbuf_card_float(&b, "BSCALE", 1.0);
    fitsbuf_end(&b);
    uint8_t pixdata[6];
    write_be16(pixdata + 0, -32768); /* physical 0 */
    write_be16(pixdata + 2, 0);      /* physical 32768 */
    write_be16(pixdata + 4, 32767);  /* physical 65535 */
    fitsbuf_append(&b, pixdata, sizeof(pixdata));
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    /* normalized: storage range [0, 65535] mapped to [0, 1] */
    float out[3];
    err = tinyfits_to_float_normalized(&header, pixels, out);
    CHECK(err == TINYFITS_OK, "to_float_normalized succeeds");
    CHECK_CLOSE(out[0], 0.0f, 0.01f, "normalized[0] = 0");
    CHECK_CLOSE(out[1], 0.5f, 0.01f, "normalized[1] = 0.5");
    CHECK_CLOSE(out[2], 1.0f, 0.01f, "normalized[2] = 1.0");

    /* physical: bscale=1, bzero=0 (post-unsigned-conversion) -> direct cast */
    err = tinyfits_to_float_physical(&header, pixels, out);
    CHECK(err == TINYFITS_OK, "to_float_physical succeeds");
    CHECK_CLOSE(out[0], 0.0f, 0.5f, "physical[0] = 0");
    CHECK_CLOSE(out[1], 32768.0f, 0.5f, "physical[1] = 32768");
    CHECK_CLOSE(out[2], 65535.0f, 0.5f, "physical[2] = 65535");

    /* Float32 identity through physical (default scaling = memcpy) */
    TinyFitsHeader info2 = {0};
    info2.width = 2;
    info2.height = 1;
    info2.num_channels = 1;
    info2.pixel_type = TINYFITS_FLOAT32;
    info2.bscale = 1.0;
    info2.bzero = 0.0;
    float fpx[] = {1.5f, -3.0f};
    float fout[2];
    err = tinyfits_to_float_physical(&info2, fpx, fout);
    CHECK(err == TINYFITS_OK, "float32 identity succeeds");
    CHECK_CLOSE(fout[0], 1.5f, 1e-6f, "float32 identity [0]");
    CHECK_CLOSE(fout[1], -3.0f, 1e-6f, "float32 identity [1]");

    /* normalized rejects float pixel types */
    err = tinyfits_to_float_normalized(&info2, fpx, fout);
    CHECK(err == TINYFITS_ERR_BAD_PIXEL_TYPE, "normalized rejects FLOAT32");

    /* Error case: UNKNOWN pixel_type rejected by both */
    TinyFitsHeader info3 = {0};
    info3.width = 1;
    info3.height = 1;
    info3.num_channels = 1;
    err = tinyfits_to_float_physical(&info3, fpx, fout);
    CHECK(err == TINYFITS_ERR_BAD_PIXEL_TYPE, "physical rejects UNKNOWN");
    err = tinyfits_to_float_normalized(&info3, fpx, fout);
    CHECK(err == TINYFITS_ERR_BAD_PIXEL_TYPE, "normalized rejects UNKNOWN");

    /* NULL arg rejection */
    err = tinyfits_to_float_physical(NULL, fpx, fout);
    CHECK(err == TINYFITS_ERR_NULL_ARG, "physical rejects NULL header");
    err = tinyfits_to_float_normalized(&info2, NULL, fout);
    CHECK(err == TINYFITS_ERR_NULL_ARG, "normalized rejects NULL pixels");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

/* --- Pixel loading tests --- */

static void test_load_uint8(void)
{
    printf("Testing load uint8 ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 4, 2);
    fitsbuf_end(&b);
    uint8_t pix[] = {10, 20, 30, 40, 50, 60, 70, 80};
    fitsbuf_append(&b, pix, sizeof(pix));
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(header.pixel_type == TINYFITS_UINT8, "pixel_type");
    CHECK(header.width == 4 && header.height == 2, "dimensions");

    uint8_t* px = (uint8_t*)pixels;
    CHECK(px[0] == 10 && px[3] == 40 && px[7] == 80, "pixel values");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_load_int16(void)
{
    printf("Testing load int16 ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 4, 1);
    fitsbuf_end(&b);
    uint8_t pixdata[8];
    write_be16(pixdata + 0, -100);
    write_be16(pixdata + 2, 0);
    write_be16(pixdata + 4, 100);
    write_be16(pixdata + 6, 32767);
    fitsbuf_append(&b, pixdata, sizeof(pixdata));
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(header.pixel_type == TINYFITS_INT16, "pixel_type");

    int16_t* px = (int16_t*)pixels;
    CHECK(px[0] == -100, "px[0]");
    CHECK(px[1] == 0, "px[1]");
    CHECK(px[2] == 100, "px[2]");
    CHECK(px[3] == 32767, "px[3]");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_load_uint16(void)
{
    printf("Testing load uint16 (BZERO=32768) ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 4, 1);
    fitsbuf_card_float(&b, "BZERO", 32768.0);
    fitsbuf_card_float(&b, "BSCALE", 1.0);
    fitsbuf_end(&b);

    /* On disk: signed int16. Physical = stored + 32768.
     * stored=-32768 -> physical=0, stored=0 -> physical=32768,
     * stored=32767 -> physical=65535
     */
    uint8_t pixdata[8];
    write_be16(pixdata + 0, -32768); /* physical 0 */
    write_be16(pixdata + 2, -1);     /* physical 32767 */
    write_be16(pixdata + 4, 0);      /* physical 32768 */
    write_be16(pixdata + 6, 32767);  /* physical 65535 */
    fitsbuf_append(&b, pixdata, sizeof(pixdata));
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(header.pixel_type == TINYFITS_UINT16, "pixel_type");

    uint16_t* px = (uint16_t*)pixels;
    CHECK(px[0] == 0, "physical 0");
    CHECK(px[1] == 32767, "physical 32767");
    CHECK(px[2] == 32768, "physical 32768");
    CHECK(px[3] == 65535, "physical 65535");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_load_int32(void)
{
    printf("Testing load int32 ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 32, 2, 1);
    fitsbuf_end(&b);
    uint8_t pixdata[8];
    write_be32_int(pixdata + 0, -1000000);
    write_be32_int(pixdata + 4, 1000000);
    fitsbuf_append(&b, pixdata, sizeof(pixdata));
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(header.pixel_type == TINYFITS_INT32, "pixel_type");

    int32_t* px = (int32_t*)pixels;
    CHECK(px[0] == -1000000, "px[0]");
    CHECK(px[1] == 1000000, "px[1]");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_load_float32(void)
{
    printf("Testing load float32 ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, -32, 3, 1);
    fitsbuf_end(&b);
    uint8_t pixdata[12];
    write_be32_float(pixdata + 0, 0.0f);
    write_be32_float(pixdata + 4, 1.5f);
    write_be32_float(pixdata + 8, -42.25f);
    fitsbuf_append(&b, pixdata, sizeof(pixdata));
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(header.pixel_type == TINYFITS_FLOAT32, "pixel_type");

    float* px = (float*)pixels;
    CHECK_CLOSE(px[0], 0.0f, 1e-6, "px[0]");
    CHECK_CLOSE(px[1], 1.5f, 1e-6, "px[1]");
    CHECK_CLOSE(px[2], -42.25f, 1e-6, "px[2]");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_load_float64(void)
{
    printf("Testing load float64 ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, -64, 2, 1);
    fitsbuf_end(&b);
    uint8_t pixdata[16];
    write_be64_double(pixdata + 0, 3.141592653589793);
    write_be64_double(pixdata + 8, -1.0e-15);
    fitsbuf_append(&b, pixdata, sizeof(pixdata));
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(header.pixel_type == TINYFITS_FLOAT64, "pixel_type");

    double* px = (double*)pixels;
    CHECK_CLOSE(px[0], 3.141592653589793, 1e-15, "px[0]");
    CHECK_CLOSE(px[1], -1.0e-15, 1e-30, "px[1]");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_load_3channel(void)
{
    printf("Testing load 3-channel int16 ...\n");

    FitsBuf b;
    fitsbuf_standard_header_3d(&b, 16, 2, 2, 3);
    fitsbuf_end(&b);

    /* 3 channels, 2x2 each = 12 samples, channel-planar */
    uint8_t pixdata[24];
    /* Ch0 (R): 10, 20, 30, 40 */
    write_be16(pixdata + 0, 10);
    write_be16(pixdata + 2, 20);
    write_be16(pixdata + 4, 30);
    write_be16(pixdata + 6, 40);
    /* Ch1 (G): 110, 120, 130, 140 */
    write_be16(pixdata + 8, 110);
    write_be16(pixdata + 10, 120);
    write_be16(pixdata + 12, 130);
    write_be16(pixdata + 14, 140);
    /* Ch2 (B): 210, 220, 230, 240 */
    write_be16(pixdata + 16, 210);
    write_be16(pixdata + 18, 220);
    write_be16(pixdata + 20, 230);
    write_be16(pixdata + 22, 240);
    fitsbuf_append(&b, pixdata, sizeof(pixdata));
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(header.num_channels == 3, "3 channels");

    int16_t* px = (int16_t*)pixels;
    int plane = 2 * 2;
    CHECK(px[0 * plane + 0] == 10, "R[0]");
    CHECK(px[0 * plane + 3] == 40, "R[3]");
    CHECK(px[1 * plane + 0] == 110, "G[0]");
    CHECK(px[2 * plane + 3] == 240, "B[3]");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_load_truncated(void)
{
    printf("Testing load on truncated file ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 100, 100);
    fitsbuf_end(&b);
    /* Only 4 bytes of pixel data instead of 100*100*2=20000 */
    uint8_t tiny[4] = {0};
    fitsbuf_append(&b, tiny, sizeof(tiny));
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_ERR_TRUNCATED, "truncated file rejected");
    CHECK(pixels == NULL, "pixels is NULL on failure");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_load_null_pixels(void)
{
    printf("Testing load with NULL pixels pointer ...\n");

    TinyFitsHeader header = {0};
    int err = tinyfits_load_from_memory(&header, "x", 1, NULL);
    CHECK(err == TINYFITS_ERR_NULL_ARG, "NULL pixels rejected");
}

/* --- Writing and round-trip tests --- */

static void test_roundtrip_uint8(void)
{
    printf("Testing roundtrip uint8 ...\n");
    uint8_t src[] = {0, 128, 255, 42};
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT8;
    w.bscale = 1.0; w.bzero = 0.0;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(r.pixel_type == TINYFITS_UINT8, "pixel_type");

    uint8_t* px = (uint8_t*)pixels;
    CHECK(px[0] == 0 && px[1] == 128 && px[2] == 255 && px[3] == 42, "values match");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_int16(void)
{
    printf("Testing roundtrip int16 ...\n");
    int16_t src[] = {-32768, -1, 0, 32767};
    TinyFitsHeader w = {0};
    w.width = 4; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_INT16;
    w.bscale = 1.0; w.bzero = 0.0;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    int16_t* px = (int16_t*)pixels;
    CHECK(px[0] == -32768 && px[1] == -1 && px[2] == 0 && px[3] == 32767, "values match");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_uint16(void)
{
    printf("Testing roundtrip uint16 ...\n");
    uint16_t src[] = {0, 1000, 32768, 65535};
    TinyFitsHeader w = {0};
    w.width = 4; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT16;
    w.bscale = 1.0; w.bzero = 0.0;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(r.pixel_type == TINYFITS_UINT16, "pixel_type");

    uint16_t* px = (uint16_t*)pixels;
    CHECK(px[0] == 0 && px[1] == 1000 && px[2] == 32768 && px[3] == 65535, "values match");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_int32(void)
{
    printf("Testing roundtrip int32 ...\n");
    int32_t src[] = {-2000000, 0, 2000000};
    TinyFitsHeader w = {0};
    w.width = 3; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_INT32;
    w.bscale = 1.0; w.bzero = 0.0;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    int32_t* px = (int32_t*)pixels;
    CHECK(px[0] == -2000000 && px[1] == 0 && px[2] == 2000000, "values match");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_uint32(void)
{
    printf("Testing roundtrip uint32 ...\n");
    uint32_t src[] = {0, 2147483648u, 4294967295u};
    TinyFitsHeader w = {0};
    w.width = 3; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT32;
    w.bscale = 1.0; w.bzero = 0.0;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(r.pixel_type == TINYFITS_UINT32, "pixel_type");

    uint32_t* px = (uint32_t*)pixels;
    CHECK(px[0] == 0 && px[1] == 2147483648u && px[2] == 4294967295u, "values match");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_float32(void)
{
    printf("Testing roundtrip float32 ...\n");
    float src[] = {0.0f, 1.5f, -42.25f, 1e10f};
    TinyFitsHeader w = {0};
    w.width = 4; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_FLOAT32;
    w.bscale = 1.0; w.bzero = 0.0;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    float* px = (float*)pixels;
    CHECK_CLOSE(px[0], 0.0f, 1e-10, "px[0]");
    CHECK_CLOSE(px[1], 1.5f, 1e-10, "px[1]");
    CHECK_CLOSE(px[2], -42.25f, 1e-10, "px[2]");
    CHECK_CLOSE(px[3], 1e10f, 1.0f, "px[3]");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_float64(void)
{
    printf("Testing roundtrip float64 ...\n");
    double src[] = {3.141592653589793, -1.0e-15};
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_FLOAT64;
    w.bscale = 1.0; w.bzero = 0.0;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    double* px = (double*)pixels;
    CHECK_CLOSE(px[0], 3.141592653589793, 1e-15, "px[0]");
    CHECK_CLOSE(px[1], -1.0e-15, 1e-30, "px[1]");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_3channel(void)
{
    printf("Testing roundtrip 3-channel uint16 ...\n");
    /* 2x2, 3 channels, planar: R plane, G plane, B plane */
    uint16_t src[] = {
        100, 200, 300, 400,   /* R */
        500, 600, 700, 800,   /* G */
        900, 1000, 1100, 1200 /* B */
    };
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 3;
    w.pixel_type = TINYFITS_UINT16;
    w.bscale = 1.0; w.bzero = 0.0;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(r.num_channels == 3, "3 channels");

    uint16_t* px = (uint16_t*)pixels;
    CHECK(memcmp(px, src, sizeof(src)) == 0, "pixel-exact match");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_interleaved(void)
{
    printf("Testing roundtrip interleaved write ...\n");
    /* 2x2, 3 channels, interleaved: RGBRGBRGBRGB */
    uint16_t interleaved[] = {
        100, 500, 900,    /* pixel (0,0): R,G,B */
        200, 600, 1000,   /* pixel (1,0) */
        300, 700, 1100,   /* pixel (0,1) */
        400, 800, 1200    /* pixel (1,1) */
    };
    /* Expected planar output after deinterleave */
    uint16_t expected[] = {
        100, 200, 300, 400,   /* R */
        500, 600, 700, 800,   /* G */
        900, 1000, 1100, 1200 /* B */
    };

    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 3;
    w.pixel_type = TINYFITS_UINT16;
    w.bscale = 1.0; w.bzero = 0.0;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, interleaved, &fdata, &fsize, 1);
    CHECK(err == TINYFITS_OK, "save interleaved succeeds");

    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    uint16_t* px = (uint16_t*)pixels;
    CHECK(memcmp(px, expected, sizeof(expected)) == 0, "deinterleaved correctly");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

/* Round-trip a 2x2x3 interleaved RGB buffer for each multi-byte pixel
 * type. Exercises the writer's slow-path (interleaved && ch>1) deinterleave
 * loop -- the only path NOT exercised by the bulk-memcpy fast path.
 */
static void test_interleaved_all_types(void)
{
    printf("Testing interleaved=1 deinterleave for all pixel types ...\n");

#define INTERLEAVED_ROUNDTRIP(TYPE_ENUM, C_TYPE, MSG)                                  \
    do {                                                                      \
        C_TYPE src[12]      = {1,5,9, 2,6,10, 3,7,11, 4,8,12};                \
        C_TYPE expected[12] = {1,2,3,4, 5,6,7,8, 9,10,11,12};                 \
        TinyFitsHeader w = {0};                                                     \
        w.width = 2; w.height = 2; w.num_channels = 3;                        \
        w.pixel_type = TYPE_ENUM;                                             \
        w.bscale = 1.0; w.bzero = 0.0;                                        \
        void* fdata; size_t fsize;                                            \
        int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 1);        \
        CHECK(err == TINYFITS_OK, MSG " interleaved save ok");                \
        TinyFitsHeader r = {0};                                                     \
        void* pixels;                                                         \
        err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);           \
        CHECK(err == TINYFITS_OK, MSG " load ok");                            \
        CHECK(memcmp(pixels, expected, sizeof(expected)) == 0,                \
              MSG " deinterleaved correctly");                                \
        tinyfits_free_buffer(pixels);                                         \
        tinyfits_free_header(&r);                                                    \
        tinyfits_free_buffer(fdata);                                          \
    } while (0)

    INTERLEAVED_ROUNDTRIP(TINYFITS_UINT8,   uint8_t,  "uint8");
    INTERLEAVED_ROUNDTRIP(TINYFITS_INT16,   int16_t,  "int16");
    INTERLEAVED_ROUNDTRIP(TINYFITS_UINT16,  uint16_t, "uint16");
    INTERLEAVED_ROUNDTRIP(TINYFITS_INT32,   int32_t,  "int32");
    INTERLEAVED_ROUNDTRIP(TINYFITS_UINT32,  uint32_t, "uint32");
    INTERLEAVED_ROUNDTRIP(TINYFITS_FLOAT32, float,    "float32");
    INTERLEAVED_ROUNDTRIP(TINYFITS_FLOAT64, double,   "float64");

#undef INTERLEAVED_ROUNDTRIP
}

static void test_roundtrip_headers(void)
{
    printf("Testing roundtrip header preservation ...\n");

    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT8;
    w.bscale = 1.0; w.bzero = 0.0;
    tinyfits_set_keyword(&w, "OBJECT", "M31", "Andromeda");
    tinyfits_set_keyword(&w, "EXPTIME", "180.0", "seconds");
    tinyfits_append_keyword(&w, "HISTORY", "Calibrated", "");
    tinyfits_append_keyword(&w, "HISTORY", "Stacked", "");

    uint8_t src[] = {1, 2, 3, 4};
    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    const TinyFitsKeyword* obj = tinyfits_get_keyword(&r, "OBJECT");
    CHECK(obj != NULL && strcmp(obj->value, "M31") == 0, "OBJECT preserved");

    const TinyFitsKeyword* exp = tinyfits_get_keyword(&r, "EXPTIME");
    CHECK(exp != NULL && strcmp(exp->value, "180.0") == 0, "EXPTIME preserved");

    int hist_count = tinyfits_get_keywords(&r, "HISTORY", NULL, 0);
    CHECK(hist_count == 2, "two HISTORY entries preserved");

    /* Verify order */
    const TinyFitsKeyword* hist[2];
    tinyfits_get_keywords(&r, "HISTORY", hist, 2);
    CHECK(strcmp(hist[0]->value, "Calibrated") == 0, "first HISTORY");
    CHECK(strcmp(hist[1]->value, "Stacked") == 0, "second HISTORY");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
    tinyfits_free_header(&w);
}

static void test_roundtrip_load_modify_save(void)
{
    printf("Testing load-modify-save with HISTORY ...\n");

    /* Create an initial file with one HISTORY entry */
    uint8_t src[] = {10, 20, 30, 40};
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT8;
    w.bscale = 1.0; w.bzero = 0.0;
    tinyfits_set_keyword(&w, "OBJECT", "M31", "");
    tinyfits_append_keyword(&w, "HISTORY", "Original", "");

    void* fdata1; size_t fsize1;
    tinyfits_save_to_memory(&w, src, &fdata1, &fsize1, 0);
    tinyfits_free_header(&w);

    /* Load, add another HISTORY, save again */
    TinyFitsHeader r = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&r, fdata1, fsize1, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    tinyfits_append_keyword(&r, "HISTORY", "Reprocessed", "");

    void* fdata2; size_t fsize2;
    err = tinyfits_save_to_memory(&r, pixels, &fdata2, &fsize2, 0);
    CHECK(err == TINYFITS_OK, "re-save succeeds");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata1);

    /* Load final file and verify */
    TinyFitsHeader r2 = {0};
    void* pixels2;
    err = tinyfits_load_from_memory(&r2, fdata2, fsize2, &pixels2);
    CHECK(err == TINYFITS_OK, "final load succeeds");

    const TinyFitsKeyword* obj = tinyfits_get_keyword(&r2, "OBJECT");
    CHECK(obj != NULL && strcmp(obj->value, "M31") == 0, "OBJECT preserved");

    int hist_count = tinyfits_get_keywords(&r2, "HISTORY", NULL, 0);
    CHECK(hist_count == 2, "two HISTORY entries");

    const TinyFitsKeyword* hist[2];
    tinyfits_get_keywords(&r2, "HISTORY", hist, 2);
    CHECK(strcmp(hist[0]->value, "Original") == 0, "first HISTORY preserved");
    CHECK(strcmp(hist[1]->value, "Reprocessed") == 0, "appended HISTORY present");

    tinyfits_free_buffer(pixels2);
    tinyfits_free_header(&r2);
    tinyfits_free_buffer(fdata2);
}

static void test_mandatory_header_order(void)
{
    printf("Testing mandatory header order in output ...\n");

    uint8_t src[] = {1, 2, 3, 4};
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT16;
    w.bscale = 1.0; w.bzero = 0.0;
    tinyfits_set_keyword(&w, "OBJECT", "Test", "");

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    /* Parse the raw header cards to verify order */
    const char* hdr = (const char*)fdata;
    char keys[5][TINYFITS_CARD_KEY_LEN + 1];
    for (int c = 0; c < 5; c++)
    {
        memcpy(keys[c], hdr + c * TINYFITS_CARD_SIZE, TINYFITS_CARD_KEY_LEN);
        keys[c][TINYFITS_CARD_KEY_LEN] = '\0';
        /* Trim trailing spaces */
        for (int i = TINYFITS_CARD_KEY_LEN - 1; i >= 0; i--)
        {
            if (keys[c][i] == ' ') keys[c][i] = '\0';
            else break;
        }
    }

    CHECK(strcmp(keys[0], "SIMPLE") == 0, "card 0 is SIMPLE");
    CHECK(strcmp(keys[1], "BITPIX") == 0, "card 1 is BITPIX");
    CHECK(strcmp(keys[2], "NAXIS")  == 0, "card 2 is NAXIS");
    CHECK(strcmp(keys[3], "NAXIS1") == 0, "card 3 is NAXIS1");
    CHECK(strcmp(keys[4], "NAXIS2") == 0, "card 4 is NAXIS2");

    /* Verify no duplicate SIMPLE/BITPIX/NAXIS in the rest of the header */
    int simple_count = 0;
    int bitpix_count = 0;
    for (int i = 0; i < (int)(fsize / TINYFITS_CARD_SIZE); i++)
    {
        const char* c = hdr + i * TINYFITS_CARD_SIZE;
        if (memcmp(c, "SIMPLE  ", TINYFITS_CARD_KEY_LEN) == 0) simple_count++;
        if (memcmp(c, "BITPIX  ", TINYFITS_CARD_KEY_LEN) == 0) bitpix_count++;
        if (memcmp(c, "END     ", TINYFITS_CARD_KEY_LEN) == 0) break;
    }
    CHECK(simple_count == 1, "SIMPLE appears exactly once");
    CHECK(bitpix_count == 1, "BITPIX appears exactly once");

    tinyfits_free_header(&w);
    tinyfits_free_buffer(fdata);
}

static void test_naxis_0_and_gt3(void)
{
    printf("Testing NAXIS=0 and NAXIS>3 rejection ...\n");

    /* NAXIS=0 */
    FitsBuf b;
    fitsbuf_init(&b);
    fitsbuf_card(&b, "SIMPLE", "                   T");
    fitsbuf_card_int(&b, "BITPIX", 16);
    fitsbuf_card_int(&b, "NAXIS", 0);
    fitsbuf_end(&b);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_ERR_NO_IMAGE, "NAXIS=0 rejected");
    tinyfits_free_header(&header);
    fitsbuf_free(&b);

    /* NAXIS=4: walker skips past since NAXIS not in {2, 3}. Include the
     * declared data block (10*10*3*2 int16 = 1200 bytes) so the skip is
     * well-defined.
     */
    fitsbuf_init(&b);
    fitsbuf_card(&b, "SIMPLE", "                   T");
    fitsbuf_card_int(&b, "BITPIX", 16);
    fitsbuf_card_int(&b, "NAXIS", 4);
    fitsbuf_card_int(&b, "NAXIS1", 10);
    fitsbuf_card_int(&b, "NAXIS2", 10);
    fitsbuf_card_int(&b, "NAXIS3", 3);
    fitsbuf_card_int(&b, "NAXIS4", 2);
    fitsbuf_end(&b);
    char zeros[1200] = {0};
    fitsbuf_append(&b, zeros, sizeof(zeros));
    fitsbuf_pad_to_block(&b);

    err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_ERR_NO_IMAGE, "NAXIS=4 walked past, no image found");
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_load_struct_reuse(void)
{
    printf("Testing load struct reuse ...\n");

    /* Create two different files */
    uint8_t src1[] = {10, 20, 30, 40};
    TinyFitsHeader w1 = {0};
    w1.width = 2; w1.height = 2; w1.num_channels = 1;
    w1.pixel_type = TINYFITS_UINT8;
    w1.bscale = 1.0; w1.bzero = 0.0;
    tinyfits_set_keyword(&w1, "OBJECT", "First", "");
    void* fdata1; size_t fsize1;
    tinyfits_save_to_memory(&w1, src1, &fdata1, &fsize1, 0);
    tinyfits_free_header(&w1);

    int16_t src2[] = {-1, -2, -3, -4};
    TinyFitsHeader w2 = {0};
    w2.width = 2; w2.height = 2; w2.num_channels = 1;
    w2.pixel_type = TINYFITS_INT16;
    w2.bscale = 1.0; w2.bzero = 0.0;
    tinyfits_set_keyword(&w2, "OBJECT", "Second", "");
    void* fdata2; size_t fsize2;
    tinyfits_save_to_memory(&w2, src2, &fdata2, &fsize2, 0);
    tinyfits_free_header(&w2);

    /* Load first */
    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, fdata1, fsize1, &pixels);
    CHECK(err == TINYFITS_OK, "first load succeeds");
    CHECK(header.pixel_type == TINYFITS_UINT8, "first pixel_type");
    tinyfits_free_buffer(pixels);

    /* Load second into same struct (must free first) */
    tinyfits_free_header(&header);
    err = tinyfits_load_from_memory(&header, fdata2, fsize2, &pixels);
    CHECK(err == TINYFITS_OK, "second load succeeds");
    CHECK(header.pixel_type == TINYFITS_INT16, "second pixel_type");

    const TinyFitsKeyword* obj = tinyfits_get_keyword(&header, "OBJECT");
    CHECK(obj != NULL && strcmp(obj->value, "Second") == 0, "second OBJECT");

    int16_t* px = (int16_t*)pixels;
    CHECK(px[0] == -1 && px[3] == -4, "second pixel values");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    tinyfits_free_buffer(fdata1);
    tinyfits_free_buffer(fdata2);
}

static void test_to_float_all_types(void)
{
    printf("Testing to_float for all pixel types ...\n");

    /* normalized: storage range mapped to [0, 1] for every integer type */

    /* UINT8: [0, 255] -> [0, 1] */
    {
        TinyFitsHeader i = {0};
        i.width = 2; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_UINT8;
        uint8_t src[] = {0, 255};
        float out[2];
        CHECK(tinyfits_to_float_normalized(&i, src, out) == TINYFITS_OK, "uint8 normalized ok");
        CHECK_CLOSE(out[0], 0.0f, 0.01f, "uint8 normalized [0]");
        CHECK_CLOSE(out[1], 1.0f, 0.01f, "uint8 normalized [1]");
    }
    /* INT16: [-32768, 32767] -> [0, 1] (Formula B) */
    {
        TinyFitsHeader i = {0};
        i.width = 2; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_INT16;
        int16_t src[] = {-32768, 32767};
        float out[2];
        CHECK(tinyfits_to_float_normalized(&i, src, out) == TINYFITS_OK, "int16 normalized ok");
        CHECK_CLOSE(out[0], 0.0f, 1e-6f, "int16 normalized [0] (INT16_MIN -> 0)");
        CHECK_CLOSE(out[1], 1.0f, 1e-6f, "int16 normalized [1] (INT16_MAX -> 1)");
    }
    /* UINT16: [0, 65535] -> [0, 1] */
    {
        TinyFitsHeader i = {0};
        i.width = 2; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_UINT16;
        uint16_t src[] = {0, 65535};
        float out[2];
        CHECK(tinyfits_to_float_normalized(&i, src, out) == TINYFITS_OK, "uint16 normalized ok");
        CHECK_CLOSE(out[0], 0.0f, 0.01f, "uint16 normalized [0]");
        CHECK_CLOSE(out[1], 1.0f, 0.01f, "uint16 normalized [1]");
    }
    /* INT32: [INT32_MIN, INT32_MAX] -> [0, 1] via uint32 arithmetic */
    {
        TinyFitsHeader i = {0};
        i.width = 2; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_INT32;
        int32_t src[] = {INT32_MIN, INT32_MAX};
        float out[2];
        CHECK(tinyfits_to_float_normalized(&i, src, out) == TINYFITS_OK, "int32 normalized ok");
        CHECK_CLOSE(out[0], 0.0f, 1e-6f, "int32 normalized [0] (INT32_MIN -> 0)");
        CHECK_CLOSE(out[1], 1.0f, 1e-6f, "int32 normalized [1] (INT32_MAX -> 1)");
    }
    /* UINT32: [0, UINT32_MAX] -> [0, 1] */
    {
        TinyFitsHeader i = {0};
        i.width = 2; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_UINT32;
        uint32_t src[] = {0, UINT32_MAX};
        float out[2];
        CHECK(tinyfits_to_float_normalized(&i, src, out) == TINYFITS_OK, "uint32 normalized ok");
        CHECK_CLOSE(out[0], 0.0f, 1e-6f, "uint32 normalized [0]");
        CHECK_CLOSE(out[1], 1.0f, 1e-6f, "uint32 normalized [1]");
    }

    /* physical: bzero + bscale * stored. Default scaling -> direct cast. */

    /* UINT8 physical default */
    {
        TinyFitsHeader i = {0};
        i.width = 2; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_UINT8;
        i.bscale = 1.0; i.bzero = 0.0;
        uint8_t src[] = {0, 200};
        float out[2];
        CHECK(tinyfits_to_float_physical(&i, src, out) == TINYFITS_OK, "uint8 physical ok");
        CHECK_CLOSE(out[0], 0.0f, 1e-6f, "uint8 physical [0]");
        CHECK_CLOSE(out[1], 200.0f, 1e-6f, "uint8 physical [1]");
    }
    /* INT16 physical with non-default scaling: bscale=2e-16, bzero=0 */
    {
        TinyFitsHeader i = {0};
        i.width = 1; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_INT16;
        i.bscale = 2.0e-16; i.bzero = 0.0;
        int16_t src[] = {1000};
        float out[1];
        CHECK(tinyfits_to_float_physical(&i, src, out) == TINYFITS_OK, "int16 physical scaled ok");
        CHECK_CLOSE(out[0], 2.0e-13f, 1e-19f, "int16 physical scaled [0]");
    }
    /* FLOAT32 physical with non-default scaling */
    {
        TinyFitsHeader i = {0};
        i.width = 1; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_FLOAT32;
        i.bscale = 2.0; i.bzero = 10.0;
        float src[] = {5.0f};
        float out[1];
        CHECK(tinyfits_to_float_physical(&i, src, out) == TINYFITS_OK, "float32 physical scaled ok");
        CHECK_CLOSE(out[0], 20.0f, 1e-6f, "float32 physical scaled [0] (10 + 2*5)");
    }
    /* FLOAT64 physical default = narrowing cast */
    {
        TinyFitsHeader i = {0};
        i.width = 1; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_FLOAT64;
        i.bscale = 1.0; i.bzero = 0.0;
        double src[] = {3.14159265358979};
        float out[1];
        CHECK(tinyfits_to_float_physical(&i, src, out) == TINYFITS_OK, "float64 physical ok");
        CHECK_CLOSE(out[0], 3.14159f, 1e-4f, "float64 physical [0]");
    }
}

/* For each pixel type, exercise both branches of tinyfits_to_float_physical:
 *   - default scaling (bscale=1, bzero=0): the cast / memcpy fast path
 *   - non-identity transform: the bzero + bscale * stored slow path
 * Existing tests cover the matrix only patchily.
 */
static void test_to_float_physical_dual_paths(void)
{
    printf("Testing to_float_physical fast/slow paths for all pixel types ...\n");

#define PHYS_CASE(TYPE_ENUM, C_TYPE, BSCALE, BZERO, SRC_VAL, EXPECTED, TOL, MSG) \
    do {                                                                         \
        TinyFitsHeader i = {0};                                                        \
        i.width = 1; i.height = 1; i.num_channels = 1;                           \
        i.pixel_type = TYPE_ENUM;                                                \
        i.bscale = BSCALE; i.bzero = BZERO;                                      \
        C_TYPE src[1] = {SRC_VAL};                                               \
        float out[1];                                                            \
        CHECK(tinyfits_to_float_physical(&i, src, out) == TINYFITS_OK,           \
              MSG " call ok");                                                   \
        CHECK_CLOSE(out[0], EXPECTED, TOL, MSG " value");                        \
    } while (0)

    /* UINT8: fast (200 -> 200), slow (200 * 2 + 10 = 410) */
    PHYS_CASE(TINYFITS_UINT8,  uint8_t, 1.0,  0.0, 200, 200.0f, 1e-6f,
              "uint8 fast");
    PHYS_CASE(TINYFITS_UINT8,  uint8_t, 2.0, 10.0, 200, 410.0f, 1e-4f,
              "uint8 slow");

    /* INT16: fast (-1234 -> -1234), slow (-1000 * 0.5 + 100 = -400) */
    PHYS_CASE(TINYFITS_INT16,  int16_t, 1.0,  0.0, -1234, -1234.0f, 1e-6f,
              "int16 fast");
    PHYS_CASE(TINYFITS_INT16,  int16_t, 0.5, 100.0, -1000, -400.0f, 1e-4f,
              "int16 slow");

    /* UINT16: fast (60000 -> 60000), slow (50000 * 0.001 + 5 = 55) */
    PHYS_CASE(TINYFITS_UINT16, uint16_t, 1.0,    0.0, 60000, 60000.0f, 1e-2f,
              "uint16 fast");
    PHYS_CASE(TINYFITS_UINT16, uint16_t, 0.001,  5.0, 50000, 55.0f, 1e-4f,
              "uint16 slow");

    /* INT32: fast, slow */
    PHYS_CASE(TINYFITS_INT32,  int32_t, 1.0,  0.0, -100000, -100000.0f, 1e-1f,
              "int32 fast");
    PHYS_CASE(TINYFITS_INT32,  int32_t, 0.01, -1.0, 100000, 999.0f, 1e-3f,
              "int32 slow");

    /* UINT32: fast, slow */
    PHYS_CASE(TINYFITS_UINT32, uint32_t, 1.0,  0.0, 1000000, 1000000.0f, 1.0f,
              "uint32 fast");
    PHYS_CASE(TINYFITS_UINT32, uint32_t, 1e-6, 0.5, 1000000, 1.5f, 1e-4f,
              "uint32 slow");

    /* FLOAT32: fast (memcpy), slow (10 + 2 * 5 = 20) */
    PHYS_CASE(TINYFITS_FLOAT32, float, 1.0,  0.0, 3.14f, 3.14f, 1e-6f,
              "float32 fast");
    PHYS_CASE(TINYFITS_FLOAT32, float, 2.0, 10.0, 5.0f, 20.0f, 1e-6f,
              "float32 slow");

    /* FLOAT64: fast (always cast), slow */
    PHYS_CASE(TINYFITS_FLOAT64, double, 1.0,  0.0, 2.71828, 2.71828f, 1e-4f,
              "float64 fast");
    PHYS_CASE(TINYFITS_FLOAT64, double, 0.5, -1.0, 4.0, 1.0f, 1e-6f,
              "float64 slow");

#undef PHYS_CASE
}

static void test_save_to_file(void)
{
    printf("Testing save to file ...\n");

    uint16_t src[] = {100, 200, 300, 400};
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT16;
    w.bscale = 1.0; w.bzero = 0.0;
    tinyfits_set_keyword(&w, "OBJECT", "FileTest", "");

    int err = tinyfits_save(&w, src, "test_save_output.fits", 0);
    CHECK(err == TINYFITS_OK, "save to file succeeds");

    /* Load it back */
    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load(&r, "test_save_output.fits", &pixels);
    CHECK(err == TINYFITS_OK, "load from file succeeds");
    CHECK(r.pixel_type == TINYFITS_UINT16, "pixel_type");
    CHECK(r.width == 2 && r.height == 2, "dimensions");

    uint16_t* px = (uint16_t*)pixels;
    CHECK(px[0] == 100 && px[1] == 200 && px[2] == 300 && px[3] == 400,
          "pixel values match");

    const TinyFitsKeyword* obj = tinyfits_get_keyword(&r, "OBJECT");
    CHECK(obj != NULL && strcmp(obj->value, "FileTest") == 0, "header preserved");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_header(&w);
    remove("test_save_output.fits");
}

static void test_to_float_normalized_rejects_floats(void)
{
    printf("Testing to_float_normalized rejects float pixel types ...\n");

    /* The function explicitly rejects float pixel types: there's no
     * natural type max to normalize against.
     */
    {
        TinyFitsHeader i = {0};
        i.width = 1; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_FLOAT32;
        float src[1] = {0.0f};
        float out[1];
        CHECK(tinyfits_to_float_normalized(&i, src, out) == TINYFITS_ERR_BAD_PIXEL_TYPE,
              "FLOAT32 normalized rejected");
    }
    {
        TinyFitsHeader i = {0};
        i.width = 1; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_FLOAT64;
        double src[1] = {0.0};
        float out[1];
        CHECK(tinyfits_to_float_normalized(&i, src, out) == TINYFITS_ERR_BAD_PIXEL_TYPE,
              "FLOAT64 normalized rejected");
    }
}

static void test_file_io_errors(void)
{
    printf("Testing file I/O error paths (missing file, etc.) ...\n");

    const char* missing = "this_file_does_not_exist_xyz.fits";

    /* tinyfits_load on a missing file -> ERR_OPEN */
    {
        TinyFitsHeader header = {0};
        void* pixels = NULL;
        int err = tinyfits_load(&header, missing, &pixels);
        CHECK(err == TINYFITS_ERR_OPEN, "load(missing) -> ERR_OPEN");
        CHECK(pixels == NULL, "pixels NULL on load failure");
        tinyfits_free_header(&header);
    }

    /* tinyfits_load_header on a missing file -> ERR_OPEN */
    {
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header(&header, missing);
        CHECK(err == TINYFITS_ERR_OPEN, "header(missing) -> ERR_OPEN");
        tinyfits_free_header(&header);
    }

    /* Save to a path under a non-existent directory: fopen("wb") fails
     * with ENOENT on both POSIX and Windows.
     */
    {
        uint16_t src[] = {1, 2, 3, 4};
        TinyFitsHeader w = {0};
        w.width = 2; w.height = 2; w.num_channels = 1;
        w.pixel_type = TINYFITS_UINT16;
        w.bscale = 1.0; w.bzero = 0.0;
        int err = tinyfits_save(&w, src,
                                "no_such_dir_xyz/out.fits", 0);
        CHECK(err == TINYFITS_ERR_OPEN, "save(bad path) -> ERR_OPEN");
        tinyfits_free_header(&w);
    }
}

static void test_roundtrip_mono_naxis2(void)
{
    printf("Testing monochrome roundtrip writes NAXIS=2 ...\n");

    float src[] = {1.0f, 2.0f, 3.0f, 4.0f};
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_FLOAT32;
    w.bscale = 1.0; w.bzero = 0.0;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    /* Verify NAXIS=2 in the raw header (no NAXIS3) */
    const char* hdr = (const char*)fdata;
    int found_naxis3 = 0;
    for (int i = 0; i < (int)(fsize / TINYFITS_CARD_SIZE); i++)
    {
        const char* c = hdr + i * TINYFITS_CARD_SIZE;
        if (memcmp(c, "NAXIS3  ", TINYFITS_CARD_KEY_LEN) == 0) found_naxis3 = 1;
        if (memcmp(c, "END     ", TINYFITS_CARD_KEY_LEN) == 0) break;
    }
    CHECK(!found_naxis3, "no NAXIS3 card for mono image");

    /* Verify round-trip */
    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(r.num_channels == 1, "1 channel");

    float* px = (float*)pixels;
    CHECK_CLOSE(px[0], 1.0f, 1e-6f, "px[0]");
    CHECK_CLOSE(px[3], 4.0f, 1e-6f, "px[3]");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

static void test_max_header_blocks(void)
{
    printf("Testing max header blocks ...\n");

    /* Build a FITS file with one more header block than allowed. */
    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 4, 4);
    /* Pad rest of block 0 with blanks */
    while (b.size < TINYFITS_BLOCK_SIZE)
    {
        char blank[TINYFITS_CARD_SIZE];
        memset(blank, ' ', TINYFITS_CARD_SIZE);
        fitsbuf_append(&b, blank, TINYFITS_CARD_SIZE);
    }
    /* Blocks 1..MAX-1: all blank cards */
    for (int i = 1; i < TINYFITS_MAX_HEADER_BLOCKS; i++)
    {
        char block[TINYFITS_BLOCK_SIZE];
        memset(block, ' ', TINYFITS_BLOCK_SIZE);
        fitsbuf_append(&b, block, TINYFITS_BLOCK_SIZE);
    }
    /* Block MAX (one over the limit): END card */
    fitsbuf_end(&b);
    uint8_t pixels[16] = {0};
    fitsbuf_append(&b, pixels, 16);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_ERR_HEADER_TOO_LARGE, "1025 blocks rejected on load");

    err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_ERR_HEADER_TOO_LARGE, "1025 blocks rejected on header");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_save_errors(void)
{
    printf("Testing save error cases ...\n");

    TinyFitsHeader w = {0};
    uint8_t dummy = 0;
    void* fdata; size_t fsize;

    /* Zero dimensions */
    w.pixel_type = TINYFITS_UINT8;
    w.bscale = 1.0; w.bzero = 0.0;
    int err = tinyfits_save_to_memory(&w, &dummy, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_BAD_DIMENSION, "zero dimensions rejected");

    /* Unknown pixel_type */
    w.width = 1; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_UNKNOWN;
    err = tinyfits_save_to_memory(&w, &dummy, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_BAD_PIXEL_TYPE, "unknown pixel_type rejected");

    /* NULL pixels */
    w.pixel_type = TINYFITS_UINT8;
    w.bscale = 1.0; w.bzero = 0.0;
    err = tinyfits_save_to_memory(&w, NULL, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_NULL_ARG, "NULL pixels rejected");

    /* NULL out_data */
    err = tinyfits_save_to_memory(&w, &dummy, NULL, &fsize, 0);
    CHECK(err == TINYFITS_ERR_NULL_ARG, "NULL out_data rejected");
}

static void test_malicious_inputs(void)
{
    printf("Testing malicious/corrupted inputs ...\n");

    /* Empty buffer */
    {
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, "", 0);
        CHECK(err == TINYFITS_ERR_NOT_FITS, "empty buffer rejected");
    }

    /* Buffer smaller than one block */
    {
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, "SIMPLE  =", 9);
        CHECK(err == TINYFITS_ERR_NOT_FITS, "tiny buffer rejected");
    }

    /* Valid header but huge dimensions (overflow attempt) */
    {
        FitsBuf b;
        fitsbuf_init(&b);
        fitsbuf_card(&b, "SIMPLE", "                   T");
        fitsbuf_card_int(&b, "BITPIX", 16);
        fitsbuf_card_int(&b, "NAXIS", 2);
        /* 65536 * 65536 * 2 = 8GB, overflows 32-bit size_t */
        fitsbuf_card(&b, "NAXIS1", "               65536");
        fitsbuf_card(&b, "NAXIS2", "               65536");
        fitsbuf_end(&b);

        TinyFitsHeader header = {0};
        void* pixels;
        int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
        /* Should fail: either overflow detected or not enough data */
        CHECK(err != TINYFITS_OK, "huge dimensions rejected");
        CHECK(pixels == NULL, "pixels NULL on failure");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }

    /* Negative dimensions */
    {
        FitsBuf b;
        fitsbuf_init(&b);
        fitsbuf_card(&b, "SIMPLE", "                   T");
        fitsbuf_card_int(&b, "BITPIX", 8);
        fitsbuf_card_int(&b, "NAXIS", 2);
        fitsbuf_card(&b, "NAXIS1", "                  -1");
        fitsbuf_card(&b, "NAXIS2", "                  10");
        fitsbuf_end(&b);

        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
        CHECK(err == TINYFITS_ERR_BAD_DIMENSION, "negative NAXIS1 rejected");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }

    /* Non-numeric BITPIX */
    {
        FitsBuf b;
        fitsbuf_init(&b);
        fitsbuf_card(&b, "SIMPLE", "                   T");
        fitsbuf_card(&b, "BITPIX", "             garbage");
        fitsbuf_card_int(&b, "NAXIS", 2);
        fitsbuf_card_int(&b, "NAXIS1", 4);
        fitsbuf_card_int(&b, "NAXIS2", 4);
        fitsbuf_end(&b);

        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
        /* atoi("garbage") = 0, which is not a valid BITPIX */
        CHECK(err != TINYFITS_OK, "garbage BITPIX rejected");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }

    /* Valid header, pixel data truncated to 1 byte */
    {
        FitsBuf b;
        fitsbuf_init(&b);
        fitsbuf_card(&b, "SIMPLE", "                   T");
        fitsbuf_card_int(&b, "BITPIX", -32);
        fitsbuf_card_int(&b, "NAXIS", 2);
        fitsbuf_card_int(&b, "NAXIS1", 10);
        fitsbuf_card_int(&b, "NAXIS2", 10);
        fitsbuf_end(&b);
        /* Only 1 byte of data instead of 10*10*4=400 */
        uint8_t one = 0;
        fitsbuf_append(&b, &one, 1);

        TinyFitsHeader header = {0};
        void* pixels;
        int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
        CHECK(err == TINYFITS_ERR_TRUNCATED, "truncated pixel data rejected");
        CHECK(pixels == NULL, "pixels NULL on truncated data");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }

    /* All zeros (looks like FITS magic might partially match) */
    {
        uint8_t zeros[TINYFITS_BLOCK_SIZE];
        memset(zeros, 0, sizeof(zeros));
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, zeros, sizeof(zeros));
        CHECK(err == TINYFITS_ERR_NOT_FITS, "all-zeros rejected");
    }

    /* Valid SIMPLE but no END within the block. Each card occupies bytes
     * 0..VALUE_OFFSET+FIXED_VALUE_LEN-1 (= 30); the rest of the card
     * stays as space-fill.
     */
    {
        char block[TINYFITS_BLOCK_SIZE];
        memset(block, ' ', TINYFITS_BLOCK_SIZE);
        const char* cards[] = {
            "SIMPLE  =                    T",
            "BITPIX  =                   16",
            "NAXIS   =                    2",
            "NAXIS1  =                    4",
            "NAXIS2  =                    4",
        };
        const size_t card_prefix_len = TINYFITS_CARD_VALUE_OFFSET
                                     + TINYFITS_CARD_FIXED_VALUE_LEN;
        for (size_t i = 0; i < sizeof(cards) / sizeof(cards[0]); i++)
            memcpy(block + i * TINYFITS_CARD_SIZE, cards[i], card_prefix_len);
        /* No END card, and buffer is exactly one block */

        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, block, TINYFITS_BLOCK_SIZE);
        /* Should fail: no END card, and no second block to read */
        CHECK(err != TINYFITS_OK, "missing END card rejected");
        tinyfits_free_header(&header);
    }
}

static void test_null_params(void)
{
    printf("Testing NULL parameter handling ...\n");

    TinyFitsHeader header = {0};
    void* pixels;

    /* The size arg is irrelevant when data is NULL; the NULL check fires first. */
    CHECK(tinyfits_load_from_memory(NULL, "x", 1, &pixels) == TINYFITS_ERR_NULL_ARG,
          "NULL header to load rejected");
    CHECK(tinyfits_load_from_memory(&header, NULL, 1, &pixels) == TINYFITS_ERR_NULL_ARG,
          "NULL data to load rejected");
    CHECK(tinyfits_load_header_from_memory(NULL, "x", 1) == TINYFITS_ERR_NULL_ARG,
          "NULL header to header rejected");
    CHECK(tinyfits_load_header_from_memory(&header, NULL, 1) == TINYFITS_ERR_NULL_ARG,
          "NULL data to header rejected");
}

static void test_single_quote_roundtrip(void)
{
    printf("Testing single-quote in header value round-trip ...\n");

    TinyFitsHeader w = {0};
    w.width = 2; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT8;
    w.bscale = 1.0; w.bzero = 0.0;
    tinyfits_set_keyword(&w, "OBSERVER", "O'Brien", "");

    uint8_t src[] = {1, 2};
    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    const TinyFitsKeyword* obs = tinyfits_get_keyword(&r, "OBSERVER");
    CHECK(obs != NULL && strcmp(obs->value, "O'Brien") == 0, "single quote preserved");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
    tinyfits_free_header(&w);
}

static void test_zero_width(void)
{
    printf("Testing NAXIS1=0 (Random Groups sentinel) ...\n");

    /* NAXIS1=0 with NAXIS>=2 is the Random Groups sentinel (per FITS spec):
     * the primary HDU is not an image, but a per-group data structure that
     * tinyfits cannot decode. Walker skips past it; with no further HDUs
     * present the result is NO_IMAGE (or TRUNCATED if the skip runs off
     * the end of the buffer because no data block was provided).
     */
    FitsBuf b;
    fitsbuf_init(&b);
    fitsbuf_card(&b, "SIMPLE", "                   T");
    fitsbuf_card_int(&b, "BITPIX", 8);
    fitsbuf_card_int(&b, "NAXIS", 2);
    fitsbuf_card_int(&b, "NAXIS1", 0);
    fitsbuf_card_int(&b, "NAXIS2", 10);
    fitsbuf_end(&b);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_ERR_TRUNCATED || err == TINYFITS_ERR_NO_IMAGE,
          "NAXIS1=0 treated as Random Groups, walker advances past primary");
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_interleaved_single_channel(void)
{
    printf("Testing interleaved=1 with single channel ...\n");

    uint16_t src[] = {100, 200, 300, 400};
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT16;
    w.bscale = 1.0; w.bzero = 0.0;

    void* fdata_il; size_t fsize_il;
    int err = tinyfits_save_to_memory(&w, src, &fdata_il, &fsize_il, 1);
    CHECK(err == TINYFITS_OK, "save interleaved succeeds");

    void* fdata_pl; size_t fsize_pl;
    err = tinyfits_save_to_memory(&w, src, &fdata_pl, &fsize_pl, 0);
    CHECK(err == TINYFITS_OK, "save planar succeeds");

    /* Load both and compare pixels */
    TinyFitsHeader r1 = {0}, r2 = {0};
    void *px1, *px2;
    tinyfits_load_from_memory(&r1, fdata_il, fsize_il, &px1);
    tinyfits_load_from_memory(&r2, fdata_pl, fsize_pl, &px2);

    CHECK(memcmp(px1, px2, 4 * sizeof(uint16_t)) == 0,
          "interleaved=1 and interleaved=0 produce same pixels for 1 channel");

    tinyfits_free_buffer(px1);
    tinyfits_free_buffer(px2);
    tinyfits_free_header(&r1);
    tinyfits_free_header(&r2);
    tinyfits_free_buffer(fdata_il);
    tinyfits_free_buffer(fdata_pl);
}

static void test_failed_load_zeroes_struct(void)
{
    printf("Testing failed load leaves struct zeroed ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 100, 100);
    fitsbuf_end(&b);
    /* Truncated -- not enough pixel data */

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err != TINYFITS_OK, "load fails on truncated data");
    CHECK(pixels == NULL, "pixels is NULL");
    CHECK(header.keywords == NULL, "keywords is NULL after failed load");
    CHECK(header.width == 0, "width is 0 after failed load");
    CHECK(header.pixel_type == TINYFITS_UNKNOWN, "pixel_type is UNKNOWN after failed load");

    /* Safe to call free on the zeroed struct */
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_null_in_value(void)
{
    printf("Testing null byte in header value ...\n");

    TinyFitsHeader header = {0};
    /* "M31\0injected" -- strlen sees 3, strncpy copies "M31" */
    int err = tinyfits_set_keyword(&header, "OBJECT", "M31\0injected", "");
    CHECK(err == TINYFITS_OK, "set_header succeeds");

    const TinyFitsKeyword* val = tinyfits_get_keyword(&header, "OBJECT");
    CHECK(val != NULL && strcmp(val->value, "M31") == 0, "value truncated at NUL");
    CHECK(strlen(val->value) == 3, "value length is 3");

    tinyfits_free_header(&header);
}

static void test_continue_basic_merge(void)
{
    printf("Testing CONTINUE card basic merge on read ...\n");

    /* Build a FITS file with a CONTINUE card. The merged value fits in
     * one card, so this test exercises the read-side merge.
     */
    FitsBuf b;
    fitsbuf_init(&b);
    fitsbuf_card(&b, "SIMPLE", "                   T");
    fitsbuf_card_int(&b, "BITPIX", 8);
    fitsbuf_card_int(&b, "NAXIS", 2);
    fitsbuf_card_int(&b, "NAXIS1", 2);
    fitsbuf_card_int(&b, "NAXIS2", 2);

    {
        char card[TINYFITS_CARD_SIZE];
        memset(card, ' ', TINYFITS_CARD_SIZE);
        memcpy(card, "LONGSTR = 'This is the first part of a long string&'", 52);
        fitsbuf_append(&b, card, TINYFITS_CARD_SIZE);
    }
    {
        char card[TINYFITS_CARD_SIZE];
        memset(card, ' ', TINYFITS_CARD_SIZE);
        memcpy(card, "CONTINUE  ' and 2nd part.'", 26);
        fitsbuf_append(&b, card, TINYFITS_CARD_SIZE);
    }

    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_OK, "load succeeds");

    const TinyFitsKeyword* longstr = tinyfits_get_keyword(&header, "LONGSTR");
    CHECK(longstr != NULL, "LONGSTR present");
    /* '&' stripped, fragments concatenated. */
    CHECK(longstr && strcmp(longstr->value,
                            "This is the first part of a long string"
                            " and 2nd part.") == 0,
          "LONGSTR fragments merged");

    /* CONTINUE must NOT appear as its own keyword entry. */
    CHECK(tinyfits_get_keywords(&header, "CONTINUE", NULL, 0) == 0,
          "CONTINUE not stored as separate keyword");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_continue_long_chain(void)
{
    printf("Testing CONTINUE long chain (4 cards, 250 chars) ...\n");

    /* Build expected reassembled value: 250 chars (alternating Xn pattern)
     * so we can sanity-check fragments without depending on a specific
     * chunker boundary.
     */
    char expected[256];
    for (int i = 0; i < 250; i++)
        expected[i] = (char)('A' + (i % 26));
    expected[250] = '\0';

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);

    /* Chunk sizes are arbitrary choices within the per-card budget
     * (head + intermediate continuing cards: 67 bytes max; final card
     * without a comment: 68 bytes max). They must sum to the length of
     * `expected` (250). Positions within each card derive from the
     * chunk size and the fixed envelope.
     */
    const int head_chunk  = 60;
    const int cont1_chunk = 60;
    const int cont2_chunk = 62;
    const int cont3_chunk = 68;
    int src_pos = 0;

    /* Head card: 'LONGSTR = '<chunk>&' with continuation marker. */
    {
        char card[TINYFITS_CARD_SIZE];
        memset(card, ' ', TINYFITS_CARD_SIZE);
        memcpy(card, "LONGSTR", 7);
        card[TINYFITS_CARD_KEY_LEN]      = '=';
        card[TINYFITS_CARD_KEY_LEN + 1]  = ' ';
        card[TINYFITS_CARD_VALUE_OFFSET] = '\'';
        int p = TINYFITS_CARD_VALUE_OFFSET + 1;
        memcpy(card + p, expected + src_pos, head_chunk);
        p += head_chunk;
        card[p++] = '&';
        card[p++] = '\'';
        fitsbuf_append(&b, card, TINYFITS_CARD_SIZE);
        src_pos += head_chunk;
    }
    {
        char payload[80];
        int p = 0;
        payload[p++] = '\'';
        memcpy(payload + p, expected + src_pos, cont1_chunk);
        p += cont1_chunk;
        payload[p++] = '&';
        payload[p++] = '\'';
        fitsbuf_continue_n(&b, payload, p);
        src_pos += cont1_chunk;
    }
    {
        char payload[80];
        int p = 0;
        payload[p++] = '\'';
        memcpy(payload + p, expected + src_pos, cont2_chunk);
        p += cont2_chunk;
        payload[p++] = '&';
        payload[p++] = '\'';
        fitsbuf_continue_n(&b, payload, p);
        src_pos += cont2_chunk;
    }
    /* Final card: no '&' continuation marker. */
    {
        char payload[80];
        int p = 0;
        payload[p++] = '\'';
        memcpy(payload + p, expected + src_pos, cont3_chunk);
        p += cont3_chunk;
        payload[p++] = '\'';
        fitsbuf_continue_n(&b, payload, p);
        src_pos += cont3_chunk;
    }

    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_OK, "load succeeds");

    const TinyFitsKeyword* longstr = tinyfits_get_keyword(&header, "LONGSTR");
    CHECK(longstr != NULL, "LONGSTR present");
    CHECK(longstr && strlen(longstr->value) == 250, "LONGSTR length 250");
    CHECK(longstr && strcmp(longstr->value, expected) == 0, "LONGSTR content matches");
    CHECK(tinyfits_get_keywords(&header, "CONTINUE", NULL, 0) == 0,
          "no orphan CONTINUE entries");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

/* Test that a chain with "''" literal-quote pair positioned cleanly within
 * a chunk reads correctly. The writer is responsible for keeping pairs
 * atomic; we verify the read side decodes them per-card before merging.
 */
static void test_continue_quotepair_at_boundary(void)
{
    printf("Testing CONTINUE chain with quote-pair literal quotes ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);

    /* Head: 'a&'  -> value "a", trailing & */
    {
        char card[TINYFITS_CARD_SIZE];
        memset(card, ' ', TINYFITS_CARD_SIZE);
        memcpy(card, "LONGSTR = 'a&'", 14);
        fitsbuf_append(&b, card, TINYFITS_CARD_SIZE);
    }
    /* CONT1: '''&'  -> on-card open-quote, '' (escape pair), &, close-quote.
     * Value "'", trailing &.
     */
    fitsbuf_continue(&b, "'''&'");
    /* CONT2: 'b'  -> value "b" */
    fitsbuf_continue(&b, "'b'");

    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_OK, "load succeeds");

    const TinyFitsKeyword* longstr = tinyfits_get_keyword(&header, "LONGSTR");
    CHECK(longstr && strcmp(longstr->value, "a'b") == 0,
          "literal apostrophe in CONTINUE chunk preserved");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_continue_comment_last_wins(void)
{
    printf("Testing CONTINUE comment last-wins ...\n");

    /* Case 1: head has comment, final does not -> head's comment wins
     * (final's empty comment doesn't overwrite).
     */
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, 8, 2, 2);
        {
            char card[TINYFITS_CARD_SIZE];
            memset(card, ' ', TINYFITS_CARD_SIZE);
            memcpy(card, "LONGSTR = 'foo&' / head", 23);
            fitsbuf_append(&b, card, TINYFITS_CARD_SIZE);
        }
        fitsbuf_continue(&b, "'bar'");
        fitsbuf_end(&b);
        uint8_t pixels[] = {1, 2, 3, 4};
        fitsbuf_append(&b, pixels, 4);
        fitsbuf_pad_to_block(&b);

        TinyFitsHeader header = {0};
        void* px;
        int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
        CHECK(err == TINYFITS_OK, "load case-1 succeeds");
        const TinyFitsKeyword* v = tinyfits_get_keyword(&header, "LONGSTR");
        CHECK(v && strcmp(v->value, "foobar") == 0, "case-1 value merged");
        /* The comment-on-head retention is harder to assert via the
         * public API (no get_comment); use the private struct since
         * tests own the implementation.
         */
        CHECK(strcmp(header.keywords[5].comment, "head") == 0,
              "case-1 head comment retained");
        tinyfits_free_buffer(px);
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }

    /* Case 2: head has no comment, final has -> final's comment adopted. */
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, 8, 2, 2);
        {
            char card[TINYFITS_CARD_SIZE];
            memset(card, ' ', TINYFITS_CARD_SIZE);
            memcpy(card, "LONGSTR = 'foo&'", 16);
            fitsbuf_append(&b, card, TINYFITS_CARD_SIZE);
        }
        fitsbuf_continue(&b, "'bar' / final");
        fitsbuf_end(&b);
        uint8_t pixels[] = {1, 2, 3, 4};
        fitsbuf_append(&b, pixels, 4);
        fitsbuf_pad_to_block(&b);

        TinyFitsHeader header = {0};
        void* px;
        int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
        CHECK(err == TINYFITS_OK, "load case-2 succeeds");
        CHECK(strcmp(header.keywords[5].comment, "final") == 0,
              "case-2 final comment adopted");
        tinyfits_free_buffer(px);
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }

    /* Case 3: both have comments -> final wins. */
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, 8, 2, 2);
        {
            char card[TINYFITS_CARD_SIZE];
            memset(card, ' ', TINYFITS_CARD_SIZE);
            memcpy(card, "LONGSTR = 'foo&' / head", 23);
            fitsbuf_append(&b, card, TINYFITS_CARD_SIZE);
        }
        fitsbuf_continue(&b, "'bar' / final");
        fitsbuf_end(&b);
        uint8_t pixels[] = {1, 2, 3, 4};
        fitsbuf_append(&b, pixels, 4);
        fitsbuf_pad_to_block(&b);

        TinyFitsHeader header = {0};
        void* px;
        int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
        CHECK(err == TINYFITS_OK, "load case-3 succeeds");
        CHECK(strcmp(header.keywords[5].comment, "final") == 0,
              "case-3 final wins over head");
        tinyfits_free_buffer(px);
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }
}

/* Test that orphan CONTINUE (previous value has no trailing '&') is
 * dropped silently.
 */
static void test_continue_orphan_dropped(void)
{
    printf("Testing orphan CONTINUE dropped silently ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);
    fitsbuf_card_str(&b, "OBJECT", "M31");          /* No trailing & */
    fitsbuf_continue(&b, "'orphan'");                /* orphan */
    fitsbuf_card_str(&b, "FILTER", "Ha");
    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_OK, "load succeeds");

    const TinyFitsKeyword* obj = tinyfits_get_keyword(&header, "OBJECT");
    CHECK(obj && strcmp(obj->value, "M31") == 0, "OBJECT unchanged");
    CHECK(tinyfits_get_keyword(&header, "FILTER") != NULL, "FILTER still present");
    CHECK(tinyfits_get_keywords(&header, "CONTINUE", NULL, 0) == 0,
          "orphan CONTINUE not stored");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

/* Test chain being truncated at HDU end.
 * The trailing '&' should be dropped from the assembled value.
 */
static void test_continue_truncated_at_hdu_end(void)
{
    printf("Testing trailing & preserved when no CONTINUE follows ...\n");

    /* A trailing '&' is the CONTINUE chain marker only when a CONTINUE
     * card actually follows. Without one, the byte is part of the value
     * (matches astropy behavior; required for round-tripping any value
     * that legitimately ends with '&').
     */
    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);
    {
        char card[TINYFITS_CARD_SIZE];
        memset(card, ' ', TINYFITS_CARD_SIZE);
        memcpy(card, "LONGSTR = 'truncated&'", 22);
        fitsbuf_append(&b, card, TINYFITS_CARD_SIZE);
    }
    /* No CONTINUE follower; END comes next. */
    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_OK, "load succeeds");

    const TinyFitsKeyword* v = tinyfits_get_keyword(&header, "LONGSTR");
    CHECK(v && strcmp(v->value, "truncated&") == 0,
          "trailing & preserved (no following CONTINUE)");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_trailing_amp_roundtrip(void)
{
    printf("Testing round-trip of values ending in '&' ...\n");

    /* Three cases that exercise different code paths:
     *  - short value, fits in a single card (no chain emitted on save)
     *  - long value that requires a CONTINUE chain, ending in '&'
     *  - value where '&' lands at exactly a chain split boundary
     */
    const char* short_amp = "Bayer&";
    /* 119-byte value (forces a CONTINUE chain) ending in '&'. */
    char long_amp[120];
    for (int i = 0; i < 118; i++) long_amp[i] = 'A';
    long_amp[118] = '&';
    long_amp[119] = '\0';

    uint8_t src[] = {1, 2, 3, 4};
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT8;
    w.bscale = 1.0; w.bzero = 0.0;
    CHECK(tinyfits_set_keyword(&w, "SHORT", short_amp, "") == TINYFITS_OK,
          "set SHORT");
    CHECK(tinyfits_set_keyword(&w, "LONG", long_amp, "") == TINYFITS_OK,
          "set LONG");

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader r = {0};
    void* px;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &px);
    CHECK(err == TINYFITS_OK, "reload succeeds");

    const TinyFitsKeyword* s = tinyfits_get_keyword(&r, "SHORT");
    CHECK(s && strcmp(s->value, short_amp) == 0,
          "short value ending in '&' round-trips");

    const TinyFitsKeyword* l = tinyfits_get_keyword(&r, "LONG");
    CHECK(l && strcmp(l->value, long_amp) == 0,
          "long chained value ending in '&' round-trips");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
    tinyfits_free_header(&w);
}

/* Round-trip a battery of string values that interact with the FITS card
 * format itself: quote-escape, chain markers, leading/trailing space
 * preservation, exact-card-boundary lengths, etc. Each case is set via
 * set_keyword, saved to memory, reloaded, and compared byte-for-byte
 * against the original.
 */
static void test_string_value_roundtrip_battery(void)
{
    printf("Testing round-trip battery of string values ...\n");

    /* Each entry must round-trip exactly. Names are short and unique so
     * they fit the standard 8-char keyword namespace.
     */
    /* Note: leading and trailing spaces in FITS string values are NOT
     * significant per the FITS spec (readers may discard them). So we
     * don't test " " or "abc   " as those are format-limited, not bugs.
     */
    struct { const char* key; const char* val; const char* desc; } cases[] = {
        { "EMPTY",    "",                          "empty string" },
        { "MIDSPACE", "abc def ghi",               "internal spaces" },
        { "QUOTE1",   "it's",                      "single apostrophe" },
        { "QUOTE2",   "''",                        "two apostrophes only" },
        { "QUOTE3",   "a'b'c'd",                   "multiple apostrophes" },
        { "QUOTEEND", "ends with'",                "trailing apostrophe" },
        { "AMPMID",   "a&b&c",                     "ampersands in middle" },
        { "AMPEND",   "ends&",                     "single trailing &" },
        { "DBLAMP",   "ends&&",                    "trailing && (literal)" },
        /* Digit-leading values that look numeric but are too long for the
         * 20-char fixed-format numeric field; classifier must route them
         * to the string writer to preserve all bytes.
         */
        { "DIGITS",   "0123456789012345",          "digit-leading short" },
        { "DIGITLG",  "01234567890123456789012345678901", "digit-leading > 20 chars" },
        /* Length boundaries. Standard 8-char-key card has 68 chars between
         * the value-quotes (positions 11-78). Pick lengths that bracket
         * the single-card limit and force chain emission.
         */
        { "LEN1",     "x",                         "len 1" },
        { "LEN67",    "0123456789012345678901234567890123456789012345678901234567890123456", "len 67" },
        { "LEN68",    "01234567890123456789012345678901234567890123456789012345678901234567", "len 68 (single-card boundary)" },
        { "LEN69",    "012345678901234567890123456789012345678901234567890123456789012345678", "len 69 (forces CONTINUE)" },
        { "LEN200",   /* 200 'A's; forces multi-card chain */
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
            "200 chars (multi-card chain)" },
        { "QUOTECH",  /* Quote near a chain boundary forces atomic-pair handling */
            "0123456789012345678901234567890123456789012345678901234567890123456'89",
            "apostrophe near chain split" },
    };

    /* Build a header carrying all the test values and save it. */
    uint8_t src[] = {1, 2, 3, 4};
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT8;
    w.bscale = 1.0; w.bzero = 0.0;

    int n_cases = (int)(sizeof(cases) / sizeof(cases[0]));
    for (int i = 0; i < n_cases; i++)
    {
        int err = tinyfits_set_keyword(&w, cases[i].key, cases[i].val, "");
        CHECK(err == TINYFITS_OK, cases[i].desc);
    }

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save battery");

    TinyFitsHeader r = {0};
    void* px;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &px);
    CHECK(err == TINYFITS_OK, "reload battery");

    /* Compare each value byte-for-byte against the original. CHECK aborts
     * the function on first failure, so on regression we'll see the first
     * mismatched case (rerun with the failing case removed to see the
     * next one).
     */
    for (int i = 0; i < n_cases; i++)
    {
        const TinyFitsKeyword* kw = tinyfits_get_keyword(&r, cases[i].key);
        CHECK(kw != NULL, cases[i].desc);
        CHECK(strcmp(kw->value, cases[i].val) == 0, cases[i].desc);
    }

    tinyfits_free_buffer(px);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
    tinyfits_free_header(&w);
}

static void test_hierarch_basic_read(void)
{
    printf("Testing HIERARCH basic read ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);
    fitsbuf_raw_card(&b,
        "HIERARCH ESO INS LAMP1 ID = 'HALOGEN' / Lamp identifier");
    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_OK, "load succeeds");

    const TinyFitsKeyword* v = tinyfits_get_keyword(&header, "ESO INS LAMP1 ID");
    CHECK(v && strcmp(v->value, "HALOGEN") == 0, "HIERARCH long-key value retrieved");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_hierarch_whitespace_canonicalization(void)
{
    printf("Testing HIERARCH whitespace canonicalization ...\n");

    /* Source has multiple spaces between "HIERARCH" and the key, plus
     * runs of spaces inside the key. The stored key must be the
     * canonical form ("ESO INS LAMP1") and lookup with that form
     * must hit.
     */
    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);
    fitsbuf_raw_card(&b,
        "HIERARCH    ESO   INS  LAMP1   = 'X'");
    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_OK, "load succeeds");

    /* Stored key is canonical. */
    CHECK(strcmp(header.keywords[5].key, "ESO INS LAMP1") == 0,
          "stored key collapsed to canonical spacing");
    CHECK(strcmp(header.keywords[5].value, "X") == 0, "value parsed");

    /* Lookup with same canonical form. */
    CHECK(tinyfits_get_keyword(&header, "ESO INS LAMP1") != NULL,
          "canonical lookup hits");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_hierarch_with_continue(void)
{
    printf("Testing HIERARCH + CONTINUE chain ...\n");

    /* HIERARCH head whose long string value uses CONTINUE. The merge
     * logic on the CONTINUE card operates on "the most recently pushed
     * keyword", which is the HIERARCH entry.
     */
    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);
    fitsbuf_raw_card(&b, "HIERARCH ESO INS COMMENT = 'first part&'");
    fitsbuf_continue(&b, "' second part'");
    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_OK, "load succeeds");

    const TinyFitsKeyword* v = tinyfits_get_keyword(&header, "ESO INS COMMENT");
    CHECK(v && strcmp(v->value, "first part second part") == 0,
          "HIERARCH chain merged");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_hierarch_missing_separator(void)
{
    printf("Testing HIERARCH missing separator -> ERR_INVALID ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);
    /* No '=' anywhere on the card. */
    fitsbuf_raw_card(&b, "HIERARCH ESO INS NO SEPARATOR HERE");
    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_ERR_INVALID, "load fails with ERR_INVALID");
    CHECK(header.keywords == NULL, "struct zeroed on failure");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_hierarch_empty_key(void)
{
    printf("Testing HIERARCH empty key -> ERR_INVALID ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);
    fitsbuf_raw_card(&b, "HIERARCH    = 'X'");
    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_ERR_INVALID, "load fails with ERR_INVALID");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_hierarch_short_no_space_rejected(void)
{
    printf("Testing HIERARCH short-no-space key -> ERR_INVALID ...\n");

    /* Short HIERARCH-class keys (<= 8 chars, no space) are ambiguous
     * with the standard 8-char namespace; the parser rejects.
     */
    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);
    fitsbuf_raw_card(&b, "HIERARCH MYKEY = 'X'");
    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_ERR_INVALID,
          "short no-space HIERARCH key rejected");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_hierarch_lookup_normalization(void)
{
    printf("Testing HIERARCH lookup normalization ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);
    fitsbuf_raw_card(&b, "HIERARCH ESO INS LAMP = 'X'");
    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_OK, "load succeeds");

    /* Various lookup spellings of the same canonical key all hit. */
    CHECK(tinyfits_get_keyword(&header, "ESO INS LAMP") != NULL,
          "canonical hits");
    CHECK(tinyfits_get_keyword(&header, "ESO  INS  LAMP") != NULL,
          "double-spaced lookup normalizes and hits");
    CHECK(tinyfits_get_keyword(&header, "  ESO INS LAMP  ") != NULL,
          "leading/trailing-space lookup normalizes and hits");
    /* HIERARCH-class lookups are case-sensitive. */
    CHECK(tinyfits_get_keyword(&header, "eso ins lamp") == NULL,
          "lowercase HIERARCH lookup misses (case-sensitive)");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_hierarch_forbidden_chars(void)
{
    printf("Testing HIERARCH forbidden characters in key -> ERR_INVALID ...\n");

    /* "'" in the long key is forbidden -- easier to construct than '=',
     * which is the separator.
     */
    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);
    fitsbuf_raw_card(&b, "HIERARCH ESO IN'S LAMP = 'X'");
    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_ERR_INVALID,
          "HIERARCH key with apostrophe rejected");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

/* Build a minimal saveable header struct (UINT8 2x2). The caller adds
 * keywords, then the helper save+reload flow checks round-trip.
 */
static void make_minimal_header(TinyFitsHeader* header)
{
    header->width = 2; header->height = 2; header->num_channels = 1;
    header->pixel_type = TINYFITS_UINT8;
    header->bscale = 1.0; header->bzero = 0.0;
}

/* A long string with "''" literal-quote pairs should round-trip
 * byte-for-byte through save+load.
 */
static void test_continue_write_long_roundtrip(void)
{
    printf("Testing CONTINUE write: 250-char value with quote-pairs round-trips ...\n");

    /* Build a 250-char value that includes literal apostrophe chars at multiple
     * positions, including positions that the chunker is likely to land
     * near a card boundary.
     */
    char value[256];
    for (int i = 0; i < 250; i++)
        value[i] = (char)('A' + (i % 26));
    /* Sprinkle literal apostrophes; each requires 2 encoded bytes. */
    value[5]   = '\'';
    value[60]  = '\'';
    value[63]  = '\'';
    value[127] = '\'';
    value[200] = '\'';
    value[249] = '\'';
    value[250] = '\0';

    TinyFitsHeader w = {0};
    make_minimal_header(&w);
    int err = tinyfits_set_keyword(&w, "LONGSTR", value, "");
    CHECK(err == TINYFITS_OK, "set long LONGSTR");

    uint8_t pixels[4] = {1, 2, 3, 4};
    void* fdata = NULL;
    size_t fsize = 0;
    err = tinyfits_save_to_memory(&w, pixels, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");
    tinyfits_free_header(&w);

    TinyFitsHeader r = {0};
    void* px = NULL;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &px);
    CHECK(err == TINYFITS_OK, "reload succeeds");

    const TinyFitsKeyword* round = tinyfits_get_keyword(&r, "LONGSTR");
    CHECK(round != NULL, "LONGSTR present after reload");
    CHECK(round && strlen(round->value) == 250, "reload length 250");
    CHECK(round && strcmp(round->value, value) == 0, "reload byte-for-byte match");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

/* A value composed entirely of apostrophe chars should round-trip.
 * Each apostrophe needs 2 encoded bytes via quote-doubling; the chunker
 * must never split a pair across cards.
 */
static void test_continue_write_quotepair_atomicity(void)
{
    printf("Testing CONTINUE write: all-apostrophe value preserves quote pairs ...\n");

    char value[101];
    memset(value, '\'', 100);
    value[100] = '\0';

    TinyFitsHeader w = {0};
    make_minimal_header(&w);
    int err = tinyfits_set_keyword(&w, "QUOTES", value, "");
    CHECK(err == TINYFITS_OK, "set all-quote value");

    uint8_t pixels[4] = {0};
    void* fdata = NULL;
    size_t fsize = 0;
    err = tinyfits_save_to_memory(&w, pixels, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");
    tinyfits_free_header(&w);

    TinyFitsHeader r = {0};
    void* px = NULL;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &px);
    CHECK(err == TINYFITS_OK, "reload succeeds");

    const TinyFitsKeyword* round = tinyfits_get_keyword(&r, "QUOTES");
    CHECK(round && strlen(round->value) == 100, "reload length 100");
    CHECK(round && strcmp(round->value, value) == 0, "all-quote value preserved");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

/* A comment that overflows the final card should be silently
 * truncated; the value should be preserved faithfully.
 */
static void test_continue_write_comment_truncation(void)
{
    printf("Testing CONTINUE write: long comment truncated, value preserved ...\n");

    char value[101];
    for (int i = 0; i < 100; i++) value[i] = (char)('A' + (i % 26));
    value[100] = '\0';

    /* 200-char comment -- far longer than any card's remaining space. */
    char comment[201];
    memset(comment, 'C', 200);
    comment[200] = '\0';

    TinyFitsHeader w = {0};
    make_minimal_header(&w);
    int err = tinyfits_set_keyword(&w, "BIGSTR", value, comment);
    CHECK(err == TINYFITS_OK, "set with long comment");

    uint8_t pixels[4] = {0};
    void* fdata = NULL;
    size_t fsize = 0;
    err = tinyfits_save_to_memory(&w, pixels, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds (comment truncated, no error)");
    tinyfits_free_header(&w);

    TinyFitsHeader r = {0};
    void* px = NULL;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &px);
    CHECK(err == TINYFITS_OK, "reload succeeds (no malformed cards)");

    /* Value preserved faithfully. */
    const TinyFitsKeyword* round = tinyfits_get_keyword(&r, "BIGSTR");
    CHECK(round && strcmp(round->value, value) == 0, "value preserved");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

/* Save should return TINYFITS_ERR_KEYWORD_LENGTH when the user's
 * keywords would push the header past the cap.
 */
static void test_continue_write_header_cap(void)
{
    printf("Testing CONTINUE write: total-header cap enforced ...\n");

    /* Build a value long enough that one keyword expands to many cards.
     * 67 source chars per intermediate card; pick a value that consumes
     * ~half the cap on its own, then add many of them.
     */
    TinyFitsHeader w = {0};
    make_minimal_header(&w);

    /* 8000-char value x 5 keywords ~ 8000/67 * 5 = ~600 cards: well
     * inside the 36864-card cap and saves cleanly.
     */
    char value[8001];
    memset(value, 'X', 8000);
    value[8000] = '\0';
    char key[16];
    for (int i = 0; i < 5; i++)
    {
        snprintf(key, sizeof(key), "K%d", i);
        int err = tinyfits_set_keyword(&w, key, value, "");
        CHECK(err == TINYFITS_OK, "set ok pre-cap");
    }
    {
        uint8_t pixels[4] = {0};
        void* fdata = NULL;
        size_t fsize = 0;
        int err = tinyfits_save_to_memory(&w, pixels, &fdata, &fsize, 0);
        CHECK(err == TINYFITS_OK, "save under cap succeeds");
        tinyfits_free_buffer(fdata);
    }

    /* Now push past the cap by adding many more high-card keywords.
     * Each keyword with 8000-char value uses ~120 cards. The cap is
     * TINYFITS_MAX_HEADER_BLOCKS * TINYFITS_CARDS_PER_BLOCK = 1024 * 36
     * = 36864. So ~310 of these keywords clears the cap. We use 350 to
     * be sure.
     */
    for (int i = 5; i < 350; i++)
    {
        snprintf(key, sizeof(key), "K%d", i);
        int err = tinyfits_set_keyword(&w, key, value, "");
        CHECK(err == TINYFITS_OK, "set ok during build");
    }
    {
        uint8_t pixels[4] = {0};
        void* fdata = NULL;
        size_t fsize = 0;
        int err = tinyfits_save_to_memory(&w, pixels, &fdata, &fsize, 0);
        CHECK(err == TINYFITS_ERR_KEYWORD_LENGTH,
              "save over cap returns ERR_KEYWORD_LENGTH");
        CHECK(fdata == NULL, "no partial output buffer on cap exceeded");
    }

    tinyfits_free_header(&w);
}

/* Unit tests for tinyfits__chunk_budget covering each
 * card-kind / has-continuation / has-comment combination.
 */
static void test_chunk_budget_unit(void)
{
    printf("Testing tinyfits__chunk_budget unit cases ...\n");

    /* HEAD continuing: 67 regardless of comment (continuation cards
     * never carry comments).
     */
    CHECK(tinyfits__chunk_budget(TINYFITS_VC_HEAD, 1, 0,  0) == 67,
          "HEAD continuing no-comment = 67");
    CHECK(tinyfits__chunk_budget(TINYFITS_VC_HEAD, 1, 50, 0) == 67,
          "HEAD continuing ignores comment");

    /* HEAD final: 68 - C, where C = 0 or 3 + comment_len. */
    CHECK(tinyfits__chunk_budget(TINYFITS_VC_HEAD, 0, 0, 0) == 68,
          "HEAD final no-comment = 68");
    CHECK(tinyfits__chunk_budget(TINYFITS_VC_HEAD, 0, 10, 0) == 68 - 13,
          "HEAD final 10-char comment = 55");

    /* CONTINUE same shape as HEAD for budget purposes. */
    CHECK(tinyfits__chunk_budget(TINYFITS_VC_CONTINUE, 1, 0, 0) == 67,
          "CONTINUE continuing = 67");
    CHECK(tinyfits__chunk_budget(TINYFITS_VC_CONTINUE, 0, 0, 0) == 68,
          "CONTINUE final no-comment = 68");
    CHECK(tinyfits__chunk_budget(TINYFITS_VC_CONTINUE, 0, 20, 0) == 68 - 23,
          "CONTINUE final 20-char comment = 45");

    /* HIERARCH continuing: 65 - K. */
    CHECK(tinyfits__chunk_budget(TINYFITS_VC_HIERARCH, 1, 0, 10) == 55,
          "HIERARCH K=10 continuing = 55");
    CHECK(tinyfits__chunk_budget(TINYFITS_VC_HIERARCH, 1, 0, 63) == 2,
          "HIERARCH K=63 continuing = 2 (tightest)");

    /* HIERARCH final: 66 - K - C. */
    CHECK(tinyfits__chunk_budget(TINYFITS_VC_HIERARCH, 0, 0, 10) == 56,
          "HIERARCH K=10 final no-comment = 56");
    CHECK(tinyfits__chunk_budget(TINYFITS_VC_HIERARCH, 0, 8, 10) == 45,
          "HIERARCH K=10 final 8-char comment = 45");

    /* Overflow: C > available -> 0 (clamp). */
    CHECK(tinyfits__chunk_budget(TINYFITS_VC_HEAD, 0, 100, 0) == 0,
          "huge comment clamped to 0 budget");
}


/* HIERARCH key with a short string value should round-trip. Tests
 * the single-card HIERARCH head emission.
 */
static void test_hierarch_write_short_value(void)
{
    printf("Testing HIERARCH write: short value round-trips ...\n");

    /* 50-char HIERARCH key, short value. */
    const char* hkey = "ESO INSTRUMENT TEMPERATURE SENSOR ARRAY ELEMENT 7";

    TinyFitsHeader w = {0};
    make_minimal_header(&w);
    int err = tinyfits_set_keyword(&w, hkey, "23.5K", "calibrated");
    CHECK(err == TINYFITS_OK, "set HIERARCH ok");

    uint8_t pixels[4] = {1, 2, 3, 4};
    void* fdata = NULL;
    size_t fsize = 0;
    err = tinyfits_save_to_memory(&w, pixels, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save ok");
    tinyfits_free_header(&w);

    TinyFitsHeader r = {0};
    void* px = NULL;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &px);
    CHECK(err == TINYFITS_OK, "reload ok");
    const TinyFitsKeyword* v = tinyfits_get_keyword(&r, hkey);
    CHECK(v && strcmp(v->value, "23.5K") == 0, "HIERARCH value preserved");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

/* Test long HIERARCH key with a 1-char value (the tightest boundary that still fits). */
static void test_hierarch_write_long_key_min_value(void)
{
    printf("Testing HIERARCH write: long key with 1-char value round-trips ...\n");

    char hkey[TINYFITS_HIERARCH_KEY_MAX + 1];
    memset(hkey, 'A', TINYFITS_HIERARCH_KEY_MAX);
    hkey[TINYFITS_HIERARCH_KEY_MAX] = '\0';

    TinyFitsHeader w = {0};
    make_minimal_header(&w);
    int err = tinyfits_set_keyword(&w, hkey, "X", "");
    CHECK(err == TINYFITS_OK, "set at-cap key ok");

    uint8_t pixels[4] = {0};
    void* fdata = NULL;
    size_t fsize = 0;
    err = tinyfits_save_to_memory(&w, pixels, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save ok");
    tinyfits_free_header(&w);

    TinyFitsHeader r = {0};
    void* px = NULL;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &px);
    CHECK(err == TINYFITS_OK, "reload ok");
    const TinyFitsKeyword* v = tinyfits_get_keyword(&r, hkey);
    CHECK(v && strcmp(v->value, "X") == 0,
          "at-cap key + 1-char value preserved");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

/* Overly-long HIERARCH key should be rejected. */
static void test_hierarch_write_over_cap(void)
{
    printf("Testing HIERARCH write: long key rejected ...\n");

    char hkey[TINYFITS_HIERARCH_KEY_MAX + 2];
    memset(hkey, 'A', TINYFITS_HIERARCH_KEY_MAX + 1);
    hkey[TINYFITS_HIERARCH_KEY_MAX + 1] = '\0';

    TinyFitsHeader w = {0};
    make_minimal_header(&w);
    int err = tinyfits_set_keyword(&w, hkey, "X", "");
    CHECK(err == TINYFITS_ERR_KEYWORD_LENGTH, "long HIERARCH key rejected");
    tinyfits_free_header(&w);
}

/* HIERARCH key with a long string value should emit a HIERARCH head plus
 * CONTINUE chain; reload should re-assemble the value correctly.
 */
static void test_hierarch_write_long_value_chain(void)
{
    printf("Testing HIERARCH write: long value chains via CONTINUE ...\n");

    const char* hkey = "ESO INS LONG STRING";

    char value[181];
    for (int i = 0; i < 180; i++) value[i] = (char)('a' + (i % 26));
    value[180] = '\0';

    TinyFitsHeader w = {0};
    make_minimal_header(&w);
    int err = tinyfits_set_keyword(&w, hkey, value, "");
    CHECK(err == TINYFITS_OK, "set HIERARCH long string ok");

    uint8_t pixels[4] = {0};
    void* fdata = NULL;
    size_t fsize = 0;
    err = tinyfits_save_to_memory(&w, pixels, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save ok");
    tinyfits_free_header(&w);

    TinyFitsHeader r = {0};
    void* px = NULL;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &px);
    CHECK(err == TINYFITS_OK, "reload ok");
    const TinyFitsKeyword* v = tinyfits_get_keyword(&r, hkey);
    CHECK(v && strlen(v->value) == 180, "reloaded length 180");
    CHECK(v && strcmp(v->value, value) == 0, "HIERARCH chain content matches");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

static void test_hierarch_write_numeric_freeform(void)
{
    printf("Testing HIERARCH write: numeric/logical free-format placement ...\n");

    const char* hkey  = "ESO INS NUMVAL";
    const char* hkey2 = "ESO INS BOOLVAL";

    TinyFitsHeader w = {0};
    make_minimal_header(&w);
    int err = tinyfits_set_keyword(&w, hkey,  "42", "");
    CHECK(err == TINYFITS_OK, "set numeric HIERARCH ok");
    err = tinyfits_set_keyword(&w, hkey2, "T", "logical flag");
    CHECK(err == TINYFITS_OK, "set logical HIERARCH ok");

    uint8_t pixels[4] = {0};
    void* fdata = NULL;
    size_t fsize = 0;
    err = tinyfits_save_to_memory(&w, pixels, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save ok");
    tinyfits_free_header(&w);

    TinyFitsHeader r = {0};
    void* px = NULL;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &px);
    CHECK(err == TINYFITS_OK, "reload ok");

    const TinyFitsKeyword* v1 = tinyfits_get_keyword(&r, hkey);
    const TinyFitsKeyword* v2 = tinyfits_get_keyword(&r, hkey2);
    CHECK(v1 && strcmp(v1->value, "42") == 0, "HIERARCH numeric preserved");
    CHECK(v2 && strcmp(v2->value, "T")  == 0, "HIERARCH logical preserved");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

/* 300-char HISTORY should auto-split into the expected number of
 * cards using the documented split algorithm.
 */
static void test_history_auto_split(void)
{
    printf("Testing HISTORY auto-split: 300-char text ...\n");

    /* 300-char text. With 72-byte payload per card and split-after-
     * whitespace, the chunk count depends on whitespace placement.
     * Use a text with regular spaces every ~25 chars so splits land
     * deterministically.
     */
    char text[301];
    for (int i = 0; i < 300; i++)
        text[i] = ((i + 1) % 25 == 0) ? ' ' : (char)('A' + (i % 26));
    text[300] = '\0';

    TinyFitsHeader header = {0};
    int err = tinyfits_add_history(&header, text);
    CHECK(err == TINYFITS_OK, "add_history ok");

    /* Total HISTORY entries: at least ceil(300/72) == 5, at most
     * a few more depending on where spaces fall.
     */
    int n = tinyfits_get_keywords(&header, "HISTORY", NULL, 0);
    CHECK(n >= 5, "at least 5 HISTORY entries (300/72)");
    CHECK(n <= 8, "no more than 8 HISTORY entries");

    /* Concatenating all entries reproduces the source byte-for-byte
     * (split is whitespace-preserving).
     */
    const TinyFitsKeyword* parts[16];
    int got = tinyfits_get_keywords(&header, "HISTORY", parts, 16);
    CHECK(got == n, "get_keywords agrees with count");

    char rebuilt[400];
    rebuilt[0] = '\0';
    for (int i = 0; i < got; i++)
    {
        size_t cur = strlen(rebuilt);
        size_t left = sizeof(rebuilt) - cur;
        snprintf(rebuilt + cur, left, "%s", parts[i]->value);
    }
    CHECK(strcmp(rebuilt, text) == 0,
          "concatenated chunks reproduce the source byte-for-byte");

    tinyfits_free_header(&header);
}

/* Empty input should emit exactly one empty card. */
static void test_history_empty_input(void)
{
    printf("Testing HISTORY empty input emits one empty card ...\n");

    TinyFitsHeader header = {0};
    int err = tinyfits_add_history(&header, "");
    CHECK(err == TINYFITS_OK, "add empty history ok");
    CHECK(tinyfits_get_keywords(&header, "HISTORY", NULL, 0) == 1,
          "exactly one HISTORY entry");
    const TinyFitsKeyword* v = tinyfits_get_keyword(&header, "HISTORY");
    CHECK(v->value && v->value[0] == '\0', "empty value");

    err = tinyfits_add_comment(&header, "");
    CHECK(err == TINYFITS_OK, "add empty comment ok");
    CHECK(tinyfits_get_keywords(&header, "COMMENT", NULL, 0) == 1,
          "exactly one COMMENT entry");

    tinyfits_free_header(&header);
}

/* Non-printable bytes should return ERR_INVALID before any allocation. */
static void test_history_non_printable_rejected(void)
{
    printf("Testing HISTORY non-printable bytes rejected ...\n");

    TinyFitsHeader header = {0};
    int err = tinyfits_add_history(&header, "ok\ttab");
    CHECK(err == TINYFITS_ERR_INVALID, "tab in HISTORY rejected");
    err = tinyfits_add_history(&header, "ok\nbad");
    CHECK(err == TINYFITS_ERR_INVALID, "newline in HISTORY rejected");
    /* Pre-rejection means no entries were created. */
    CHECK(header.num_keywords == 0, "no entries on rejection");
    tinyfits_free_header(&header);
}

/* Passing a non-empty comment for HISTORY/COMMENT should be rejected
 * with ERR_INVALID (no per-card comment slot).
 */
static void test_history_non_empty_comment_rejected(void)
{
    printf("Testing HISTORY non-empty comment rejected ...\n");

    TinyFitsHeader header = {0};
    int err = tinyfits_append_keyword(&header, "HISTORY", "text", "comment");
    CHECK(err == TINYFITS_ERR_INVALID, "add_keyword HISTORY w/ comment rejected");
    err = tinyfits_append_keyword(&header, "COMMENT", "text", "comment");
    CHECK(err == TINYFITS_ERR_INVALID, "add_keyword COMMENT w/ comment rejected");
    err = tinyfits_set_keyword(&header, "HISTORY", "text", "comment");
    CHECK(err == TINYFITS_ERR_INVALID, "set_keyword HISTORY w/ comment rejected");
    tinyfits_free_header(&header);
}

/* set_keyword on HISTORY/COMMENT with a value that doesn't fit
 * on one card should return ERR_KEYWORD_LENGTH.
 */
static void test_history_set_long_value_rejected(void)
{
    printf("Testing set_keyword HISTORY long value rejected ...\n");

    TinyFitsHeader header = {0};
    char text[100];
    memset(text, 'A', 73);  /* 73 > 72 payload */
    text[73] = '\0';
    int err = tinyfits_set_keyword(&header, "HISTORY", text, "");
    CHECK(err == TINYFITS_ERR_KEYWORD_LENGTH, "73-char HISTORY set rejected");

    /* Exactly 72 chars: accepted. */
    text[72] = '\0';
    err = tinyfits_set_keyword(&header, "HISTORY", text, "");
    CHECK(err == TINYFITS_OK, "72-char HISTORY set accepted");

    tinyfits_free_header(&header);
}

/* save+reload round-trip should preserve the same number of HISTORY
 * entries (each card writes back as one card).
 */
static void test_history_split_roundtrip_byte_faithful(void)
{
    printf("Testing HISTORY save/reload preserves entry count ...\n");

    /* 250-char text -- split-on-whitespace produces several entries. */
    char text[251];
    for (int i = 0; i < 250; i++)
        text[i] = ((i + 1) % 30 == 0) ? ' ' : (char)('a' + (i % 26));
    text[250] = '\0';

    TinyFitsHeader w = {0};
    make_minimal_header(&w);
    int err = tinyfits_add_history(&w, text);
    CHECK(err == TINYFITS_OK, "add long history ok");
    int n_pre = tinyfits_get_keywords(&w, "HISTORY", NULL, 0);
    CHECK(n_pre >= 4, "got multiple HISTORY entries pre-save");

    uint8_t pixels[4] = {0};
    void* fdata = NULL; size_t fsize = 0;
    err = tinyfits_save_to_memory(&w, pixels, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save ok");
    tinyfits_free_header(&w);

    TinyFitsHeader r = {0};
    void* px = NULL;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &px);
    CHECK(err == TINYFITS_OK, "reload ok");
    int n_post = tinyfits_get_keywords(&r, "HISTORY", NULL, 0);
    CHECK(n_post == n_pre, "HISTORY entry count preserved");

    /* Concatenated post-reload reproduces the source. */
    const TinyFitsKeyword* parts[32];
    int got = tinyfits_get_keywords(&r, "HISTORY", parts, 32);
    char rebuilt[400] = "";
    for (int i = 0; i < got; i++)
    {
        size_t cur = strlen(rebuilt);
        size_t left = sizeof(rebuilt) - cur;
        snprintf(rebuilt + cur, left, "%s", parts[i]->value);
    }
    CHECK(strcmp(rebuilt, text) == 0,
          "post-reload concat reproduces source byte-for-byte");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&r);
    tinyfits_free_buffer(fdata);
}

/* Astropy-leniency: a card whose first 8 bytes form a key but which
 * lacks the '= ' indicator at byte 8 falls back to free-form text
 * parsing -- the post-key bytes become the value (not silently dropped).
 * Mirrors astropy.io.fits behavior on cards like the historical
 * 'START_AIRMASS = 1.134 / Airmass at start' local convention.
 */
static void test_nonstandard_card_freeform_fallback(void)
{
    printf("Testing non-standard card free-form fallback ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);
    /* "START_AIRMASS = 1.134 / Airmass at start" -- 13-char key with no
     * space; '=' lands at byte 14, not byte 8. Pre-fallback this would
     * have parsed as key="START_AI", value="" (post-key bytes dropped).
     * With the fallback it keeps the bytes as the value.
     */
    fitsbuf_raw_card(&b, "START_AIRMASS = 1.134 / Airmass at start");
    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_OK, "load succeeds");

    /* The 8-char-truncated key is "START_AI"; post-key bytes (starting
     * at byte 8) form the value. The literal '=' and '/' are inside
     * the value string, not parsed as comment delimiters.
     */
    const TinyFitsKeyword* v = tinyfits_get_keyword(&header, "START_AI");
    CHECK(v != NULL, "START_AI present");
    CHECK(v && strcmp(v->value, "RMASS = 1.134 / Airmass at start") == 0,
          "post-key bytes preserved as value");

    /* Round-trip: save + reload yields the same key/value pair (the
     * card layout shifts to a quoted-string form, but no data is lost).
     */
    void* fdata = NULL; size_t fsize = 0;
    err = tinyfits_save_to_memory(&header, px, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader info2 = {0};
    void* px2 = NULL;
    err = tinyfits_load_from_memory(&info2, fdata, fsize, &px2);
    CHECK(err == TINYFITS_OK, "reload succeeds");
    const TinyFitsKeyword* v2 = tinyfits_get_keyword(&info2, "START_AI");
    CHECK(v2 && strcmp(v2->value, v->value) == 0,
          "non-standard card round-trips through save+reload");

    tinyfits_free_buffer(px);
    tinyfits_free_buffer(px2);
    tinyfits_free_buffer(fdata);
    tinyfits_free_header(&header);
    tinyfits_free_header(&info2);
    fitsbuf_free(&b);
}

/* The reserved-key set (SIMPLE, BITPIX, NAXIS, NAXISn, ...) lives in the
 * standard 8-char namespace. HIERARCH-class keys (containing a space OR
 * length > 8) are a different namespace; a HIERARCH key like
 * "ESO INS NAXIS1" must not collide with the structural NAXISn check.
 * Also exercises a long all-digit-suffix HIERARCH key to confirm the
 * NAXISn suffix parser is never invoked on it (which would otherwise
 * overflow on long digit runs).
 */
static void test_hierarch_namespace_independent_of_reserved(void)
{
    printf("Testing HIERARCH-class keys bypass standard reserved set ...\n");

    TinyFitsHeader header = {0};
    /* HIERARCH key sharing a substring with "NAXIS1" is fine. */
    int err = tinyfits_set_keyword(&header, "ESO INS NAXIS1", "v", "");
    CHECK(err == TINYFITS_OK, "HIERARCH key with NAXIS substring accepted");
    /* Long all-digit-suffix HIERARCH key (would overflow naxis_suffix
     * if it were called -- it must not be).
     */
    err = tinyfits_set_keyword(&header, "NAXIS999999999999", "v", "");
    CHECK(err == TINYFITS_OK,
          "long NAXIS-prefixed HIERARCH key accepted (no overflow)");
    /* Standard-namespace NAXISn still rejected. */
    err = tinyfits_set_keyword(&header, "NAXIS3", "v", "");
    CHECK(err == TINYFITS_ERR_RESERVED_KEYWORD,
          "standard NAXIS3 still reserved");

    tinyfits_free_header(&header);
}

/* Regression: a HISTORY or COMMENT card that uses the full 72-byte
 * free-form payload (bytes 8..79, no leading-space convention) must
 * round-trip the final byte. An older bug capped the parser at 71 chars,
 * silently dropping the last byte for full-width cards.
 */
static void test_history_full_payload_not_truncated(void)
{
    printf("Testing HISTORY/COMMENT full 72-byte payload not truncated ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 2, 2);
    /* Full TINYFITS_HISTORY_PAYLOAD-byte COMMENT body: 'A' fills all but
     * the last byte; 'Z' marks the final byte to detect off-by-one
     * truncation in the parser.
     */
    {
        char card[TINYFITS_CARD_SIZE];
        memset(card, ' ', TINYFITS_CARD_SIZE);
        memcpy(card, "COMMENT", 7);  /* trailing space already from memset */
        memset(card + TINYFITS_CARD_KEY_LEN, 'A', TINYFITS_HISTORY_PAYLOAD - 1);
        card[TINYFITS_CARD_SIZE - 1] = 'Z';
        fitsbuf_append(&b, card, TINYFITS_CARD_SIZE);
    }
    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* px;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &px);
    CHECK(err == TINYFITS_OK, "load succeeds");

    const TinyFitsKeyword* v = tinyfits_get_keyword(&header, "COMMENT");
    CHECK(v != NULL, "COMMENT present");
    CHECK(v->value && strlen(v->value) == 72, "COMMENT preserved at full 72 bytes");
    CHECK(v->value && v->value[71] == 'Z',    "final byte 'Z' not dropped");

    tinyfits_free_buffer(px);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_nonstandard_scale_roundtrip(void)
{
    printf("Testing non-standard BSCALE round-trip ...\n");
    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 4, 1);
    fitsbuf_card_float(&b, "BSCALE", 2e-16);
    fitsbuf_card_float(&b, "BZERO", 0.0);
    fitsbuf_end(&b);
    int16_t pixels_in[4] = {0, 100, -100, 32767};
    for (int i = 0; i < 4; i++)
    {
        uint8_t bytes[2];
        write_be16(bytes, pixels_in[i]);
        fitsbuf_append(&b, bytes, 2);
    }
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(header.pixel_type == TINYFITS_INT16, "INT16 pixel_type (literal case)");
    CHECK(header.bscale == 2e-16, "bscale recorded on struct");
    CHECK(header.bzero == 0.0, "bzero recorded on struct");

    /* Round-trip via save */
    void* fdata; size_t fsize;
    err = tinyfits_save_to_memory(&header, pixels, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFitsHeader r = {0};
    void* px2;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &px2);
    CHECK(err == TINYFITS_OK, "reload succeeds");
    CHECK(r.bscale == 2e-16, "bscale round-trips");
    CHECK(r.bzero == 0.0, "bzero round-trips");
    int16_t* px2_int = (int16_t*)px2;
    CHECK(px2_int[0] == 0 && px2_int[3] == 32767, "pixel values round-trip");

    tinyfits_free_buffer(pixels);
    tinyfits_free_buffer(px2);
    tinyfits_free_buffer(fdata);
    tinyfits_free_header(&header);
    tinyfits_free_header(&r);
    fitsbuf_free(&b);
}

static void test_nan_inf_rejection(void)
{
    printf("Testing NaN/Inf BSCALE/BZERO rejection ...\n");

    /* BSCALE=NaN */
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, 16, 4, 1);
        fitsbuf_card_float(&b, "BSCALE", NAN);
        fitsbuf_end(&b);
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
        CHECK(err == TINYFITS_ERR_BZERO_BSCALE, "BSCALE=NaN rejected");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }
    /* BSCALE=+Inf */
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, 16, 4, 1);
        fitsbuf_card_float(&b, "BSCALE", INFINITY);
        fitsbuf_end(&b);
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
        CHECK(err == TINYFITS_ERR_BZERO_BSCALE, "BSCALE=+Inf rejected");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }
    /* BSCALE=-Inf */
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, 16, 4, 1);
        fitsbuf_card_float(&b, "BSCALE", -INFINITY);
        fitsbuf_end(&b);
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
        CHECK(err == TINYFITS_ERR_BZERO_BSCALE, "BSCALE=-Inf rejected");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }
    /* BZERO=NaN */
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, 16, 4, 1);
        fitsbuf_card_float(&b, "BZERO", NAN);
        fitsbuf_end(&b);
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
        CHECK(err == TINYFITS_ERR_BZERO_BSCALE, "BZERO=NaN rejected");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }
}

static void test_bscale_zero(void)
{
    printf("Testing BSCALE=0 load-permissive, save-rejects ...\n");

    /* Build a BITPIX=16 file with BSCALE=0, BZERO=42 */
    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 4, 1);
    fitsbuf_card_float(&b, "BSCALE", 0.0);
    fitsbuf_card_float(&b, "BZERO", 42.0);
    fitsbuf_end(&b);
    int16_t pixels_in[4] = {-1000, 0, 1000, 32767};
    for (int i = 0; i < 4; i++)
    {
        uint8_t bytes[2];
        write_be16(bytes, pixels_in[i]);
        fitsbuf_append(&b, bytes, 2);
    }
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds (load is permissive about BSCALE=0)");
    CHECK(header.bscale == 0.0, "bscale=0 preserved on load");
    CHECK(header.bzero == 42.0, "bzero preserved");

    /* physical: every pixel becomes BZERO (the degenerate transform) */
    float out[4];
    err = tinyfits_to_float_physical(&header, pixels, out);
    CHECK(err == TINYFITS_OK, "physical succeeds");
    CHECK_CLOSE(out[0], 42.0f, 1e-6f, "physical[0] = bzero");
    CHECK_CLOSE(out[1], 42.0f, 1e-6f, "physical[1] = bzero");
    CHECK_CLOSE(out[2], 42.0f, 1e-6f, "physical[2] = bzero");
    CHECK_CLOSE(out[3], 42.0f, 1e-6f, "physical[3] = bzero");

    /* normalized: doesn't consult bscale, produces correctly-shaped output */
    err = tinyfits_to_float_normalized(&header, pixels, out);
    CHECK(err == TINYFITS_OK, "normalized succeeds");
    CHECK(out[0] != out[1], "normalized still varies (bscale=0 ignored)");

    /* Save rejects bscale=0 to keep callers from silently writing degenerate
     * files (and to catch the {0}-init footgun). Caller must set bscale=1.
     */
    void* fdata; size_t fsize;
    err = tinyfits_save_to_memory(&header, pixels, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_BZERO_BSCALE, "save rejects bscale=0");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_save_validation_unsigned_with_scale(void)
{
    printf("Testing save rejects non-default scaling on unsigned types ...\n");
    uint16_t src[] = {1, 2, 3, 4};
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT16;
    w.bscale = 2.0; w.bzero = 0.0;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_BZERO_BSCALE, "UINT16 with bscale=2.0 rejected");
    CHECK(fdata == NULL, "no buffer allocated on rejection");

    /* UINT16 with bzero=32768 (caller mistakenly providing canonical pair on
     * unsigned struct) is also rejected -- the contract is bscale=1, bzero=0
     * for unsigned pixel_type.
     */
    w.bscale = 1.0; w.bzero = 32768.0;
    err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_BZERO_BSCALE, "UINT16 with bzero=32768 on struct rejected");
}

static void test_save_validation_user_bscale_keyword(void)
{
    printf("Testing save rejects user-supplied BSCALE/BZERO keywords ...\n");
    int16_t src[] = {1, 2, 3, 4};
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_INT16;
    w.bscale = 1.0; w.bzero = 0.0;

    /* Bypass the set_keyword reserved-key guard by appending directly via
     * the public API: set_keyword would already block this, but a caller
     * could mutate the keywords array. Simulate by adding a non-reserved
     * key first, then patching its name.
     */
    int err = tinyfits_set_keyword(&w, "OBJECT", "test", "");
    CHECK(err == TINYFITS_OK, "set OBJECT");
    /* Patch the keyword name to BSCALE in place (bypassing API guards). */
    TINYFITS_FREE(w.keywords[0].key);
    w.keywords[0].key = tinyfits__strdup("BSCALE");

    void* fdata; size_t fsize;
    err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_RESERVED_KEYWORD, "user BSCALE keyword rejected");

    /* Same for BZERO */
    TINYFITS_FREE(w.keywords[0].key);
    w.keywords[0].key = tinyfits__strdup("BZERO");
    err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_RESERVED_KEYWORD, "user BZERO keyword rejected");

    tinyfits_free_header(&w);
}

static void test_save_validation_long_hierarch_key(void)
{
    printf("Testing save rejects over-cap HIERARCH key (direct mutation) ...\n");

    /* Set up a header containing one valid HIERARCH-class keyword via the
     * public API, then mutate the stored key in place to a length that
     * exceeds TINYFITS_HIERARCH_KEY_MAX. Without the writer-side cap, the
     * save path would overflow the 80-byte stack card.
     */
    int16_t src[] = {1, 2, 3, 4};
    TinyFitsHeader w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_INT16;
    w.bscale = 1.0; w.bzero = 0.0;
    int err = tinyfits_set_keyword(&w, "ESO TEL ALT", "45.0", "");
    CHECK(err == TINYFITS_OK, "set valid HIERARCH key");

    /* Build a 100-char HIERARCH-class key (> 63-char cap; has a space so
     * is_hierarch_class is true). */
    char long_key[101];
    memset(long_key, 'A', 100);
    long_key[100] = '\0';
    long_key[3] = ' ';
    TINYFITS_FREE(w.keywords[0].key);
    w.keywords[0].key = tinyfits__strdup(long_key);

    /* Try the string-value path (dispatches to write_string_chain). */
    void* fdata; size_t fsize;
    err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_KEYWORD_LENGTH,
          "long HIERARCH key + string value rejected");

    /* And the numeric-value path (dispatches to write_hierarch_nonstring). */
    TINYFITS_FREE(w.keywords[0].value);
    w.keywords[0].value = tinyfits__strdup("45.0");
    err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_KEYWORD_LENGTH,
          "long HIERARCH key + numeric value rejected");

    tinyfits_free_header(&w);
}

static void test_unsigned_classification_boundaries(void)
{
    printf("Testing unsigned-conversion exact-equality boundaries ...\n");

    /* BZERO=32768.00001 is NOT exactly 32768.0 -> literal case (INT16) */
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, 16, 4, 1);
        fitsbuf_card_float(&b, "BZERO", 32768.00001);
        fitsbuf_end(&b);
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
        CHECK(err == TINYFITS_OK, "load succeeds");
        CHECK(header.pixel_type == TINYFITS_INT16, "BZERO=32768.00001 -> INT16, not UINT16");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }
    /* BZERO=32768.0 exact -> UINT16 (unsigned conversion) */
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, 16, 4, 1);
        fitsbuf_card_float(&b, "BZERO", 32768.0);
        fitsbuf_end(&b);
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
        CHECK(err == TINYFITS_OK, "load succeeds");
        CHECK(header.pixel_type == TINYFITS_UINT16, "BZERO=32768.0 exact -> UINT16");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }
    /* BZERO=2147483648.0 exact -> UINT32 */
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, 32, 4, 1);
        fitsbuf_card_float(&b, "BZERO", 2147483648.0);
        fitsbuf_end(&b);
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
        CHECK(err == TINYFITS_OK, "load succeeds");
        CHECK(header.pixel_type == TINYFITS_UINT32, "BZERO=2147483648.0 -> UINT32");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }
}

static void test_bitpix64_rejected(void)
{
    printf("Testing BITPIX=64 rejection ...\n");

    /* Default scaling */
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, 64, 4, 1);
        fitsbuf_end(&b);
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
        CHECK(err == TINYFITS_ERR_BITPIX, "BITPIX=64 rejected with ERR_BITPIX");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }
    /* BITPIX=64 with weird BSCALE/BZERO -- still ERR_BITPIX (BITPIX checked first) */
    {
        FitsBuf b;
        fitsbuf_standard_header(&b, 64, 4, 1);
        fitsbuf_card_float(&b, "BSCALE", NAN);
        fitsbuf_end(&b);
        TinyFitsHeader header = {0};
        int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
        CHECK(err == TINYFITS_ERR_BITPIX,
              "BITPIX=64 + BSCALE=NaN -> BITPIX wins per validation order");
        tinyfits_free_header(&header);
        fitsbuf_free(&b);
    }
}

/* Helpers for multi-HDU tests: build a primary HDU with NAXIS=0 (no data
 * unit), and append IMAGE / BINTABLE extensions.
 */

static void fitsbuf_primary_no_data(FitsBuf* b)
{
    fitsbuf_init(b);
    fitsbuf_card(b, "SIMPLE", "                   T");
    fitsbuf_card_int(b, "BITPIX", 8);
    fitsbuf_card_int(b, "NAXIS", 0);
    fitsbuf_card(b, "EXTEND", "                   T");
    fitsbuf_end(b);
}

static void fitsbuf_image_extension_header(FitsBuf* b, int bitpix,
                                           int naxis1, int naxis2)
{
    fitsbuf_card_str(b, "XTENSION", "IMAGE   ");
    fitsbuf_card_int(b, "BITPIX", bitpix);
    fitsbuf_card_int(b, "NAXIS", 2);
    fitsbuf_card_int(b, "NAXIS1", naxis1);
    fitsbuf_card_int(b, "NAXIS2", naxis2);
    fitsbuf_card_int(b, "PCOUNT", 0);
    fitsbuf_card_int(b, "GCOUNT", 1);
    fitsbuf_end(b);
}

/* Append a BINTABLE extension with a given row-byte-width and row count.
 * Adds the table data block (zero-filled) and pads to block boundary.
 */
static void fitsbuf_bintable_extension(FitsBuf* b, int row_bytes, int rows)
{
    fitsbuf_card_str(b, "XTENSION", "BINTABLE");
    fitsbuf_card_int(b, "BITPIX", 8);
    fitsbuf_card_int(b, "NAXIS", 2);
    fitsbuf_card_int(b, "NAXIS1", row_bytes);
    fitsbuf_card_int(b, "NAXIS2", rows);
    fitsbuf_card_int(b, "PCOUNT", 0);
    fitsbuf_card_int(b, "GCOUNT", 1);
    fitsbuf_card_int(b, "TFIELDS", 0);
    fitsbuf_end(b);

    int data_bytes = row_bytes * rows;
    char* zeros = (char*)calloc(1, data_bytes);
    fitsbuf_append(b, zeros, data_bytes);
    free(zeros);
    fitsbuf_pad_to_block(b);
}

static void test_multi_hdu_image_in_extension(void)
{
    printf("Testing multi-HDU: primary NAXIS=0 + IMAGE extension ...\n");

    FitsBuf b;
    fitsbuf_primary_no_data(&b);
    fitsbuf_image_extension_header(&b, 16, 4, 1);
    /* 4 int16 pixels */
    uint8_t pix[8];
    write_be16(pix + 0, 100);
    write_be16(pix + 2, 200);
    write_be16(pix + 4, 300);
    write_be16(pix + 6, 400);
    fitsbuf_append(&b, pix, 8);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "loads IMAGE extension");
    CHECK(header.width == 4 && header.height == 1, "dimensions from extension");
    CHECK(header.pixel_type == TINYFITS_INT16, "pixel_type from extension BITPIX");
    int16_t* px = (int16_t*)pixels;
    CHECK(px[0] == 100 && px[1] == 200 && px[2] == 300 && px[3] == 400,
          "extension pixel data correct");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_multi_hdu_no_image_only_table(void)
{
    printf("Testing multi-HDU: primary NAXIS=0 + BINTABLE only ...\n");

    FitsBuf b;
    fitsbuf_primary_no_data(&b);
    fitsbuf_bintable_extension(&b, 16, 5);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_ERR_NO_IMAGE, "no image HDU found");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_multi_hdu_skip_table_to_image(void)
{
    printf("Testing multi-HDU: skip BINTABLE, load following IMAGE ...\n");

    FitsBuf b;
    fitsbuf_primary_no_data(&b);
    fitsbuf_bintable_extension(&b, 16, 5);
    fitsbuf_image_extension_header(&b, 16, 2, 1);
    uint8_t pix[4];
    write_be16(pix + 0, 7);
    write_be16(pix + 2, 11);
    fitsbuf_append(&b, pix, 4);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "skipped BINTABLE, loaded IMAGE");
    CHECK(header.width == 2 && header.height == 1, "dimensions from image after table");
    int16_t* px = (int16_t*)pixels;
    CHECK(px[0] == 7 && px[1] == 11, "pixel data from third HDU correct");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_multi_hdu_skip_empty_image(void)
{
    printf("Testing multi-HDU: skip empty IMAGE stub, load second IMAGE ...\n");

    FitsBuf b;
    fitsbuf_primary_no_data(&b);

    /* Empty IMAGE extension (NAXIS=0) -- header only, no data unit */
    fitsbuf_card_str(&b, "XTENSION", "IMAGE   ");
    fitsbuf_card_int(&b, "BITPIX", 16);
    fitsbuf_card_int(&b, "NAXIS", 0);
    fitsbuf_card_int(&b, "PCOUNT", 0);
    fitsbuf_card_int(&b, "GCOUNT", 1);
    fitsbuf_end(&b);

    /* Real IMAGE extension with data */
    fitsbuf_image_extension_header(&b, 16, 1, 1);
    uint8_t pix[2];
    write_be16(pix, 42);
    fitsbuf_append(&b, pix, 2);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&header, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "skipped empty IMAGE, loaded next IMAGE");
    CHECK(header.width == 1 && header.height == 1, "dimensions from third HDU");
    int16_t* px = (int16_t*)pixels;
    CHECK(px[0] == 42, "pixel data from real image correct");

    tinyfits_free_buffer(pixels);
    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

static void test_multi_hdu_first_image_unsupported_bitpix(void)
{
    printf("Testing multi-HDU: first image has BITPIX=64 -> ERR_BITPIX (no skip) ...\n");

    FitsBuf b;
    fitsbuf_primary_no_data(&b);
    /* IMAGE extension with BITPIX=64 (unsupported) */
    fitsbuf_image_extension_header(&b, 64, 2, 1);
    /* Need 2 * 8 = 16 bytes of data (BITPIX=64 -> 8 bytes per pixel) */
    uint8_t pix[16] = {0};
    fitsbuf_append(&b, pix, 16);
    fitsbuf_pad_to_block(&b);

    /* Even if a later HDU were a supported IMAGE, we should still error on
     * the first image we find. Add one to make sure we don't auto-skip.
     */
    fitsbuf_image_extension_header(&b, 16, 1, 1);
    uint8_t pix2[2];
    write_be16(pix2, 99);
    fitsbuf_append(&b, pix2, 2);
    fitsbuf_pad_to_block(&b);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header_from_memory(&header, b.data, b.size);
    CHECK(err == TINYFITS_ERR_BITPIX,
          "BITPIX=64 first image returns ERR_BITPIX (does not skip to next)");

    tinyfits_free_header(&header);
    fitsbuf_free(&b);
}

/* Exercises the file-streaming HDU walker (tinyfits_load_header(path)), which
 * is independent of the in-memory walker. The file walker uses fread for
 * headers and fseek to skip past non-image data units; this is the only
 * code path that exercises both of those. tinyfits_load(path) doesn't
 * help here because it slurps to memory before walking. Placed after the
 * multi-HDU helpers since it uses fitsbuf_primary_no_data etc.
 */
static void test_info_file_streaming(void)
{
    printf("Testing tinyfits_load_header(path) with multi-HDU file ...\n");

    /* Build a primary-no-data + BINTABLE + IMAGE file on disk. The
     * BINTABLE forces the file walker to fseek past a non-image data
     * block before reaching the IMAGE HDU's header.
     */
    FitsBuf b;
    fitsbuf_primary_no_data(&b);
    fitsbuf_bintable_extension(&b, 16, 5);
    fitsbuf_image_extension_header(&b, 16, 4, 3);
    /* 4 * 3 * 2 = 24 bytes of int16 pixel data, then pad. */
    uint8_t pix[24] = {0};
    fitsbuf_append(&b, pix, sizeof(pix));
    fitsbuf_pad_to_block(&b);

    const char* path = "test_info_streaming.fits";
    fitsbuf_write(&b, path);
    fitsbuf_free(&b);

    TinyFitsHeader header = {0};
    int err = tinyfits_load_header(&header, path);
    CHECK(err == TINYFITS_OK, "header(path) on multi-HDU file succeeds");
    CHECK(header.width == 4, "width from skipped-to image HDU");
    CHECK(header.height == 3, "height from skipped-to image HDU");
    CHECK(header.bitpix == 16, "bitpix from skipped-to image HDU");

    tinyfits_free_header(&header);
    remove(path);
}

static void test_int16_canonical_pair_round_trip(void)
{
    printf("Testing INT16 + bscale=1, bzero=32768 round-trips as UINT16 ...\n");

    int16_t src[] = {-32768, -1, 0, 32767};
    TinyFitsHeader w = {0};
    w.width = 4; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_INT16;
    w.bscale = 1.0;
    w.bzero = 32768.0;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds (INT16 + canonical pair allowed)");

    TinyFitsHeader r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "reload succeeds");
    CHECK(r.pixel_type == TINYFITS_UINT16, "loaded as UINT16 (intent honored)");
    CHECK(r.bscale == 1.0 && r.bzero == 0.0, "post-load struct shows 1.0/0.0");

    /* On-disk INT16 -32768 -> UINT16 0; -1 -> 32767; 0 -> 32768; 32767 -> 65535 */
    uint16_t* px = (uint16_t*)pixels;
    CHECK(px[0] == 0, "px[0] = 0 (INT16_MIN -> UINT16 0)");
    CHECK(px[1] == 32767, "px[1] = 32767 (-1 -> UINT16 32767)");
    CHECK(px[2] == 32768, "px[2] = 32768 (0 -> UINT16 32768)");
    CHECK(px[3] == 65535, "px[3] = 65535 (INT16_MAX -> UINT16 65535)");

    tinyfits_free_buffer(pixels);
    tinyfits_free_buffer(fdata);
    tinyfits_free_header(&r);
}

static void test_keyword_pointer_lifetime(void)
{
    printf("Testing keyword pointer lifetime contract ...\n");

    TinyFitsHeader header = {0};
    tinyfits_set_keyword(&header, "OBJECT", "M31", "Andromeda");

    /* Before any mutation: pointer is valid. */
    const TinyFitsKeyword* obj = tinyfits_get_keyword(&header, "OBJECT");
    CHECK(obj && strcmp(obj->value, "M31") == 0, "initial fetch ok");

    /* Add many unrelated keys, forcing the array to grow past its
     * initial capacity. Re-fetch and verify OBJECT is still findable
     * with the original value intact.
     */
    char keybuf[16];
    for (int i = 0; i < 100; i++)
    {
        snprintf(keybuf, sizeof(keybuf), "K%03d", i);
        int err = tinyfits_append_keyword(&header, keybuf, "v", "");
        CHECK(err == TINYFITS_OK, "add filler keyword");
    }
    obj = tinyfits_get_keyword(&header, "OBJECT");
    CHECK(obj && strcmp(obj->value, "M31") == 0,
          "OBJECT findable + value intact after grow-inducing adds");

    /* set_keyword on the same key: replace value/comment, re-fetch. */
    tinyfits_set_keyword(&header, "OBJECT", "M42", "Orion");
    obj = tinyfits_get_keyword(&header, "OBJECT");
    CHECK(obj && strcmp(obj->value, "M42") == 0, "value updated after replace");
    CHECK(obj && strcmp(obj->comment, "Orion") == 0, "comment updated after replace");

    /* Remove an unrelated key (causes memmove that may shift OBJECT). */
    tinyfits_remove_keyword(&header, "K050");
    obj = tinyfits_get_keyword(&header, "OBJECT");
    CHECK(obj && strcmp(obj->value, "M42") == 0, "OBJECT findable after remove");

    /* Remove the tracked key: get_keyword now returns NULL. */
    tinyfits_remove_keyword(&header, "OBJECT");
    obj = tinyfits_get_keyword(&header, "OBJECT");
    CHECK(obj == NULL, "OBJECT gone after remove");

    tinyfits_free_header(&header);
}

static void test_keyword_case_dispatch(void)
{
    printf("Testing keyword case dispatch ...\n");

    TinyFitsHeader header = {0};
    tinyfits_set_keyword(&header, "OBJECT", "M31", "");

    CHECK(tinyfits_get_keyword(&header, "OBJECT") != NULL, "exact match");
    CHECK(tinyfits_get_keyword(&header, "object") != NULL, "lowercase matches");
    CHECK(tinyfits_get_keyword(&header, "Object") != NULL, "mixed case matches");
    CHECK(tinyfits_get_keyword(&header, "OBJEC")  == NULL, "shorter prefix no match");
    CHECK(tinyfits_get_keyword(&header, "OBJECTS") == NULL, "longer no match");

    tinyfits_free_header(&header);
}

/* add_history / add_comment convenience wrappers. */
static void test_add_history_comment(void)
{
    printf("Testing add_history / add_comment ...\n");

    TinyFitsHeader header = {0};
    int err = tinyfits_add_history(&header, "Calibrated");
    CHECK(err == TINYFITS_OK, "add_history ok");
    err = tinyfits_add_comment(&header, "test image");
    CHECK(err == TINYFITS_OK, "add_comment ok");

    const TinyFitsKeyword* h = tinyfits_get_keyword(&header, "HISTORY");
    const TinyFitsKeyword* c = tinyfits_get_keyword(&header, "COMMENT");
    CHECK(h && strcmp(h->value, "Calibrated") == 0, "HISTORY value");
    CHECK(c && strcmp(c->value, "test image") == 0, "COMMENT value");

    tinyfits_free_header(&header);
}

/* ASCII-printable input validation on set_keyword / add_keyword. */
static void test_keyword_input_validation(void)
{
    printf("Testing keyword ASCII validation ...\n");

    TinyFitsHeader header = {0};
    /* Tab in value: rejected. */
    int err = tinyfits_set_keyword(&header, "OBJECT", "M\t31", "");
    CHECK(err == TINYFITS_ERR_INVALID, "tab in value rejected");

    /* Newline in comment: rejected. */
    err = tinyfits_set_keyword(&header, "OBJECT", "M31", "ok\nbad");
    CHECK(err == TINYFITS_ERR_INVALID, "newline in comment rejected");

    /* High byte (0x80) in value: rejected. */
    unsigned char hi[] = {'a', 0x80, 0};
    err = tinyfits_set_keyword(&header, "OBJECT", (const char*)hi, "");
    CHECK(err == TINYFITS_ERR_INVALID, "high byte rejected");

    /* DEL (0x7F) in value: rejected. */
    char del[] = {'a', 0x7F, 0};
    err = tinyfits_set_keyword(&header, "OBJECT", del, "");
    CHECK(err == TINYFITS_ERR_INVALID, "DEL rejected");

    /* Pure ASCII printable: accepted. */
    err = tinyfits_set_keyword(&header, "OBJECT", "M31 / star", "");
    CHECK(err == TINYFITS_OK, "ASCII printable accepted");

    tinyfits_free_header(&header);
}

/* Construct an 80-byte buffer where the value sits at a non-default
 * column (mimicking a HIERARCH card body) and verify the helper
 * extracts value + comment correctly.
 */
static void test_parse_value_column_agnostic(void)
{
    printf("Testing tinyfits__parse_value at non-default columns ...\n");

    char card[TINYFITS_CARD_SIZE];
    char value[TINYFITS_CARD_VALUE_MAX_LEN + 1];
    char comment[TINYFITS_CARD_VALUE_MAX_LEN + 1];
    const char* card_end = card + TINYFITS_CARD_SIZE;

    /* String value starting at column 22 (post-HIERARCH-key '=' position).
     * Bytes: cols 0..21 spaces, col 22 '\'', "ABC", '\'', " / cmt", padding.
     */
    memset(card, ' ', TINYFITS_CARD_SIZE);
    memcpy(card + 22, "'ABC' / cmt", 11);
    tinyfits__parse_value(card + 22, card_end, value, comment);
    CHECK(strcmp(value, "ABC") == 0, "string at col 22");
    CHECK(strcmp(comment, "cmt") == 0, "comment at col 22");

    /* Numeric value at column 30. */
    memset(card, ' ', TINYFITS_CARD_SIZE);
    memcpy(card + 30, "42 / answer", 11);
    tinyfits__parse_value(card + 30, card_end, value, comment);
    CHECK(strcmp(value, "42") == 0, "numeric at col 30");
    CHECK(strcmp(comment, "answer") == 0, "comment at col 30");

    /* String at default column 10 (regression: no behavior change). */
    memset(card, ' ', TINYFITS_CARD_SIZE);
    memcpy(card + 10, "'HALOGEN'           / Lamp ID", 29);
    tinyfits__parse_value(card + 10, card_end, value, comment);
    CHECK(strcmp(value, "HALOGEN") == 0, "string at col 10");
    CHECK(strcmp(comment, "Lamp ID") == 0, "comment at col 10");

    /* Empty value (vstart >= card_end after space-skip). */
    memset(card, ' ', TINYFITS_CARD_SIZE);
    tinyfits__parse_value(card + 70, card_end, value, comment);
    CHECK(value[0] == '\0', "empty value when only spaces remain");
    CHECK(comment[0] == '\0', "empty comment when only spaces remain");
}

static void test_examples(void)
{
    printf("Testing doc examples ...\n");
    TinyFitsHeader header = {0};
    header.width = 624;
    header.height = 417;
    header.num_channels = 1;
    header.pixel_type = TINYFITS_UINT16;
    header.bscale = 1.0;
 
    void* pixels = malloc(header.width * header.height * sizeof(uint16_t));
 
    // Short keyword, value, empty comment
    tinyfits_set_keyword(&header, "BAYERPAT", "RGGB", "");
 
    // Long string values chain via CONTINUE on save
    tinyfits_set_keyword(&header,
                     "OBSERVER",
                     "Not from the stars do I my judgement pluck; And yet methinks I have Astronomy",
                     "Sonnet 14");

    // Long keys
    tinyfits_set_keyword(&header, "ESO INS LAMP1 ID", "HALOGEN", "Lamp ID");
 
    // HISTORY auto-splits across multiple cards if needed:
    tinyfits_add_history(&header, "Calibrated with master dark and flat");
 
    int err = tinyfits_save(&header, pixels, "example.fits", 0 /* planar */);

    CHECK(err == TINYFITS_OK, "write example file");
    free(pixels);
    tinyfits_free_header(&header);

    err = tinyfits_load_header(&header, "example.fits");
    CHECK(err == TINYFITS_OK, "load example.fits header");
    const TinyFitsKeyword* kw = tinyfits_get_keyword(&header, "BAYERPAT");
    CHECK(kw && kw->value && strcmp(kw->value, "RGGB") == 0, "read header only");
    tinyfits_free_header(&header);

    pixels = NULL;
    err = tinyfits_load(&header, "example.fits", &pixels);
    CHECK(err == TINYFITS_OK, "read example.fits");
    CHECK(header.pixel_type == TINYFITS_UINT16, "read example.fits pixel_type");

}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Basics */
    test_last_error();
    test_null_args();
    test_free_safety();
    test_image_size();
    test_get_header_empty();
    test_parse_value_column_agnostic();
    test_keyword_pointer_lifetime();
    test_keyword_case_dispatch();
    test_add_history_comment();
    test_keyword_input_validation();

    /* Header parsing and header */
    test_info_uint16();
    test_info_float32();
    test_info_missing_bzero();
    test_info_nonstandard_bscale();
    test_info_nonstandard_bzero();
    test_info_invalid_naxis();
    test_info_header_comments();
    test_info_history();
    test_info_struct_reuse();
    test_info_not_fits();
    test_info_uint32();
    test_info_all_pixel_types();

    /* Header mutation and utilities */
    test_set_header();
    test_add_header();
    test_remove_header();
    test_get_headers();
    test_reserved_key_rejection();
    test_header_field_validation();
    test_to_float();

    /* Pixel loading */
    test_load_uint8();
    test_load_int16();
    test_load_uint16();
    test_load_int32();
    test_load_float32();
    test_load_float64();
    test_load_3channel();
    test_load_truncated();
    test_load_null_pixels();

    /* Writing and round-trip */
    test_roundtrip_uint8();
    test_roundtrip_int16();
    test_roundtrip_uint16();
    test_roundtrip_int32();
    test_roundtrip_uint32();
    test_roundtrip_float32();
    test_roundtrip_float64();
    test_roundtrip_3channel();
    test_roundtrip_interleaved();
    test_interleaved_all_types();
    test_roundtrip_headers();
    test_roundtrip_load_modify_save();
    test_mandatory_header_order();
    test_save_errors();
    test_naxis_0_and_gt3();
    test_load_struct_reuse();
    test_to_float_all_types();
    test_to_float_physical_dual_paths();
    test_to_float_normalized_rejects_floats();
    test_save_to_file();
    test_file_io_errors();
    test_roundtrip_mono_naxis2();
    test_max_header_blocks();

    /* Security / robustness */
    test_malicious_inputs();
    test_null_params();
    test_single_quote_roundtrip();
    test_zero_width();
    test_interleaved_single_channel();
    test_failed_load_zeroes_struct();
    test_null_in_value();
    test_continue_basic_merge();
    test_continue_long_chain();
    test_continue_quotepair_at_boundary();
    test_continue_comment_last_wins();
    test_continue_orphan_dropped();
    test_continue_truncated_at_hdu_end();
    test_trailing_amp_roundtrip();
    test_string_value_roundtrip_battery();
    test_hierarch_basic_read();
    test_hierarch_whitespace_canonicalization();
    test_hierarch_with_continue();
    test_hierarch_missing_separator();
    test_hierarch_empty_key();
    test_hierarch_short_no_space_rejected();
    test_hierarch_lookup_normalization();
    test_hierarch_forbidden_chars();
    test_continue_write_long_roundtrip();
    test_continue_write_quotepair_atomicity();
    test_continue_write_comment_truncation();
    test_continue_write_header_cap();
    test_chunk_budget_unit();
    test_hierarch_write_short_value();
    test_hierarch_write_long_key_min_value();
    test_hierarch_write_over_cap();
    test_hierarch_write_long_value_chain();
    test_hierarch_write_numeric_freeform();
    test_history_auto_split();
    test_history_empty_input();
    test_history_non_printable_rejected();
    test_history_non_empty_comment_rejected();
    test_history_set_long_value_rejected();
    test_history_split_roundtrip_byte_faithful();
    test_nonstandard_card_freeform_fallback();
    test_hierarch_namespace_independent_of_reserved();
    test_history_full_payload_not_truncated();

    /* BSCALE/BZERO contract */
    test_nonstandard_scale_roundtrip();
    test_nan_inf_rejection();
    test_bscale_zero();
    test_save_validation_unsigned_with_scale();
    test_save_validation_user_bscale_keyword();
    test_save_validation_long_hierarch_key();
    test_unsigned_classification_boundaries();
    test_bitpix64_rejected();
    test_int16_canonical_pair_round_trip();

    /* Multi-HDU walk */
    test_multi_hdu_image_in_extension();
    test_multi_hdu_no_image_only_table();
    test_multi_hdu_skip_table_to_image();
    test_multi_hdu_skip_empty_image();
    test_multi_hdu_first_image_unsupported_bitpix();
    test_info_file_streaming();

    /* Examples */
    test_examples();

    printf("\n%d / %d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
