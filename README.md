# tinyfits

![Build](https://github.com/cek/tinyfits/actions/workflows/test.yml/badge.svg)

A single-header, zero-dependency FITS image reader/writer for C/C++.

Tinyfits supports a subset of FITS features that are commonly
used in amateur astrophotography. It is designed to be easy to use, small,
and reasonably fast.

## Supports

- Grayscale, RGB, and multi-plane images
- 8/16/32-bit integer and 32/64-bit floating point format images
- Keyword reading, modification, addition, and deletion
- FITS image writing
- Pixel data conversion to float32 via `tinyfits_to_float()`
- Custom memory allocation

## Not supported

- Multi-HDU / extension files (only the primary image is read)
- 64-bit integer data
- FITS images with BSCALE != 1
- FITS images with BZERO other than standard values that indicate unsigned data
- Compressed images (Rice, GZIP, HCOMPRESS)
- Binary or ASCII tables
- Automatic concatenation of CONTINUE long strings
- HIERARCH keywords

## Usage

Copy `tinyfits.h` into your project. In exactly one `.c` or `.cpp` file:

```c
#define TINYFITS_IMPLEMENTATION
#include "tinyfits.h"
```

All other files `#include "tinyfits.h"` for the declarations.

### Load an image

```c
TinyFits info = {0};
void* pixels;
int err = tinyfits_load(&info, "light.fits", &pixels);
if (err != TINYFITS_OK) { /* handle error */ }

// info.width, info.height, info.num_channels, info.pixel_type
if (info.pixel_type == TINYFITS_UINT16) {
    uint16_t* px = (uint16_t*)pixels;
    // ...
}

tinyfits_free_buffer(pixels);
tinyfits_free(&info);
```

### Read keywords without loading pixels

```c
TinyFits info = {0};
tinyfits_info(&info, "light.fits");
const char* bayer = tinyfits_get_keyword(&info, "BAYERPAT");
tinyfits_free(&info);
```

### Write an image

```c
TinyFits info = {0};
info.width = 1024;
info.height = 768;
info.num_channels = 1;
info.pixel_type = TINYFITS_UINT16;
tinyfits_set_keyword(&info, "INSTRUME", "ZWO ASI2600MC Pro", "");
tinyfits_save(&info, pixels, "output.fits", 0);
tinyfits_free(&info);
```

## API

| Function | Description |
|----------|-------------|
| `tinyfits_load` | Load image from file (returns native-format pixels) |
| `tinyfits_load_from_memory` | Load image from memory buffer |
| `tinyfits_info` | Read keywords/dimensions from file (no pixels) |
| `tinyfits_info_from_memory` | Read keywords/dimensions from memory (no pixels) |
| `tinyfits_save` | Write image to file |
| `tinyfits_save_to_memory` | Write image to memory buffer |
| `tinyfits_to_float` | Convert native pixels to float32 |
| `tinyfits_free` | Free metadata (keywords) |
| `tinyfits_free_buffer` | Free library-allocated pixel/data buffers |
| `tinyfits_get_keyword` | Look up a keyword value by key |
| `tinyfits_get_keywords` | Get all values for a repeating keyword (HISTORY, etc.) |
| `tinyfits_set_keyword` | Set or replace a keyword |
| `tinyfits_add_keyword` | Append a keyword (for HISTORY, COMMENT) |
| `tinyfits_remove_keyword` | Remove a keyword |
| `tinyfits_image_size` | Pixel buffer size in bytes |
| `tinyfits_error_string` | Convert error code to string |

## Building the tests

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
