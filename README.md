# tundra-libdraw

Drawing library of Tundra OS: shapes, images, text and pixel art, painted into a
buffer of the caller. Its design is in ADR-040 of
[Tundra-OS](https://github.com/CarlosDZ/Tundra-OS).

## Requirements

- pixman, which blends every pixel; zlib, which compresses captures.
- FreeType, which draws each letter once; tundra-libconf, which reads the fonts
  file.
- tundra-libtoa, which reads the pixel art of `.toa` files.
- Terminus, Symbols Nerd Font and DejaVu: the fonts of `/etc/tundra/fonts`.
- wuffs, to build: its PNG and JPEG decoders, compiled in.
- DejaVu Sans and DejaVu Sans Mono, to test: `make check TEST_FONT_DIR=…` if
  they are not in `/usr/share/fonts/TTF`.

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
| `tdraw_image_decode`, `tdraw_image_size`, `tdraw_image_free` | A PNG or a JPEG, from bytes of the caller, decoded once into premultiplied ARGB |
| `tdraw_image` | An image at a whole scale, 1 to 64, each of its pixels a square: for pixel art and icons |
| `tdraw_image_fit` | An image stretched to a rectangle, smoothed: for photos |
| `tdraw_capture` | A rectangle of the canvas as a PNG, compressed at zlib's fastest level |
| `tdraw_font_open`, `tdraw_font_free` | The fonts of a use, such as `mono` or `ui`, at a line height in pixels |
| `tdraw_font_exact` | The largest line, up to a size, that the fonts of a use draw to the pixel |
| `tdraw_font_metrics` | Height of the line, ascent to the baseline, and the cell when the font is monospaced |
| `tdraw_text`, `tdraw_text_width` | A line of UTF-8 from its top left corner, and its width |
| `tdraw_theme_open`, `tdraw_theme_free` | The theme, empty until loaded |
| `tdraw_theme_load`, `tdraw_theme_fd` | Reads the theme files; the descriptor is readable when they change |
| `tdraw_theme_color` | A color of the theme by name, or the caller's when it has none |
| `tdraw_art_open`, `tdraw_art_size`, `tdraw_art_free` | Pixel art from the bytes of a `.toa`, which outlive it |
| `tdraw_art` | The drawing at a whole scale, 1 to 64, flipped horizontally, vertically or both |
| `tdraw_art_play`, `tdraw_art_advance` | Starts an animation at an instant, and applies what is due at another: what changed and the ms to the next change |
| `tdraw_art_anim` | The names of its animations, in order |
| `tdraw_art_theme` | Looks its colors up again once the theme has loaded |
| `tdraw_version` | Version of the library |

A pen fills a shape with width 0, and strokes it, centered on its outline,
with any other width. Smoothed, its edges are antialiased; otherwise each pixel
is in or out by its center, so that pixel art stays exact. Pixel `(x, y)`
covers `x` to `x + 1`: a line one pixel wide falls on a row when its `y` is
that row's plus 0.5. Shapes return `0`, `-1` without memory, or `-2` for an
argument out of place: a coordinate past 8192, a stroke without width.

Images are decoded by wuffs, whose decoders check every bound of what they
read, and premultiplied by libdraw, with the rounding that `tdraw_capture`
undoes: a capture read back is the same, to the bit.

Fonts come from `/etc/tundra/fonts` and `~/.config/tundra/fonts`, one line
per font of a use, in order:

```
mono = /usr/share/fonts/terminus/ter-u24n.otb
mono = /usr/share/fonts/TTF/DejaVuSansMono.ttf
```

Each letter comes from the first font that has it, and one that no font has
is the box of the first. A use in the user's file replaces its list in the
system's. A bitmap font only takes the sizes that are whole multiples of its
height, each of its pixels a square; a vector font is smoothed in gray and set
to the size. When the first font is monospaced, every letter takes one cell:
monospaced vector fonts after it shrink to fit there, and other letters are
centered on it. Text has no kerning and no line
breaks.

The theme comes from `/etc/tundra/theme` and `~/.config/tundra/theme`:
`color0` to `color15`, the colors of the terminal, and any other name a
program asks for, as `RRGGBB` with an optional alpha of 0 to 255:

```
accent = 66dc5c
border = 3e494f 128
```

`/etc/tundra/theme` comes with the colors of Tundra (`data/theme`): the 16 of
the terminal and the names its programs share, `background`, `surface`,
`text`, `dim`, `border`, `focus`, `accent`, `alert`, `warn` and `ok`. A program
of Tundra looks a color up as `program.name` first and then as `name`, so that
`borealis.alert` changes the red of borealis alone. The user's file replaces the system's color by color, and a color neither has
is the one the caller passes. `tdraw_theme_fd` watches both directories with
inotify, or the nearest directory above each that exists until it does: a
program puts it in its event
loop, loads the theme when it is readable, and repaints if a color changed. A
broken file keeps the colors as they were.

Pixel art keeps its current frame resolved: a palette entry and a color for
each pixel, 5 bytes. Painting it is one blend by pixman, the same as an image,
and a frame only rewrites the cells it changes. The caller holds the clock:

```c
tdraw_art_play(a, "walk", now);
int ms = tdraw_art_advance(a, now, &changed); /* -1: nothing else changes */
```

and puts a timer of `ms` in its event loop. Woken late, it applies what it
missed at once, and at most one lap of a loop. A color of the theme that the
theme lacks is painted with the RGB the drawing keeps for it.

Each letter is drawn once by FreeType into the glyph cache of pixman, which
drops the least used past 16384, and a line is composited in one call.

Every shape is turned into trapezoids that do not overlap, cut at each vertex
and each crossing of two edges, and pixman fills them with its antialiased or
exact rasterizer.

## Build and install

```
make
make check
make install
```

Installs `libtundra-draw.so.0`, the `libtundra-draw.so` link,
`<tundra/draw.h>`, and `/etc/tundra/fonts` and `/etc/tundra/theme` if they
are not there yet. `make check` runs the unit tests with ASan and UBSan, a
program linked to the shared library and the list of exported symbols
(`tests/abi/symbols`). Built checkouts of tundra-libconf and tundra-libtoa
next to this one, `../tundra-libconf` and `../tundra-libtoa`, are used instead
of the installed libraries. The unit tests check the pixels of each shape, and
paint thousands of them at random, inside and outside the canvas, under ASan.
They make their PNG and JPEG samples themselves, read them back from captures,
and flip every bit of them. They write bitmap fonts to check text to the pixel,
and draw past the limit of the glyph cache. Pixel art is checked against its
source frame by frame and woken years late, and images and pixel art at every
scale from 1 to 64, flipped or not, to the end of the coordinates.

The soname changes when a released function or struct does.

## License

GPL-3.0-or-later.
