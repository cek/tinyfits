# tinyfits

![Build](https://github.com/cek/tinyfits/actions/workflows/test.yml/badge.svg)

A single-header, zero-dependency FITS image reader/writer for C and C++.

`tinyfits` supports single- or multi-channel integer or float FITS image data.

It is intended to work with FITS files commonly used in amateur
astrophotography. In particular, it loads the first image in
a FITS file, if any, and writes single-HDU image files.

## Supports:
 - Greyscale, RGB, and multi-plane images
 - 8/16/32-bit integer and 32/64-bit floating point format images
 - Arbitrary BZERO/BSCALE transformations
 - Auto-conversion to unsigned integer datatypes when indicated
 - Optional image data conversion to normalized or physical float32 values
 - Keyword reading, setting, addition, deletion
 - Long string values via CONTINUE (transparent on read and write)
 - HIERARCH long keywords up to 63 chars (transparent on read and write)
 - HISTORY/COMMENT auto-split for long text
 - Single-HDU image writing, with optional interleaved-to-planar conversion
 - Custom memory allocation

## Not supported
 - 64-bit integer image data
 - Compressed images (e.g., Rice, GZIP, HCOMPRESS)
 - Tables, 1D arrays, Random Groups, mosaic / tile schemes, etc.
 - Multi-HDU output

## Requirements

- C99 or later, C++ via `extern "C"`
- No external dependencies beyond standard library
- Tested with MSVC, GCC, and Clang on Windows, Linux, and macOS

`tinyfits` is thread-compatible; it maintains no global state, but
concurrent access to a single `TinyFitsHeader` requires external
synchronization.

## Usage


### Including

Copy `tinyfits.h` into your project. In exactly one `.c` or `.cpp` file:

```c
#define TINYFITS_IMPLEMENTATION
#include "tinyfits.h"
```

Other files may `#include "tinyfits.h"` for function and type declarations.

### Load an image

```c
TinyFitsHeader header = {0};
void* pixels = NULL;
int err = tinyfits_load(&header, "light.fits", &pixels);
if (err != TINYFITS_OK) {
    fprintf(stderr, "Unable to load FITS file: %s\n", header.last_error);
    return;
}

if (header.pixel_type == TINYFITS_UINT16) {
    uint16_t* px = (uint16_t*)pixels;
    // ...
}

tinyfits_free_buffer(pixels);
tinyfits_free_header(&header);
```

### Read header without loading pixel data

```c
TinyFitsHeader header = {0};
tinyfits_load_header(&header, "light.fits");
const TinyFitsKeyword* kw = tinyfits_get_keyword(&header, "BAYERPAT");
if (kw && kw->value) {
    printf("Bayer pattern: %s\n", kw->value);
}
tinyfits_free_header(&header);
```

### Write an image

```c
TinyFitsHeader header = {0};
header.width = 6248;
header.height = 4176;
header.num_channels = 1;
header.pixel_type = TINYFITS_UINT16;
header.bscale = 1.0;

void* pixels = malloc(header.width * header.height * sizeof(uint16_t));

// Short keyword, value, empty comment
tinyfits_set_keyword(&header, "INSTRUME", "ZWO ASI2600MC Pro", "");

// Long string values chain via CONTINUE on save
tinyfits_set_keyword(&header,
                   "OBSERVER",
                   "Not from the stars do I my judgement pluck; And yet methinks I have Astronomy",
                   "Sonnet 14");

// Long keys
tinyfits_set_keyword(&header, "ESO INS LAMP1 ID", "HALOGEN", "Lamp ID");

// HISTORY auto-splits across multiple cards if needed:
tinyfits_add_history(&header, "Calibrated with master dark and flat");

int err = tinyfits_save(&header, pixels, "output.fits", 0 /* planar */);
if (err != TINYFITS_OK) {
   fprintf(stderr, "Failed to save FITS image: %s\n", header.last_error);
   return;
}

tinyfits_free_header(&header);
free(pixels);
```

### Convert pixel data to float

```c
float* out = malloc(header.width * header.height * header.num_channels * sizeof(float));

// Physical units per FITS spec (out = bzero + bscale * stored).
tinyfits_to_float_physical(&header, pixels, out);

// Pixel datatype storage range mapped to [0, 1] (integer pixel types only).
tinyfits_to_float_normalized(&header, pixels, out);
```

## API

| Function | Description |
|----------|-------------|
| `tinyfits_load` | Load image from file (returns native-format pixels) |
| `tinyfits_load_from_memory` | Load image from memory buffer |
| `tinyfits_load_header` | Initialize `TinyFitsHeader` from file |
| `tinyfits_load_header_from_memory` | Initialize `TinyFitsHeader` from buffer |
| `tinyfits_save` | Write image to file |
| `tinyfits_save_to_memory` | Write image to memory buffer |
| `tinyfits_to_float_physical` | Convert native pixels to float32, applying BSCALE/BZERO |
| `tinyfits_to_float_normalized` | Convert integer pixels to float32 in [0, 1] |
| `tinyfits_free_header` | Free header data |
| `tinyfits_free_buffer` | Free library-allocated pixel/data buffers |
| `tinyfits_get_keyword` | Look up a keyword by key (case-insensitive for standard keys; HIERARCH lookups normalize spacing) |
| `tinyfits_get_keywords` | Get all entries with matching keys |
| `tinyfits_set_keyword` | Set or replace a keyword (long string values chain via CONTINUE on save) |
| `tinyfits_append_keyword` | Append a keyword (HISTORY/COMMENT auto-split into multiple cards) |
| `tinyfits_remove_keyword` | Remove the first matching keyword |
| `tinyfits_add_history` | Convenience wrapper: `append_keyword("HISTORY", text, NULL)` |
| `tinyfits_add_comment` | Convenience wrapper: `append_keyword("COMMENT", text, NULL)` |
| `tinyfits_image_size` | Pixel buffer size in bytes |

## Running the tests

```bash
# Compile and run (MSVC)
cl /W4 /D_CRT_SECURE_NO_WARNINGS test_tinyfits.c /Fe:test_tinyfits.exe
test_tinyfits.exe

# Compile and run (GCC/Clang)
cc -Wall -Wextra -O2 test_tinyfits.c -o test_tinyfits -lm
./test_tinyfits
```

## License

Public domain or MIT, depending on your needs. See `LICENSE` or the end of `tinyfits.h` for details.
