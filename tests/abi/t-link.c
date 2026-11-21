#include <tundra/draw.h>

#include <stdlib.h>
#include <string.h>

#include "test.h"

int main(int argc, char **argv) {
	static uint32_t px[16 * 16];
	struct tdraw_canvas *c = tdraw_canvas_new(px, 16, 16, 16 * 4);
	struct tdraw_pen pen = {0xff00ff00, 0, 0};

	CHECK(argc == 2 && strcmp(tdraw_version(), argv[1]) == 0,
	      "the library reports the version of metapkg");
	CHECK(c && tdraw_fill(c, 0, 0, 16, 16, 0xff0000ff) == 0 &&
	          tdraw_ellipse(c, 8, 8, 4, 4, &pen) == 0 &&
	          px[8 * 16 + 8] == 0xff00ff00 && px[0] == 0xff0000ff,
	      "shapes are painted through the shared library");
	void *png = NULL;
	size_t len = 0;
	const char *err = NULL;
	struct tdraw_image *im = NULL;
	int w = 0, h = 0;

	CHECK(tdraw_capture(c, 0, 0, 16, 16, &png, &len) == 0 &&
	          (im = tdraw_image_decode(png, len, &err)) != NULL &&
	          (tdraw_image_size(im, &w, &h), w == 16 && h == 16) &&
	          tdraw_image(c, im, 0, 0, 1) == 0 && px[8 * 16 + 8] == 0xff00ff00,
	      "a capture is written, read and painted through the shared library");
	tdraw_image_free(im);
	free(png);
	tdraw_canvas_free(c);
	return test_summary();
}
