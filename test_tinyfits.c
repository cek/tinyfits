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

#define CHECK(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
    } else { \
        tests_passed++; \
    } \
} while (0)

#define CHECK_CLOSE(a, b, tol, msg) do { \
    tests_run++; \
    if (fabs((double)(a) - (double)(b)) > (tol)) { \
        printf("  FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b)); \
    } else { \
        tests_passed++; \
    } \
} while (0)

/* --- Basic declaration and utility tests --- */

static void test_error_strings(void)
{
    printf("Testing error strings ...\n");
    CHECK(strcmp(tinyfits_error_string(TINYFITS_OK), "OK") == 0, "OK string");
    CHECK(strcmp(tinyfits_error_string(TINYFITS_ERR_OPEN), "Could not open file") == 0, "OPEN string");
    CHECK(strcmp(tinyfits_error_string(TINYFITS_ERR_BZERO_BSCALE), "Non-standard BZERO/BSCALE") == 0, "BSCALE string");
    CHECK(strcmp(tinyfits_error_string(TINYFITS_ERR_WRITE), "Write error") == 0, "WRITE string");
    CHECK(strcmp(tinyfits_error_string(-1), "Unknown error") == 0, "negative error");
    CHECK(strcmp(tinyfits_error_string(99), "Unknown error") == 0, "out of range error");
}

static void test_free_safety(void)
{
    printf("Testing free safety ...\n");

    /* Zero-initialized struct */
    TinyFits info = {0};
    tinyfits_free(&info);
    CHECK(info.keywords == NULL, "keywords NULL after free of zero struct");
    CHECK(info.num_keywords == 0, "num_keywords 0 after free of zero struct");
    CHECK(info.pixel_type == TINYFITS_UNKNOWN, "pixel_type UNKNOWN after free");

    /* Double free */
    tinyfits_free(&info);
    CHECK(info.keywords == NULL, "keywords still NULL after double free");

    /* free_buffer with NULL */
    tinyfits_free_buffer(NULL);
    CHECK(1, "free_buffer(NULL) did not crash");
}

static void test_image_size(void)
{
    printf("Testing image_size ...\n");

    TinyFits info = {0};
    CHECK(tinyfits_image_size(&info) == 0, "zero-initialized returns 0");

    info.width = 100;
    info.height = 200;
    info.num_channels = 1;
    info.pixel_type = TINYFITS_UINT16;
    CHECK(tinyfits_image_size(&info) == 100 * 200 * 1 * 2, "uint16 mono");

    info.pixel_type = TINYFITS_FLOAT32;
    CHECK(tinyfits_image_size(&info) == 100 * 200 * 1 * 4, "float32 mono");

    info.num_channels = 3;
    info.pixel_type = TINYFITS_FLOAT64;
    CHECK(tinyfits_image_size(&info) == 100 * 200 * 3 * 8, "float64 rgb");

    info.pixel_type = TINYFITS_UNKNOWN;
    CHECK(tinyfits_image_size(&info) == 0, "unknown type returns 0");
}

static void test_pixel_type_constants(void)
{
    printf("Testing pixel_type constants ...\n");
    CHECK(TINYFITS_UNKNOWN == 0, "UNKNOWN is 0");
    CHECK(TINYFITS_UINT8 == 1, "UINT8 is 1");
    CHECK(TINYFITS_INT16 == 2, "INT16 is 2");
    CHECK(TINYFITS_UINT16 == 3, "UINT16 is 3");
    CHECK(TINYFITS_INT32 == 4, "INT32 is 4");
    CHECK(TINYFITS_UINT32 == 5, "UINT32 is 5");
    CHECK(TINYFITS_FLOAT32 == 6, "FLOAT32 is 6");
    CHECK(TINYFITS_FLOAT64 == 7, "FLOAT64 is 7");
}

static void test_get_header_empty(void)
{
    printf("Testing get_header on empty struct ...\n");
    TinyFits info = {0};
    CHECK(tinyfits_get_keyword(&info, "BITPIX") == NULL, "no keywords returns NULL");
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
    char card[80];
    memset(card, ' ', 80);
    size_t klen = strlen(keyword);
    if (klen > 8) klen = 8;
    memcpy(card, keyword, klen);
    card[8] = '=';
    card[9] = ' ';
    size_t vlen = strlen(value);
    if (vlen > 70) vlen = 70;
    memcpy(card + 10, value, vlen);
    fitsbuf_append(b, card, 80);
}

static void fitsbuf_card_str(FitsBuf* b, const char* keyword, const char* value)
{
    char vbuf[72];
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
    char card[80];
    memset(card, ' ', 80);
    size_t klen = strlen(keyword);
    if (klen > 8) klen = 8;
    memcpy(card, keyword, klen);
    card[8] = '=';
    card[9] = ' ';
    size_t vlen = strlen(value);
    if (vlen > 30) vlen = 30;
    memcpy(card + 10, value, vlen);
    size_t cpos = 10 + vlen + 1;
    if (cpos < 78 && comment && comment[0])
    {
        card[cpos] = '/';
        card[cpos + 1] = ' ';
        size_t clen = strlen(comment);
        if (clen > 80 - cpos - 2) clen = 80 - cpos - 2;
        memcpy(card + cpos + 2, comment, clen);
    }
    fitsbuf_append(b, card, 80);
}

static void fitsbuf_history(FitsBuf* b, const char* text)
{
    char card[80];
    memset(card, ' ', 80);
    memcpy(card, "HISTORY ", 8);
    size_t tlen = strlen(text);
    if (tlen > 72) tlen = 72;
    memcpy(card + 8, text, tlen);
    fitsbuf_append(b, card, 80);
}

static void fitsbuf_end(FitsBuf* b)
{
    char card[80];
    memset(card, ' ', 80);
    memcpy(card, "END", 3);
    fitsbuf_append(b, card, 80);
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

/* --- Header parsing and info tests --- */

static void test_info_uint16(void)
{
    printf("Testing info on uint16 file ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 64, 48);
    fitsbuf_card_float(&b, "BZERO", 32768.0);
    fitsbuf_card_float(&b, "BSCALE", 1.0);
    fitsbuf_card_str(&b, "INSTRUME", "TestCam");
    fitsbuf_end(&b);
    /* No pixel data needed for info */

    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_OK, "info_from_memory succeeds");
    CHECK(info.width == 64, "width");
    CHECK(info.height == 48, "height");
    CHECK(info.num_channels == 1, "num_channels");
    CHECK(info.bitpix == 16, "bitpix");
    CHECK(info.pixel_type == TINYFITS_UINT16, "pixel_type is UINT16");

    /* BZERO/BSCALE should be stripped */
    CHECK(tinyfits_get_keyword(&info, "BZERO") == NULL, "BZERO stripped");
    CHECK(tinyfits_get_keyword(&info, "BSCALE") == NULL, "BSCALE stripped");

    /* Other keywords should be present */
    const char* instrume = tinyfits_get_keyword(&info, "INSTRUME");
    CHECK(instrume != NULL, "INSTRUME present");
    if (instrume) CHECK(strcmp(instrume, "TestCam") == 0, "INSTRUME value");

    tinyfits_free(&info);
    fitsbuf_free(&b);
}

static void test_info_float32(void)
{
    printf("Testing info on float32 file ...\n");

    FitsBuf b;
    fitsbuf_standard_header_3d(&b, -32, 100, 200, 3);
    fitsbuf_end(&b);

    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_OK, "info succeeds");
    CHECK(info.width == 100, "width");
    CHECK(info.height == 200, "height");
    CHECK(info.num_channels == 3, "num_channels");
    CHECK(info.pixel_type == TINYFITS_FLOAT32, "pixel_type is FLOAT32");

    tinyfits_free(&info);
    fitsbuf_free(&b);
}

static void test_info_missing_bzero(void)
{
    printf("Testing info with missing BZERO/BSCALE ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 10, 10);
    fitsbuf_end(&b);

    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_OK, "info succeeds with no BZERO/BSCALE");
    CHECK(info.pixel_type == TINYFITS_INT16, "pixel_type is INT16 (default BZERO=0)");

    tinyfits_free(&info);
    fitsbuf_free(&b);
}

static void test_info_nonstandard_bscale(void)
{
    printf("Testing info with non-standard BSCALE ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 10, 10);
    fitsbuf_card_float(&b, "BSCALE", 0.5);
    fitsbuf_end(&b);

    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_ERR_BZERO_BSCALE, "non-standard BSCALE rejected");

    tinyfits_free(&info);
    fitsbuf_free(&b);
}

static void test_info_nonstandard_bzero(void)
{
    printf("Testing info with non-standard BZERO ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 16, 10, 10);
    fitsbuf_card_float(&b, "BZERO", 100.0);
    fitsbuf_end(&b);

    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_ERR_BZERO_BSCALE, "non-standard BZERO rejected");

    tinyfits_free(&info);
    fitsbuf_free(&b);
}

static void test_info_invalid_naxis(void)
{
    printf("Testing info with NAXIS=1 ...\n");

    FitsBuf b;
    fitsbuf_init(&b);
    fitsbuf_card(&b, "SIMPLE", "                   T");
    fitsbuf_card_int(&b, "BITPIX", 16);
    fitsbuf_card_int(&b, "NAXIS", 1);
    fitsbuf_card_int(&b, "NAXIS1", 100);
    fitsbuf_end(&b);

    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_ERR_INVALID, "NAXIS=1 rejected");

    tinyfits_free(&info);
    fitsbuf_free(&b);
}

static void test_info_header_comments(void)
{
    printf("Testing header comment parsing ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 4, 4);
    fitsbuf_card_with_comment(&b, "EXPTIME", "              180.0", "Exposure time in seconds");
    fitsbuf_end(&b);

    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_OK, "info succeeds");

    /* Find EXPTIME and check comment */
    int found = 0;
    for (int i = 0; i < info.num_keywords; i++)
    {
        if (strcmp(info.keywords[i].key, "EXPTIME") == 0)
        {
            found = 1;
            CHECK(strcmp(info.keywords[i].comment, "Exposure time in seconds") == 0,
                  "comment parsed correctly");
            break;
        }
    }
    CHECK(found, "EXPTIME header found");

    tinyfits_free(&info);
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

    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_OK, "info succeeds");

    /* Count HISTORY entries */
    int count = 0;
    for (int i = 0; i < info.num_keywords; i++)
    {
        if (strcmp(info.keywords[i].key, "HISTORY") == 0)
        {
            if (count == 0)
                CHECK(strcmp(info.keywords[i].value, "Calibrated with master dark") == 0,
                      "first HISTORY value");
            if (count == 1)
                CHECK(strcmp(info.keywords[i].value, "Stacked 54 frames") == 0,
                      "second HISTORY value");
            CHECK(info.keywords[i].comment[0] == '\0', "HISTORY comment is empty");
            count++;
        }
    }
    CHECK(count == 2, "two HISTORY entries");

    tinyfits_free(&info);
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

    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_OK, "first info succeeds");
    CHECK(info.width == 4, "first width");

    /* Reuse -- must free first to avoid leaking keywords */
    tinyfits_free(&info);
    err = tinyfits_info_from_memory(&info, b2.data, b2.size);
    CHECK(err == TINYFITS_OK, "second info succeeds");
    CHECK(info.width == 8, "second width");
    CHECK(info.pixel_type == TINYFITS_FLOAT32, "second pixel_type");

    const char* obj = tinyfits_get_keyword(&info, "OBJECT");
    CHECK(obj != NULL && strcmp(obj, "Second") == 0, "second OBJECT value");

    tinyfits_free(&info);
    fitsbuf_free(&b);
    fitsbuf_free(&b2);
}

