#ifndef TDRAW_BDF_WRITE_H
#define TDRAW_BDF_WRITE_H

#include <stdint.h>
#include <stdio.h>

struct bdf_glyph {
	uint32_t cp;
	const uint8_t *rows;
};

static void bdf_char(FILE *f, uint32_t cp, int w, int h, int asc,
                     const uint8_t *rows) {
	fprintf(f, "STARTCHAR u%04X\nENCODING %u\nSWIDTH 500 0\nDWIDTH %d 0\n", cp,
	        cp, w);
	fprintf(f, "BBX %d %d 0 %d\nBITMAP\n", w, h, asc - h);
	for (int r = 0; r < h; r++)
		fprintf(f, "%02X\n", rows[r]);
	fprintf(f, "ENDCHAR\n");
}

static int bdf_write(const char *path, int w, int h, int asc,
                     const struct bdf_glyph *g, int n) {
	uint8_t box[64];
	FILE *f = fopen(path, "w");

	if (!f || w > 8 || h > 64)
		return -1;
	for (int r = 0; r < h; r++)
		box[r] = (uint8_t)(r == 0 || r == h - 1 ? (0xff << (8 - w)) & 0xff
		                                        : 0x80 | 0x80 >> (w - 1));
	fprintf(f, "STARTFONT 2.1\n");
	fprintf(f,
	        "FONT -tdraw-test-medium-r-normal--%d-%d-75-75-c-%d-iso10646-1\n",
	        h, h * 10, w * 10);
	fprintf(f, "SIZE %d 75 75\nFONTBOUNDINGBOX %d %d 0 %d\n", h, w, h, asc - h);
	fprintf(f, "STARTPROPERTIES 6\nFONT_ASCENT %d\nFONT_DESCENT %d\n", asc,
	        h - asc);
	fprintf(f, "SPACING \"C\"\nCHARSET_REGISTRY \"ISO10646\"\n");
	fprintf(f, "CHARSET_ENCODING \"1\"\nDEFAULT_CHAR 0\nENDPROPERTIES\n");
	fprintf(f, "CHARS %d\n", n + 1);
	bdf_char(f, 0, w, h, asc, box);
	for (int i = 0; i < n; i++)
		bdf_char(f, g[i].cp, w, h, asc, g[i].rows);
	fprintf(f, "ENDFONT\n");
	return fclose(f);
}

#endif
