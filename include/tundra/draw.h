#ifndef TUNDRA_DRAW_H
#define TUNDRA_DRAW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TDRAW_COORD_MAX 8192.0f

struct tdraw_canvas;

struct tdraw_canvas *tdraw_canvas_new(uint32_t *px, int width, int height,
                                      int stride);
void tdraw_canvas_free(struct tdraw_canvas *c);

int tdraw_clip(struct tdraw_canvas *c, int x, int y, int w, int h);
void tdraw_unclip(struct tdraw_canvas *c);

struct tdraw_pen {
	uint32_t color;
	float width;
	int smooth;
};

int tdraw_fill(struct tdraw_canvas *c, int x, int y, int w, int h,
               uint32_t color);

int tdraw_line(struct tdraw_canvas *c, float x0, float y0, float x1, float y1,
               const struct tdraw_pen *p);

int tdraw_rect(struct tdraw_canvas *c, float x, float y, float w, float h,
               float radius, const struct tdraw_pen *p);

int tdraw_ellipse(struct tdraw_canvas *c, float cx, float cy, float rx,
                  float ry, const struct tdraw_pen *p);

int tdraw_arc(struct tdraw_canvas *c, float cx, float cy, float rx, float ry,
              float from, float to, const struct tdraw_pen *p);

int tdraw_polygon(struct tdraw_canvas *c, const float *xy, int n,
                  const struct tdraw_pen *p);
int tdraw_polyline(struct tdraw_canvas *c, const float *xy, int n,
                   const struct tdraw_pen *p);

struct tdraw_image;

struct tdraw_image *tdraw_image_decode(const void *data, size_t len,
                                       const char **err);
void tdraw_image_size(const struct tdraw_image *im, int *w, int *h);
void tdraw_image_free(struct tdraw_image *im);

int tdraw_image(struct tdraw_canvas *c, const struct tdraw_image *im, int x,
                int y, int scale);

int tdraw_image_fit(struct tdraw_canvas *c, const struct tdraw_image *im, int x,
                    int y, int w, int h);

int tdraw_capture(struct tdraw_canvas *c, int x, int y, int w, int h,
                  void **png, size_t *len);

struct tdraw_font;

#define TDRAW_FONT_SIZE_MAX 256
#define TDRAW_TEXT_MAX 65536

struct tdraw_font *tdraw_font_open(const char *use, int size, const char **err);
void tdraw_font_free(struct tdraw_font *f);

int tdraw_font_exact(const char *use, int size, const char **err);

struct tdraw_font_metrics {
	int height, ascent, cell;
};

void tdraw_font_metrics(const struct tdraw_font *f,
                        struct tdraw_font_metrics *m);

int tdraw_text(struct tdraw_canvas *c, struct tdraw_font *f, int x, int y,
               const char *utf8, size_t len, uint32_t color);

int tdraw_text_width(struct tdraw_font *f, const char *utf8, size_t len);

struct tdraw_theme;

struct tdraw_theme *tdraw_theme_open(void);
void tdraw_theme_free(struct tdraw_theme *t);

int tdraw_theme_load(struct tdraw_theme *t, const char **err);

int tdraw_theme_fd(const struct tdraw_theme *t);

uint32_t tdraw_theme_color(const struct tdraw_theme *t, const char *name,
                           uint32_t fallback);

struct tdraw_rect {
	int x, y, w, h;
};

struct tdraw_art;

#define TDRAW_HFLIP 1
#define TDRAW_VFLIP 2

struct tdraw_art *tdraw_art_open(const void *toa, size_t len,
                                 const struct tdraw_theme *theme,
                                 const char **err);
void tdraw_art_free(struct tdraw_art *a);
void tdraw_art_size(const struct tdraw_art *a, int *w, int *h);

const char *tdraw_art_anim(const struct tdraw_art *a, int i);

int tdraw_art_theme(struct tdraw_art *a, const struct tdraw_theme *theme);

int tdraw_art_play(struct tdraw_art *a, const char *anim, uint64_t now_ms);

int tdraw_art_advance(struct tdraw_art *a, uint64_t now_ms,
                      struct tdraw_rect *changed);

int tdraw_art(struct tdraw_canvas *c, const struct tdraw_art *a, int x, int y,
              int scale, int flip);

const char *tdraw_version(void);

#ifdef __cplusplus
}
#endif

#endif