static void test_info_not_fits(void)
{
    printf("Testing info on non-FITS data ...\n");

    const char* garbage = "This is not a FITS file at all";
    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, garbage, strlen(garbage));
    CHECK(err == TINYFITS_ERR_NOT_FITS, "non-FITS rejected");
}

static void test_info_uint32(void)
{
    printf("Testing info on uint32 (BZERO=2147483648) ...\n");

    FitsBuf b;
    fitsbuf_standard_header(&b, 32, 10, 10);
    fitsbuf_card_float(&b, "BZERO", 2147483648.0);
    fitsbuf_card_float(&b, "BSCALE", 1.0);
    fitsbuf_end(&b);

    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_OK, "info succeeds");
    CHECK(info.pixel_type == TINYFITS_UINT32, "pixel_type is UINT32");

    tinyfits_free(&info);
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

        TinyFits info = {0};
        int err = tinyfits_info_from_memory(&info, b.data, b.size);
        CHECK(err == TINYFITS_OK, "info succeeds");
        CHECK(info.pixel_type == cases[t].expected, "pixel_type matches");

        tinyfits_free(&info);
        fitsbuf_free(&b);
    }
}

/* --- Header mutation and utility tests --- */

static void test_set_header(void)
{
    printf("Testing set_header ...\n");

    TinyFits info = {0};

    int err = tinyfits_set_keyword(&info, "OBJECT", "M31", "Andromeda Galaxy");
    CHECK(err == TINYFITS_OK, "set new header");
    CHECK(info.num_keywords == 1, "one header");

    const char* val = tinyfits_get_keyword(&info, "OBJECT");
    CHECK(val != NULL && strcmp(val, "M31") == 0, "value is M31");
    CHECK(strcmp(info.keywords[0].comment, "Andromeda Galaxy") == 0, "comment set");

    /* Replace existing */
    err = tinyfits_set_keyword(&info, "OBJECT", "M42", "Orion Nebula");
    CHECK(err == TINYFITS_OK, "replace succeeds");
    CHECK(info.num_keywords == 1, "still one header");
    val = tinyfits_get_keyword(&info, "OBJECT");
    CHECK(val != NULL && strcmp(val, "M42") == 0, "value replaced");

    /* Add another */
    err = tinyfits_set_keyword(&info, "FILTER", "Ha", "");
    CHECK(err == TINYFITS_OK, "add second header");
    CHECK(info.num_keywords == 2, "two keywords");

    tinyfits_free(&info);
}

