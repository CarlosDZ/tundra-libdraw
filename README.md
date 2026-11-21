# tundra-libdraw

Drawing library of Tundra OS: shapes, images, text and pixel art, painted into a
buffer of the caller. Its design is in ADR-040 of
[Tundra-OS](https://github.com/CarlosDZ/Tundra-OS).

## Requirements

- pixman, which blends every pixel.

## Usage

```c
#include <tundra/draw.h>

struct tdraw_canvas *c = tdraw_canvas_new(px, width, height, stride);
struct tdraw_pen pen = {0xff88c0d0, 1, 0}; /* color, width (0 fills), smooth */

tdraw_fill(c, 0, 0, width, 24, 0xff2e3440);
tdraw_rect(c, 0.5f, 0.5f, width - 1, height - 1, 4, &pen);
tdraw_canvas_free(c);
```

Link with `-ltundra-draw`. The canvas is the caller's buffer, 32-bit ARGB with
premultiplied alpha, as Wayland and wlroots hand it; libdraw paints it in
place, always over what is there. Colors are `0xAARRGGBB`, not premultiplied.

| Function | What it does |
|----------|--------------|
| `tdraw_canvas_new`, `tdraw_canvas_free` | A canvas over a buffer of the caller |
| `tdraw_clip`, `tdraw_unclip` | Paint only inside a rectangle, or everywhere again |
| `tdraw_fill` | A rectangle of whole pixels: the fastest shape |
| `tdraw_line`, `tdraw_polyline` | Lines in any direction; their ends stretch half their width |
| `tdraw_rect` | A rectangle, its corners rounded with a radius |
| `tdraw_ellipse`, `tdraw_arc` | Ellipses and arcs; a filled arc is a slice. Angles in degrees, clockwise from the right |
| `tdraw_polygon` | Any closed outline; filled by the nonzero rule |
| `tdraw_version` | Version of the library |

A pen fills a shape with width 0, and strokes it, centered on its outline,
with any other width. Smoothed, its edges are antialiased; otherwise each pixel
is in or out by its center, so that pixel art stays exact. Pixel `(x, y)`
covers `x` to `x + 1`: a line one pixel wide falls on a row when its `y` is
that row's plus 0.5. Shapes return `0`, `-1` without memory, or `-2` for an
argument out of place: a coordinate past 8192, a stroke without width.

Every shape is turned into trapezoids that do not overlap, cut at each vertex
and each crossing of two edges, and pixman fills them with its antialiased or
exact rasterizer.

## Build and install

```
make
make check
make install
```

Installs `libtundra-draw.so.0`, the `libtundra-draw.so` link and
`<tundra/draw.h>`. `make check` runs the unit tests with ASan and UBSan, a
program linked to the shared library and the list of exported symbols
(`tests/abi/symbols`). The unit tests check the pixels of each shape, and
paint thousands of them at random, inside and outside the canvas, under ASan.

The soname changes when a released function or struct does.

## License

GPL-3.0-or-later.
