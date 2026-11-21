# tundra-libdraw

Drawing library of Tundra OS: shapes, images, text and pixel art, painted into a
buffer of the caller. Its design is in ADR-040 of
[Tundra-OS](https://github.com/CarlosDZ/Tundra-OS).

## Usage

```c
#include <tundra/draw.h>
```

Link with `-ltundra-draw`.

| Function | What it does |
|----------|--------------|
| `tdraw_version` | Version of the library |

## Build and install

```
make
make check
make install
```

Installs `libtundra-draw.so.0`, the `libtundra-draw.so` link and
`<tundra/draw.h>`. `make check` runs the unit tests with ASan and UBSan, a
program linked to the shared library and the list of exported symbols
(`tests/abi/symbols`).

The soname changes when a released function or struct does.

## License

GPL-3.0-or-later.