static void test_add_header(void)
{
    printf("Testing add_header ...\n");

    TinyFits info = {0};

    tinyfits_add_keyword(&info, "HISTORY", "Step 1: calibrate", "");
    tinyfits_add_keyword(&info, "HISTORY", "Step 2: stack", "");
    tinyfits_add_keyword(&info, "HISTORY", "Step 3: stretch", "");

    CHECK(info.num_keywords == 3, "three HISTORY entries");

    /* All three should be retrievable */
    int count = 0;
    for (int i = 0; i < info.num_keywords; i++)
    {
        if (strcmp(info.keywords[i].key, "HISTORY") == 0)
            count++;
    }
    CHECK(count == 3, "three HISTORY keys found");

    tinyfits_free(&info);
}

static void test_remove_header(void)
{
    printf("Testing remove_header ...\n");

    TinyFits info = {0};
    tinyfits_set_keyword(&info, "AAA", "1", "");
    tinyfits_set_keyword(&info, "BBB", "2", "");
    tinyfits_set_keyword(&info, "CCC", "3", "");
    CHECK(info.num_keywords == 3, "three keywords");

    tinyfits_remove_keyword(&info, "BBB");
    CHECK(info.num_keywords == 2, "two keywords after remove");
    CHECK(tinyfits_get_keyword(&info, "BBB") == NULL, "BBB removed");
    CHECK(tinyfits_get_keyword(&info, "AAA") != NULL, "AAA still present");
    CHECK(tinyfits_get_keyword(&info, "CCC") != NULL, "CCC still present");

    /* Remove nonexistent -- no-op */
    tinyfits_remove_keyword(&info, "ZZZ");
    CHECK(info.num_keywords == 2, "still two keywords");

    tinyfits_free(&info);
}

static void test_get_headers(void)
{
    printf("Testing get_headers ...\n");

    TinyFits info = {0};
    tinyfits_add_keyword(&info, "HISTORY", "first", "");
    tinyfits_add_keyword(&info, "OBJECT", "M31", "");
    tinyfits_add_keyword(&info, "HISTORY", "second", "");
    tinyfits_add_keyword(&info, "HISTORY", "third", "");

    /* Count only */
    int count = tinyfits_get_keywords(&info, "HISTORY", NULL, 0);
    CHECK(count == 3, "3 HISTORY entries");

    /* Retrieve */
    const char* vals[4];
    int n = tinyfits_get_keywords(&info, "HISTORY", vals, 4);
    CHECK(n == 3, "returns 3");
    CHECK(strcmp(vals[0], "first") == 0, "first value");
    CHECK(strcmp(vals[1], "second") == 0, "second value");
    CHECK(strcmp(vals[2], "third") == 0, "third value");

    /* Partial retrieve */
    const char* vals2[2];
    n = tinyfits_get_keywords(&info, "HISTORY", vals2, 2);
    CHECK(n == 3, "total count is 3 even with max_values=2");
    CHECK(strcmp(vals2[0], "first") == 0, "partial first");
    CHECK(strcmp(vals2[1], "second") == 0, "partial second");

    /* No matches */
    n = tinyfits_get_keywords(&info, "MISSING", NULL, 0);
    CHECK(n == 0, "0 for missing key");

    tinyfits_free(&info);
}

static void test_reserved_key_rejection(void)
{
    printf("Testing reserved key rejection ...\n");

    TinyFits info = {0};

    CHECK(tinyfits_set_keyword(&info, "SIMPLE", "T", "") == TINYFITS_ERR_INVALID, "SIMPLE rejected");
    CHECK(tinyfits_set_keyword(&info, "BITPIX", "16", "") == TINYFITS_ERR_INVALID, "BITPIX rejected");
    CHECK(tinyfits_set_keyword(&info, "NAXIS", "2", "") == TINYFITS_ERR_INVALID, "NAXIS rejected");
    CHECK(tinyfits_set_keyword(&info, "NAXIS1", "100", "") == TINYFITS_ERR_INVALID, "NAXIS1 rejected");
    CHECK(tinyfits_set_keyword(&info, "NAXIS3", "3", "") == TINYFITS_ERR_INVALID, "NAXIS3 rejected");
    CHECK(tinyfits_set_keyword(&info, "BZERO", "32768", "") == TINYFITS_ERR_INVALID, "BZERO rejected");
    CHECK(tinyfits_set_keyword(&info, "BSCALE", "1", "") == TINYFITS_ERR_INVALID, "BSCALE rejected");
    CHECK(tinyfits_set_keyword(&info, "EXTEND", "T", "") == TINYFITS_ERR_INVALID, "EXTEND rejected");
    CHECK(tinyfits_set_keyword(&info, "END", "", "") == TINYFITS_ERR_INVALID, "END rejected");

    /* add_header should also reject */
    CHECK(tinyfits_add_keyword(&info, "BZERO", "0", "") == TINYFITS_ERR_INVALID, "add BZERO rejected");

    /* Non-reserved should succeed */
    CHECK(tinyfits_set_keyword(&info, "OBJECT", "M31", "") == TINYFITS_OK, "OBJECT ok");
    CHECK(tinyfits_set_keyword(&info, "HISTORY", "test", "") == TINYFITS_OK, "HISTORY ok via set");
    CHECK(tinyfits_add_keyword(&info, "COMMENT", "test", "") == TINYFITS_OK, "COMMENT ok via add");

    CHECK(info.num_keywords == 3, "three non-reserved keywords");

    tinyfits_free(&info);
}

