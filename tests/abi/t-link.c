#include <tundra/draw.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bdf-write.h"
#include "test.h"

static void home_through(uint32_t *px, struct tdraw_canvas *c, int *text_ok,
                         int *theme_ok) {
	static const uint8_t bar[16] = {0, 0, 0, 0, 0, 0xff};
	const struct bdf_glyph g = {'A', bar};
	char dir[64], p[160];
	const char *tmp = getenv("TMPDIR"), *err = NULL;
	struct tdraw_font *f;
	struct tdraw_theme *t;
	FILE *conf;

	*text_ok = *theme_ok = 0;
	snprintf(dir, sizeof(dir), "%s/tdraw-abi-XXXXXX", tmp ? tmp : "/tmp");
	if (!mkdtemp(dir))
		return;
	snprintf(p, sizeof(p), "%s/a.bdf", dir);
	bdf_write(p, 8, 16, 12, &g, 1);
	snprintf(p, sizeof(p), "%s/.config", dir);
	mkdir(p, 0700);
	snprintf(p, sizeof(p), "%s/.config/tundra", dir);
	mkdir(p, 0700);
	snprintf(p, sizeof(p), "%s/.config/tundra/fonts", dir);
	if ((conf = fopen(p, "w"))) {
		fprintf(conf, "abi = %s/a.bdf\n", dir);
		fclose(conf);
	}
	snprintf(p, sizeof(p), "%s/.config/tundra/theme", dir);
	if ((conf = fopen(p, "w"))) {
		fprintf(conf, "abi = 88c0d0 128\n");
		fclose(conf);
	}
	setenv("HOME", dir, 1);
	f = tdraw_font_open("abi", 16, &err);
	memset(px, 0, 16 * 16 * 4);
	*text_ok = f && tdraw_text(c, f, 0, 0, "A", 1, 0xffffffff) == 0 &&
	           tdraw_text_width(f, "AA", 2) == 16 && px[5 * 16] == 0xffffffff &&
	           px[4 * 16] == 0;
	if (!f)
		printf("# %s\n", err);
	tdraw_font_free(f);
	t = tdraw_theme_open();
	*theme_ok = t && tdraw_theme_fd(t) >= 0 && tdraw_theme_load(t, &err) == 1 &&
	            tdraw_theme_color(t, "abi", 0) == 0x8088c0d0;
	tdraw_theme_free(t);
	unlink(p);
	snprintf(p, sizeof(p), "%s/.config/tundra/fonts", dir);
	unlink(p);
	snprintf(p, sizeof(p), "%s/.config/tundra", dir);
	rmdir(p);
	snprintf(p, sizeof(p), "%s/.config", dir);
	rmdir(p);
	snprintf(p, sizeof(p), "%s/a.bdf", dir);
	unlink(p);
	rmdir(dir);
}

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
	int text_ok, theme_ok;

	home_through(px, c, &text_ok, &theme_ok);
	CHECK(text_ok,
	      "text is drawn through the shared library, from ~/.config/tundra");
	CHECK(theme_ok, "and the theme is read from there");
	tdraw_canvas_free(c);
	return test_summary();
}
