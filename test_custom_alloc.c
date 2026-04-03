/* Test that custom allocator macros compile and work correctly. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int alloc_count = 0;
static int free_count = 0;

static void* my_malloc(size_t sz)        { alloc_count++; return malloc(sz); }
static void* my_calloc(size_t n, size_t s){ alloc_count++; return calloc(n, s); }
static void* my_realloc(void* p, size_t s){ return realloc(p, s); }
static void  my_free(void* p)            { if (p) free_count++; free(p); }

#define TINYFITS_MALLOC(sz)        my_malloc(sz)
#define TINYFITS_CALLOC(cnt, sz)   my_calloc((cnt), (sz))
#define TINYFITS_REALLOC(p, sz)    my_realloc((p), (sz))
#define TINYFITS_FREE(p)           my_free(p)
#define TINYFITS_IMPLEMENTATION
#include "tinyfits.h"

int main(void)
{
    int pass = 0, fail = 0;

    /* Build a minimal FITS image in memory */
    char block[2880];
    memset(block, ' ', 2880);

    const char* cards[] = {
        "SIMPLE  =                    T",
        "BITPIX  =                   16",
        "NAXIS   =                    2",
        "NAXIS1  =                    2",
        "NAXIS2  =                    2",
        "BZERO   =                32768",
        "END",
    };
    for (int i = 0; i < 7; i++)
    {
        size_t len = strlen(cards[i]);
        memcpy(block + i * 80, cards[i], len);
    }

    /* Append 4 big-endian uint16 pixels (with BZERO=32768 XOR) */
    /* Values: 0, 1000, 32768, 65535 -> stored as -32768, -31768, 0, 32767 */
    unsigned char pixels[8] = {0x80, 0x00, 0x83, 0xE8, 0x00, 0x00, 0x7F, 0xFF};
    char full[2880 + 2880];
    memcpy(full, block, 2880);
    memset(full + 2880, 0, 2880);
    memcpy(full + 2880, pixels, 8);

    alloc_count = 0;
    free_count = 0;

    /* Load from memory */
    TinyFits info = {0};
    void* px = NULL;
    int err = tinyfits_load_from_memory(&info, full, sizeof(full), &px);

    if (err != TINYFITS_OK) { printf("FAIL: load failed: %s\n", tinyfits_error_string(err)); fail++; }
    else { pass++; }

    if (alloc_count == 0) { printf("FAIL: no allocations recorded\n"); fail++; }
    else { pass++; }

    /* Verify pixel data */
    if (px)
    {
        uint16_t* p = (uint16_t*)px;
        if (p[0] != 0 || p[1] != 1000 || p[2] != 32768 || p[3] != 65535)
        { printf("FAIL: pixel data mismatch\n"); fail++; }
        else { pass++; }
    }

    /* Save to memory */
    void* out = NULL;
    size_t out_size = 0;
    int allocs_before_save = alloc_count;
    err = tinyfits_save_to_memory(&info, px, &out, &out_size, 0);
    if (err != TINYFITS_OK) { printf("FAIL: save failed\n"); fail++; }
    else { pass++; }

    if (alloc_count <= allocs_before_save) { printf("FAIL: save did not allocate\n"); fail++; }
    else { pass++; }

    /* Free everything */
    int frees_before = free_count;
    tinyfits_free_buffer(px);
    tinyfits_free_buffer(out);
    tinyfits_free(&info);

    if (free_count <= frees_before) { printf("FAIL: no frees recorded\n"); fail++; }
    else { pass++; }

    printf("Custom allocator test: %d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