static void test_header_field_validation(void)
{
    printf("Testing header field length validation ...\n");

    TinyFits info = {0};

    /* Key too long */
    CHECK(tinyfits_set_keyword(&info, "TOOLONGKEY", "v", "") == TINYFITS_ERR_INVALID,
          "key > 8 rejected");

    /* Value too long (72 chars) */
    char longval[73];
    memset(longval, 'x', 72);
    longval[72] = '\0';
    CHECK(tinyfits_set_keyword(&info, "TEST", longval, "") == TINYFITS_ERR_INVALID,
          "value > 71 rejected");

    /* Comment too long */
    char longcmt[73];
    memset(longcmt, 'y', 72);
    longcmt[72] = '\0';
    CHECK(tinyfits_set_keyword(&info, "TEST", "v", longcmt) == TINYFITS_ERR_INVALID,
          "comment > 71 rejected");

    /* Exactly 71 chars should be OK */
    char ok71[72];
    memset(ok71, 'z', 71);
    ok71[71] = '\0';
    CHECK(tinyfits_set_keyword(&info, "TEST", ok71, "") == TINYFITS_OK,
          "value == 71 accepted");

    tinyfits_free(&info);
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

    TinyFits info = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&info, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    float out[3];
    err = tinyfits_to_float(&info, pixels, out);
    CHECK(err == TINYFITS_OK, "to_float succeeds");
    CHECK_CLOSE(out[0], 0.0f, 0.01f, "float[0] = 0");
    CHECK_CLOSE(out[1], 0.5f, 0.01f, "float[1] = 0.5");
    CHECK_CLOSE(out[2], 1.0f, 0.01f, "float[2] = 1.0");

    /* Float32 identity */
    TinyFits info2 = {0};
    info2.width = 2;
    info2.height = 1;
    info2.num_channels = 1;
    info2.pixel_type = TINYFITS_FLOAT32;
    float fpx[] = {1.5f, -3.0f};
    float fout[2];
    err = tinyfits_to_float(&info2, fpx, fout);
    CHECK(err == TINYFITS_OK, "float32 identity succeeds");
    CHECK_CLOSE(fout[0], 1.5f, 1e-6f, "float32 identity [0]");
    CHECK_CLOSE(fout[1], -3.0f, 1e-6f, "float32 identity [1]");

    /* Error case: UNKNOWN pixel_type */
    TinyFits info3 = {0};
    info3.width = 1;
    info3.height = 1;
    info3.num_channels = 1;
    err = tinyfits_to_float(&info3, fpx, fout);
    CHECK(err == TINYFITS_ERR_INVALID, "UNKNOWN pixel_type rejected");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&info);
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

    TinyFits info = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&info, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(info.pixel_type == TINYFITS_UINT8, "pixel_type");
    CHECK(info.width == 4 && info.height == 2, "dimensions");

    uint8_t* px = (uint8_t*)pixels;
    CHECK(px[0] == 10 && px[3] == 40 && px[7] == 80, "pixel values");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&info);
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

    TinyFits info = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&info, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(info.pixel_type == TINYFITS_INT16, "pixel_type");

    int16_t* px = (int16_t*)pixels;
    CHECK(px[0] == -100, "px[0]");
    CHECK(px[1] == 0, "px[1]");
    CHECK(px[2] == 100, "px[2]");
    CHECK(px[3] == 32767, "px[3]");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&info);
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
     * stored=32767 -> physical=65535 */
    uint8_t pixdata[8];
    write_be16(pixdata + 0, -32768); /* physical 0 */
    write_be16(pixdata + 2, -1);     /* physical 32767 */
    write_be16(pixdata + 4, 0);      /* physical 32768 */
    write_be16(pixdata + 6, 32767);  /* physical 65535 */
    fitsbuf_append(&b, pixdata, sizeof(pixdata));
    fitsbuf_pad_to_block(&b);

    TinyFits info = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&info, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(info.pixel_type == TINYFITS_UINT16, "pixel_type");

    uint16_t* px = (uint16_t*)pixels;
    CHECK(px[0] == 0, "physical 0");
    CHECK(px[1] == 32767, "physical 32767");
    CHECK(px[2] == 32768, "physical 32768");
    CHECK(px[3] == 65535, "physical 65535");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&info);
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

    TinyFits info = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&info, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(info.pixel_type == TINYFITS_INT32, "pixel_type");

    int32_t* px = (int32_t*)pixels;
    CHECK(px[0] == -1000000, "px[0]");
    CHECK(px[1] == 1000000, "px[1]");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&info);
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

    TinyFits info = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&info, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(info.pixel_type == TINYFITS_FLOAT32, "pixel_type");

    float* px = (float*)pixels;
    CHECK_CLOSE(px[0], 0.0f, 1e-6, "px[0]");
    CHECK_CLOSE(px[1], 1.5f, 1e-6, "px[1]");
    CHECK_CLOSE(px[2], -42.25f, 1e-6, "px[2]");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&info);
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

    TinyFits info = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&info, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(info.pixel_type == TINYFITS_FLOAT64, "pixel_type");

    double* px = (double*)pixels;
    CHECK_CLOSE(px[0], 3.141592653589793, 1e-15, "px[0]");
    CHECK_CLOSE(px[1], -1.0e-15, 1e-30, "px[1]");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&info);
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

    TinyFits info = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&info, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(info.num_channels == 3, "3 channels");

    int16_t* px = (int16_t*)pixels;
    int plane = 2 * 2;
    CHECK(px[0 * plane + 0] == 10, "R[0]");
    CHECK(px[0 * plane + 3] == 40, "R[3]");
    CHECK(px[1 * plane + 0] == 110, "G[0]");
    CHECK(px[2 * plane + 3] == 240, "B[3]");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&info);
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

    TinyFits info = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&info, b.data, b.size, &pixels);
    CHECK(err == TINYFITS_ERR_READ, "truncated file rejected");
    CHECK(pixels == NULL, "pixels is NULL on failure");

    tinyfits_free(&info);
    fitsbuf_free(&b);
}

static void test_load_null_pixels(void)
{
    printf("Testing load with NULL pixels pointer ...\n");

    TinyFits info = {0};
    int err = tinyfits_load_from_memory(&info, "x", 1, NULL);
    CHECK(err == TINYFITS_ERR_INVALID, "NULL pixels rejected");
}

/* --- Writing and round-trip tests --- */

