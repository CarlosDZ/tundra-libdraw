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

const char *tdraw_version(void);

#ifdef __cplusplus
}
#endif

#endif