static void test_roundtrip_uint8(void)
{
    printf("Testing roundtrip uint8 ...\n");
    uint8_t src[] = {0, 128, 255, 42};
    TinyFits w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT8;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(r.pixel_type == TINYFITS_UINT8, "pixel_type");

    uint8_t* px = (uint8_t*)pixels;
    CHECK(px[0] == 0 && px[1] == 128 && px[2] == 255 && px[3] == 42, "values match");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_int16(void)
{
    printf("Testing roundtrip int16 ...\n");
    int16_t src[] = {-32768, -1, 0, 32767};
    TinyFits w = {0};
    w.width = 4; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_INT16;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    int16_t* px = (int16_t*)pixels;
    CHECK(px[0] == -32768 && px[1] == -1 && px[2] == 0 && px[3] == 32767, "values match");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_uint16(void)
{
    printf("Testing roundtrip uint16 ...\n");
    uint16_t src[] = {0, 1000, 32768, 65535};
    TinyFits w = {0};
    w.width = 4; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT16;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(r.pixel_type == TINYFITS_UINT16, "pixel_type");

    uint16_t* px = (uint16_t*)pixels;
    CHECK(px[0] == 0 && px[1] == 1000 && px[2] == 32768 && px[3] == 65535, "values match");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_int32(void)
{
    printf("Testing roundtrip int32 ...\n");
    int32_t src[] = {-2000000, 0, 2000000};
    TinyFits w = {0};
    w.width = 3; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_INT32;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    int32_t* px = (int32_t*)pixels;
    CHECK(px[0] == -2000000 && px[1] == 0 && px[2] == 2000000, "values match");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_uint32(void)
{
    printf("Testing roundtrip uint32 ...\n");
    uint32_t src[] = {0, 2147483648u, 4294967295u};
    TinyFits w = {0};
    w.width = 3; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT32;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(r.pixel_type == TINYFITS_UINT32, "pixel_type");

    uint32_t* px = (uint32_t*)pixels;
    CHECK(px[0] == 0 && px[1] == 2147483648u && px[2] == 4294967295u, "values match");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_float32(void)
{
    printf("Testing roundtrip float32 ...\n");
    float src[] = {0.0f, 1.5f, -42.25f, 1e10f};
    TinyFits w = {0};
    w.width = 4; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_FLOAT32;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    float* px = (float*)pixels;
    CHECK_CLOSE(px[0], 0.0f, 1e-10, "px[0]");
    CHECK_CLOSE(px[1], 1.5f, 1e-10, "px[1]");
    CHECK_CLOSE(px[2], -42.25f, 1e-10, "px[2]");
    CHECK_CLOSE(px[3], 1e10f, 1.0f, "px[3]");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_float64(void)
{
    printf("Testing roundtrip float64 ...\n");
    double src[] = {3.141592653589793, -1.0e-15};
    TinyFits w = {0};
    w.width = 2; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_FLOAT64;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    double* px = (double*)pixels;
    CHECK_CLOSE(px[0], 3.141592653589793, 1e-15, "px[0]");
    CHECK_CLOSE(px[1], -1.0e-15, 1e-30, "px[1]");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
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
    TinyFits w = {0};
    w.width = 2; w.height = 2; w.num_channels = 3;
    w.pixel_type = TINYFITS_UINT16;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(r.num_channels == 3, "3 channels");

    uint16_t* px = (uint16_t*)pixels;
    CHECK(memcmp(px, src, sizeof(src)) == 0, "pixel-exact match");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
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

    TinyFits w = {0};
    w.width = 2; w.height = 2; w.num_channels = 3;
    w.pixel_type = TINYFITS_UINT16;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, interleaved, &fdata, &fsize, 1);
    CHECK(err == TINYFITS_OK, "save interleaved succeeds");

    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    uint16_t* px = (uint16_t*)pixels;
    CHECK(memcmp(px, expected, sizeof(expected)) == 0, "deinterleaved correctly");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
    tinyfits_free_buffer(fdata);
}

static void test_roundtrip_headers(void)
{
    printf("Testing roundtrip header preservation ...\n");

    TinyFits w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT8;
    tinyfits_set_keyword(&w, "OBJECT", "M31", "Andromeda");
    tinyfits_set_keyword(&w, "EXPTIME", "180.0", "seconds");
    tinyfits_add_keyword(&w, "HISTORY", "Calibrated", "");
    tinyfits_add_keyword(&w, "HISTORY", "Stacked", "");

    uint8_t src[] = {1, 2, 3, 4};
    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    const char* obj = tinyfits_get_keyword(&r, "OBJECT");
    CHECK(obj != NULL && strcmp(obj, "M31") == 0, "OBJECT preserved");

    const char* exp = tinyfits_get_keyword(&r, "EXPTIME");
    CHECK(exp != NULL && strcmp(exp, "180.0") == 0, "EXPTIME preserved");

    int hist_count = tinyfits_get_keywords(&r, "HISTORY", NULL, 0);
    CHECK(hist_count == 2, "two HISTORY entries preserved");

    /* Verify order */
    const char* hist[2];
    tinyfits_get_keywords(&r, "HISTORY", hist, 2);
    CHECK(strcmp(hist[0], "Calibrated") == 0, "first HISTORY");
    CHECK(strcmp(hist[1], "Stacked") == 0, "second HISTORY");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
    tinyfits_free_buffer(fdata);
    tinyfits_free(&w);
}

static void test_roundtrip_load_modify_save(void)
{
    printf("Testing load-modify-save with HISTORY ...\n");

    /* Create an initial file with one HISTORY entry */
    uint8_t src[] = {10, 20, 30, 40};
    TinyFits w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT8;
    tinyfits_set_keyword(&w, "OBJECT", "M31", "");
    tinyfits_add_keyword(&w, "HISTORY", "Original", "");

    void* fdata1; size_t fsize1;
    tinyfits_save_to_memory(&w, src, &fdata1, &fsize1, 0);
    tinyfits_free(&w);

    /* Load, add another HISTORY, save again */
    TinyFits r = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&r, fdata1, fsize1, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    tinyfits_add_keyword(&r, "HISTORY", "Reprocessed", "");

    void* fdata2; size_t fsize2;
    err = tinyfits_save_to_memory(&r, pixels, &fdata2, &fsize2, 0);
    CHECK(err == TINYFITS_OK, "re-save succeeds");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
    tinyfits_free_buffer(fdata1);

    /* Load final file and verify */
    TinyFits r2 = {0};
    void* pixels2;
    err = tinyfits_load_from_memory(&r2, fdata2, fsize2, &pixels2);
    CHECK(err == TINYFITS_OK, "final load succeeds");

    const char* obj = tinyfits_get_keyword(&r2, "OBJECT");
    CHECK(obj != NULL && strcmp(obj, "M31") == 0, "OBJECT preserved");

    int hist_count = tinyfits_get_keywords(&r2, "HISTORY", NULL, 0);
    CHECK(hist_count == 2, "two HISTORY entries");

    const char* hist[2];
    tinyfits_get_keywords(&r2, "HISTORY", hist, 2);
    CHECK(strcmp(hist[0], "Original") == 0, "first HISTORY preserved");
    CHECK(strcmp(hist[1], "Reprocessed") == 0, "appended HISTORY present");

    tinyfits_free_buffer(pixels2);
    tinyfits_free(&r2);
    tinyfits_free_buffer(fdata2);
}

static void test_mandatory_header_order(void)
{
    printf("Testing mandatory header order in output ...\n");

    uint8_t src[] = {1, 2, 3, 4};
    TinyFits w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT16;
    tinyfits_set_keyword(&w, "OBJECT", "Test", "");

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    /* Parse the raw header cards to verify order */
    const char* hdr = (const char*)fdata;
    char key0[9], key1[9], key2[9], key3[9], key4[9];
    memcpy(key0, hdr + 0*80, 8); key0[8] = '\0';
    memcpy(key1, hdr + 1*80, 8); key1[8] = '\0';
    memcpy(key2, hdr + 2*80, 8); key2[8] = '\0';
    memcpy(key3, hdr + 3*80, 8); key3[8] = '\0';
    memcpy(key4, hdr + 4*80, 8); key4[8] = '\0';

    /* Trim trailing spaces */
    for (int i = 7; i >= 0; i--) { if (key0[i] == ' ') key0[i] = '\0'; else break; }
    for (int i = 7; i >= 0; i--) { if (key1[i] == ' ') key1[i] = '\0'; else break; }
    for (int i = 7; i >= 0; i--) { if (key2[i] == ' ') key2[i] = '\0'; else break; }
    for (int i = 7; i >= 0; i--) { if (key3[i] == ' ') key3[i] = '\0'; else break; }
    for (int i = 7; i >= 0; i--) { if (key4[i] == ' ') key4[i] = '\0'; else break; }

    CHECK(strcmp(key0, "SIMPLE") == 0, "card 0 is SIMPLE");
    CHECK(strcmp(key1, "BITPIX") == 0, "card 1 is BITPIX");
    CHECK(strcmp(key2, "NAXIS") == 0, "card 2 is NAXIS");
    CHECK(strcmp(key3, "NAXIS1") == 0, "card 3 is NAXIS1");
    CHECK(strcmp(key4, "NAXIS2") == 0, "card 4 is NAXIS2");

    /* Verify no duplicate SIMPLE/BITPIX/NAXIS in the rest of the header */
    int simple_count = 0;
    int bitpix_count = 0;
    for (int i = 0; i < (int)(fsize / 80); i++)
    {
        if (memcmp(hdr + i * 80, "SIMPLE  ", 8) == 0) simple_count++;
        if (memcmp(hdr + i * 80, "BITPIX  ", 8) == 0) bitpix_count++;
        if (memcmp(hdr + i * 80, "END     ", 8) == 0) break;
    }
    CHECK(simple_count == 1, "SIMPLE appears exactly once");
    CHECK(bitpix_count == 1, "BITPIX appears exactly once");

    tinyfits_free(&w);
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

    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_ERR_INVALID, "NAXIS=0 rejected");
    tinyfits_free(&info);
    fitsbuf_free(&b);

    /* NAXIS=4 */
    fitsbuf_init(&b);
    fitsbuf_card(&b, "SIMPLE", "                   T");
    fitsbuf_card_int(&b, "BITPIX", 16);
    fitsbuf_card_int(&b, "NAXIS", 4);
    fitsbuf_card_int(&b, "NAXIS1", 10);
    fitsbuf_card_int(&b, "NAXIS2", 10);
    fitsbuf_card_int(&b, "NAXIS3", 3);
    fitsbuf_card_int(&b, "NAXIS4", 2);
    fitsbuf_end(&b);

    err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_ERR_INVALID, "NAXIS=4 rejected");
    tinyfits_free(&info);
    fitsbuf_free(&b);
}

static void test_load_struct_reuse(void)
{
    printf("Testing load struct reuse ...\n");

    /* Create two different files */
    uint8_t src1[] = {10, 20, 30, 40};
    TinyFits w1 = {0};
    w1.width = 2; w1.height = 2; w1.num_channels = 1;
    w1.pixel_type = TINYFITS_UINT8;
    tinyfits_set_keyword(&w1, "OBJECT", "First", "");
    void* fdata1; size_t fsize1;
    tinyfits_save_to_memory(&w1, src1, &fdata1, &fsize1, 0);
    tinyfits_free(&w1);

    int16_t src2[] = {-1, -2, -3, -4};
    TinyFits w2 = {0};
    w2.width = 2; w2.height = 2; w2.num_channels = 1;
    w2.pixel_type = TINYFITS_INT16;
    tinyfits_set_keyword(&w2, "OBJECT", "Second", "");
    void* fdata2; size_t fsize2;
    tinyfits_save_to_memory(&w2, src2, &fdata2, &fsize2, 0);
    tinyfits_free(&w2);

    /* Load first */
    TinyFits info = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&info, fdata1, fsize1, &pixels);
    CHECK(err == TINYFITS_OK, "first load succeeds");
    CHECK(info.pixel_type == TINYFITS_UINT8, "first pixel_type");
    tinyfits_free_buffer(pixels);

    /* Load second into same struct (must free first) */
    tinyfits_free(&info);
    err = tinyfits_load_from_memory(&info, fdata2, fsize2, &pixels);
    CHECK(err == TINYFITS_OK, "second load succeeds");
    CHECK(info.pixel_type == TINYFITS_INT16, "second pixel_type");

    const char* obj = tinyfits_get_keyword(&info, "OBJECT");
    CHECK(obj != NULL && strcmp(obj, "Second") == 0, "second OBJECT");

    int16_t* px = (int16_t*)pixels;
    CHECK(px[0] == -1 && px[3] == -4, "second pixel values");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&info);
    tinyfits_free_buffer(fdata1);
    tinyfits_free_buffer(fdata2);
}

static void test_to_float_all_types(void)
{
    printf("Testing to_float for all pixel types ...\n");

    /* UINT8 */
    {
        TinyFits i = {0};
        i.width = 2; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_UINT8;
        uint8_t src[] = {0, 255};
        float out[2];
        CHECK(tinyfits_to_float(&i, src, out) == TINYFITS_OK, "uint8 ok");
        CHECK_CLOSE(out[0], 0.0f, 0.01f, "uint8 [0]");
        CHECK_CLOSE(out[1], 1.0f, 0.01f, "uint8 [1]");
    }
    /* INT16 */
    {
        TinyFits i = {0};
        i.width = 2; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_INT16;
        int16_t src[] = {-32768, 32767};
        float out[2];
        CHECK(tinyfits_to_float(&i, src, out) == TINYFITS_OK, "int16 ok");
        CHECK_CLOSE(out[0], -32768.0f / 32767.0f, 0.01f, "int16 [0]");
        CHECK_CLOSE(out[1], 1.0f, 0.01f, "int16 [1]");
    }
    /* UINT16 */
    {
        TinyFits i = {0};
        i.width = 2; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_UINT16;
        uint16_t src[] = {0, 65535};
        float out[2];
        CHECK(tinyfits_to_float(&i, src, out) == TINYFITS_OK, "uint16 ok");
        CHECK_CLOSE(out[0], 0.0f, 0.01f, "uint16 [0]");
        CHECK_CLOSE(out[1], 1.0f, 0.01f, "uint16 [1]");
    }
    /* INT32 */
    {
        TinyFits i = {0};
        i.width = 2; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_INT32;
        int32_t src[] = {-1000000, 1000000};
        float out[2];
        CHECK(tinyfits_to_float(&i, src, out) == TINYFITS_OK, "int32 ok");
        CHECK_CLOSE(out[0], -1000000.0f / 2147483647.0f, 1e-6f, "int32 [0]");
        CHECK_CLOSE(out[1], 1000000.0f / 2147483647.0f, 1e-6f, "int32 [1]");
    }
    /* UINT32 */
    {
        TinyFits i = {0};
        i.width = 1; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_UINT32;
        uint32_t src[] = {1000000};
        float out[1];
        CHECK(tinyfits_to_float(&i, src, out) == TINYFITS_OK, "uint32 ok");
        CHECK_CLOSE(out[0], 1000000.0f / 4294967295.0f, 1e-6f, "uint32 [0]");
    }
    /* FLOAT32 (identity) already tested */
    /* FLOAT64 */
    {
        TinyFits i = {0};
        i.width = 1; i.height = 1; i.num_channels = 1;
        i.pixel_type = TINYFITS_FLOAT64;
        double src[] = {3.14159265358979};
        float out[1];
        CHECK(tinyfits_to_float(&i, src, out) == TINYFITS_OK, "float64 ok");
        CHECK_CLOSE(out[0], 3.14159f, 1e-4f, "float64 [0]");
    }
}

static void test_save_to_file(void)
{
    printf("Testing save to file ...\n");

    uint16_t src[] = {100, 200, 300, 400};
    TinyFits w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT16;
    tinyfits_set_keyword(&w, "OBJECT", "FileTest", "");

    int err = tinyfits_save(&w, src, "test_save_output.fits", 0);
    CHECK(err == TINYFITS_OK, "save to file succeeds");

    /* Load it back */
    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load(&r, "test_save_output.fits", &pixels);
    CHECK(err == TINYFITS_OK, "load from file succeeds");
    CHECK(r.pixel_type == TINYFITS_UINT16, "pixel_type");
    CHECK(r.width == 2 && r.height == 2, "dimensions");

    uint16_t* px = (uint16_t*)pixels;
    CHECK(px[0] == 100 && px[1] == 200 && px[2] == 300 && px[3] == 400,
          "pixel values match");

    const char* obj = tinyfits_get_keyword(&r, "OBJECT");
    CHECK(obj != NULL && strcmp(obj, "FileTest") == 0, "header preserved");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
    tinyfits_free(&w);
    remove("test_save_output.fits");
}

static void test_roundtrip_mono_naxis2(void)
{
    printf("Testing monochrome roundtrip writes NAXIS=2 ...\n");

    float src[] = {1.0f, 2.0f, 3.0f, 4.0f};
    TinyFits w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_FLOAT32;

    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    /* Verify NAXIS=2 in the raw header (no NAXIS3) */
    const char* hdr = (const char*)fdata;
    int found_naxis3 = 0;
    for (int i = 0; i < (int)(fsize / 80); i++)
    {
        if (memcmp(hdr + i * 80, "NAXIS3  ", 8) == 0) found_naxis3 = 1;
        if (memcmp(hdr + i * 80, "END     ", 8) == 0) break;
    }
    CHECK(!found_naxis3, "no NAXIS3 card for mono image");

    /* Verify round-trip */
    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");
    CHECK(r.num_channels == 1, "1 channel");

    float* px = (float*)pixels;
    CHECK_CLOSE(px[0], 1.0f, 1e-6f, "px[0]");
    CHECK_CLOSE(px[3], 4.0f, 1e-6f, "px[3]");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
    tinyfits_free_buffer(fdata);
}

static void test_max_header_blocks(void)
{
    printf("Testing max header blocks ...\n");

    /* Build a FITS file with 1025 header blocks (exceeds 1024 limit) */
    FitsBuf b;
    fitsbuf_standard_header(&b, 8, 4, 4);
    /* Pad rest of block 0 with blanks */
    while (b.size < TINYFITS_BLOCK_SIZE)
    {
        char blank[80];
        memset(blank, ' ', 80);
        fitsbuf_append(&b, blank, 80);
    }
    /* Blocks 1-1023: all blank cards */
    for (int i = 1; i < 1024; i++)
    {
        char block[TINYFITS_BLOCK_SIZE];
        memset(block, ' ', TINYFITS_BLOCK_SIZE);
        fitsbuf_append(&b, block, TINYFITS_BLOCK_SIZE);
    }
    /* Block 1024 (the 1025th): END card */
    fitsbuf_end(&b);
    uint8_t pixels[16] = {0};
    fitsbuf_append(&b, pixels, 16);
    fitsbuf_pad_to_block(&b);

    TinyFits info = {0};
    void* px;
    int err = tinyfits_load_from_memory(&info, b.data, b.size, &px);
    CHECK(err == TINYFITS_ERR_INVALID, "1025 blocks rejected on load");

    err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_ERR_INVALID, "1025 blocks rejected on info");

    tinyfits_free(&info);
    fitsbuf_free(&b);
}

static void test_save_errors(void)
{
    printf("Testing save error cases ...\n");

    TinyFits w = {0};
    uint8_t dummy = 0;
    void* fdata; size_t fsize;

    /* Zero dimensions */
    w.pixel_type = TINYFITS_UINT8;
    int err = tinyfits_save_to_memory(&w, &dummy, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_INVALID, "zero dimensions rejected");

    /* Unknown pixel_type */
    w.width = 1; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_UNKNOWN;
    err = tinyfits_save_to_memory(&w, &dummy, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_INVALID, "unknown pixel_type rejected");

    /* NULL pixels */
    w.pixel_type = TINYFITS_UINT8;
    err = tinyfits_save_to_memory(&w, NULL, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_ERR_INVALID, "NULL pixels rejected");

    /* NULL out_data */
    err = tinyfits_save_to_memory(&w, &dummy, NULL, &fsize, 0);
    CHECK(err == TINYFITS_ERR_INVALID, "NULL out_data rejected");
}

static void test_malicious_inputs(void)
{
    printf("Testing malicious/corrupted inputs ...\n");

    /* Empty buffer */
    {
        TinyFits info = {0};
        int err = tinyfits_info_from_memory(&info, "", 0);
        CHECK(err == TINYFITS_ERR_NOT_FITS, "empty buffer rejected");
    }

    /* Buffer smaller than one block */
    {
        TinyFits info = {0};
        int err = tinyfits_info_from_memory(&info, "SIMPLE  =", 9);
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

        TinyFits info = {0};
        void* pixels;
        int err = tinyfits_load_from_memory(&info, b.data, b.size, &pixels);
        /* Should fail: either overflow detected or not enough data */
        CHECK(err != TINYFITS_OK, "huge dimensions rejected");
        CHECK(pixels == NULL, "pixels NULL on failure");
        tinyfits_free(&info);
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

        TinyFits info = {0};
        int err = tinyfits_info_from_memory(&info, b.data, b.size);
        CHECK(err == TINYFITS_ERR_INVALID, "negative NAXIS1 rejected");
        tinyfits_free(&info);
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

        TinyFits info = {0};
        int err = tinyfits_info_from_memory(&info, b.data, b.size);
        /* atoi("garbage") = 0, which is not a valid BITPIX */
        CHECK(err != TINYFITS_OK, "garbage BITPIX rejected");
        tinyfits_free(&info);
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

        TinyFits info = {0};
        void* pixels;
        int err = tinyfits_load_from_memory(&info, b.data, b.size, &pixels);
        CHECK(err == TINYFITS_ERR_READ, "truncated pixel data rejected");
        CHECK(pixels == NULL, "pixels NULL on truncated data");
        tinyfits_free(&info);
        fitsbuf_free(&b);
    }

    /* All zeros (looks like FITS magic might partially match) */
    {
        uint8_t zeros[2880];
        memset(zeros, 0, sizeof(zeros));
        TinyFits info = {0};
        int err = tinyfits_info_from_memory(&info, zeros, sizeof(zeros));
        CHECK(err == TINYFITS_ERR_NOT_FITS, "all-zeros rejected");
    }

    /* Valid SIMPLE but no END within the block */
    {
        char block[2880];
        memset(block, ' ', 2880);
        memcpy(block, "SIMPLE  =                    T", 30);
        memcpy(block + 80, "BITPIX  =                   16", 30);
        memcpy(block + 160, "NAXIS   =                    2", 30);
        memcpy(block + 240, "NAXIS1  =                    4", 30);
        memcpy(block + 320, "NAXIS2  =                    4", 30);
        /* No END card, and buffer is exactly one block */

        TinyFits info = {0};
        int err = tinyfits_info_from_memory(&info, block, 2880);
        /* Should fail: no END card, and no second block to read */
        CHECK(err != TINYFITS_OK, "missing END card rejected");
        tinyfits_free(&info);
    }
}

static void test_null_params(void)
{
    printf("Testing NULL parameter handling ...\n");

    TinyFits info = {0};
    void* pixels;

    CHECK(tinyfits_load_from_memory(NULL, "x", 1, &pixels) == TINYFITS_ERR_INVALID,
          "NULL info to load rejected");
    CHECK(tinyfits_load_from_memory(&info, NULL, 2880, &pixels) == TINYFITS_ERR_INVALID,
          "NULL data to load rejected");
    CHECK(tinyfits_info_from_memory(NULL, "x", 1) == TINYFITS_ERR_INVALID,
          "NULL info to info rejected");
    CHECK(tinyfits_info_from_memory(&info, NULL, 2880) == TINYFITS_ERR_INVALID,
          "NULL data to info rejected");
}

static void test_single_quote_roundtrip(void)
{
    printf("Testing single-quote in header value round-trip ...\n");

    TinyFits w = {0};
    w.width = 2; w.height = 1; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT8;
    tinyfits_set_keyword(&w, "OBSERVER", "O'Brien", "");

    uint8_t src[] = {1, 2};
    void* fdata; size_t fsize;
    int err = tinyfits_save_to_memory(&w, src, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFits r = {0};
    void* pixels;
    err = tinyfits_load_from_memory(&r, fdata, fsize, &pixels);
    CHECK(err == TINYFITS_OK, "load succeeds");

    const char* obs = tinyfits_get_keyword(&r, "OBSERVER");
    CHECK(obs != NULL && strcmp(obs, "O'Brien") == 0, "single quote preserved");

    tinyfits_free_buffer(pixels);
    tinyfits_free(&r);
    tinyfits_free_buffer(fdata);
    tinyfits_free(&w);
}

static void test_zero_width(void)
{
    printf("Testing NAXIS1=0 rejection ...\n");

    FitsBuf b;
    fitsbuf_init(&b);
    fitsbuf_card(&b, "SIMPLE", "                   T");
    fitsbuf_card_int(&b, "BITPIX", 8);
    fitsbuf_card_int(&b, "NAXIS", 2);
    fitsbuf_card_int(&b, "NAXIS1", 0);
    fitsbuf_card_int(&b, "NAXIS2", 10);
    fitsbuf_end(&b);

    TinyFits info = {0};
    int err = tinyfits_info_from_memory(&info, b.data, b.size);
    CHECK(err == TINYFITS_ERR_INVALID, "NAXIS1=0 rejected");
    tinyfits_free(&info);
    fitsbuf_free(&b);
}

static void test_interleaved_single_channel(void)
{
    printf("Testing interleaved=1 with single channel ...\n");

    uint16_t src[] = {100, 200, 300, 400};
    TinyFits w = {0};
    w.width = 2; w.height = 2; w.num_channels = 1;
    w.pixel_type = TINYFITS_UINT16;

    void* fdata_il; size_t fsize_il;
    int err = tinyfits_save_to_memory(&w, src, &fdata_il, &fsize_il, 1);
    CHECK(err == TINYFITS_OK, "save interleaved succeeds");

    void* fdata_pl; size_t fsize_pl;
    err = tinyfits_save_to_memory(&w, src, &fdata_pl, &fsize_pl, 0);
    CHECK(err == TINYFITS_OK, "save planar succeeds");

    /* Load both and compare pixels */
    TinyFits r1 = {0}, r2 = {0};
    void *px1, *px2;
    tinyfits_load_from_memory(&r1, fdata_il, fsize_il, &px1);
    tinyfits_load_from_memory(&r2, fdata_pl, fsize_pl, &px2);

    CHECK(memcmp(px1, px2, 4 * sizeof(uint16_t)) == 0,
          "interleaved=1 and interleaved=0 produce same pixels for 1 channel");

    tinyfits_free_buffer(px1);
    tinyfits_free_buffer(px2);
    tinyfits_free(&r1);
    tinyfits_free(&r2);
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

    TinyFits info = {0};
    void* pixels;
    int err = tinyfits_load_from_memory(&info, b.data, b.size, &pixels);
    CHECK(err != TINYFITS_OK, "load fails on truncated data");
    CHECK(pixels == NULL, "pixels is NULL");
    CHECK(info.keywords == NULL, "keywords is NULL after failed load");
    CHECK(info.width == 0, "width is 0 after failed load");
    CHECK(info.pixel_type == TINYFITS_UNKNOWN, "pixel_type is UNKNOWN after failed load");

    /* Safe to call free on the zeroed struct */
    tinyfits_free(&info);
    fitsbuf_free(&b);
}

static void test_null_in_value(void)
{
    printf("Testing null byte in header value ...\n");

    TinyFits info = {0};
    /* "M31\0injected" -- strlen sees 3, strncpy copies "M31" */
    int err = tinyfits_set_keyword(&info, "OBJECT", "M31\0injected", "");
    CHECK(err == TINYFITS_OK, "set_header succeeds");

    const char* val = tinyfits_get_keyword(&info, "OBJECT");
    CHECK(val != NULL && strcmp(val, "M31") == 0, "value truncated at NUL");
    CHECK(strlen(val) == 3, "value length is 3");

    tinyfits_free(&info);
}

static void test_continue_roundtrip(void)
{
    printf("Testing CONTINUE card round-trip ...\n");

    /* Build a FITS file with a CONTINUE card */
    FitsBuf b;
    fitsbuf_init(&b);
    fitsbuf_card(&b, "SIMPLE", "                   T");
    fitsbuf_card_int(&b, "BITPIX", 8);
    fitsbuf_card_int(&b, "NAXIS", 2);
    fitsbuf_card_int(&b, "NAXIS1", 2);
    fitsbuf_card_int(&b, "NAXIS2", 2);

    /* A keyword with a long value that uses CONTINUE */
    {
        char card[80];
        memset(card, ' ', 80);
        memcpy(card, "LONGSTR = 'This is the first part of a long string&'", 52);
        fitsbuf_append(&b, card, 80);
    }
    {
        char card[80];
        memset(card, ' ', 80);
        memcpy(card, "CONTINUE  ' and this is the second part.'", 41);
        fitsbuf_append(&b, card, 80);
    }

    fitsbuf_end(&b);
    uint8_t pixels[] = {1, 2, 3, 4};
    fitsbuf_append(&b, pixels, 4);
    fitsbuf_pad_to_block(&b);

    /* Load */
    TinyFits info = {0};
    void* px;
    int err = tinyfits_load_from_memory(&info, b.data, b.size, &px);
    CHECK(err == TINYFITS_OK, "load succeeds");

    /* LONGSTR should have the first part (with trailing &) */
    const char* longstr = tinyfits_get_keyword(&info, "LONGSTR");
    CHECK(longstr != NULL, "LONGSTR present");
    if (longstr)
        CHECK(strstr(longstr, "first part") != NULL, "LONGSTR has first part");

    /* CONTINUE should be stored as a separate header */
    int cont_count = tinyfits_get_keywords(&info, "CONTINUE", NULL, 0);
    CHECK(cont_count == 1, "one CONTINUE card");

    /* Save and reload -- verify cards survive round-trip */
    void* fdata; size_t fsize;
    err = tinyfits_save_to_memory(&info, px, &fdata, &fsize, 0);
    CHECK(err == TINYFITS_OK, "save succeeds");

    TinyFits info2 = {0};
    void* px2;
    err = tinyfits_load_from_memory(&info2, fdata, fsize, &px2);
    CHECK(err == TINYFITS_OK, "reload succeeds");

    const char* longstr2 = tinyfits_get_keyword(&info2, "LONGSTR");
    CHECK(longstr2 != NULL, "LONGSTR preserved after round-trip");
    if (longstr2)
        CHECK(strstr(longstr2, "first part") != NULL, "LONGSTR value preserved");

    int cont_count2 = tinyfits_get_keywords(&info2, "CONTINUE", NULL, 0);
    CHECK(cont_count2 == 1, "CONTINUE card preserved after round-trip");

    tinyfits_free_buffer(px);
    tinyfits_free_buffer(px2);
    tinyfits_free(&info);
    tinyfits_free(&info2);
    tinyfits_free_buffer(fdata);
    fitsbuf_free(&b);
}

int main(void)
{
    /* Basics */
    test_error_strings();
    test_free_safety();
    test_image_size();
    test_pixel_type_constants();
    test_get_header_empty();

    /* Header parsing and info */
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
    test_roundtrip_headers();
    test_roundtrip_load_modify_save();
    test_mandatory_header_order();
    test_save_errors();
    test_naxis_0_and_gt3();
    test_load_struct_reuse();
    test_to_float_all_types();
    test_save_to_file();
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
    test_continue_roundtrip();

    printf("\n%d / %d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
