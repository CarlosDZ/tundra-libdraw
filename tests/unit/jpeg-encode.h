#ifndef TDRAW_JPEG_ENCODE_H
#define TDRAW_JPEG_ENCODE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct jbits {
	uint8_t *out;
	size_t n;
	uint32_t acc;
	int nacc;
};

static void jput(struct jbits *b, uint32_t code, int len) {
	for (int i = len - 1; i >= 0; i--) {
		b->acc = (b->acc << 1) | (code >> i & 1);
		if (++b->nacc == 8) {
			b->out[b->n++] = (uint8_t)b->acc;
			if ((uint8_t)b->acc == 0xff)
				b->out[b->n++] = 0;
			b->acc = 0;
			b->nacc = 0;
		}
	}
}

static uint8_t jclamp(double v) {
	return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)(v + 0.5);
}

static size_t jpeg_encode(uint8_t *out, int bw, int bh,
                          const uint8_t (*rgb)[3]) {
	static const uint8_t dc_bits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1};
	static const uint8_t head[] = {0xff, 0xd8, 0xff, 0xe0, 0, 16, 'J',
	                               'F',  'I',  'F',  0,    1, 1,  0,
	                               0,    1,    0,    1,    0, 0};
	uint32_t code[12];
	int len[12], prev[3] = {0, 0, 0};
	struct jbits b = {out, 0, 0, 0};
	size_t n = 0;

	for (int l = 1, c = 0, v = 0; l <= 16; l++, c <<= 1)
		for (int k = 0; k < (l <= 16 ? dc_bits[l - 1] : 0); k++, c++, v++) {
			code[v] = (uint32_t)c;
			len[v] = l;
		}
	memcpy(out, head, sizeof(head));
	n = sizeof(head);
	memcpy(out + n, "\xff\xdb\x00\x43\x00", 5);
	n += 5;
	memset(out + n, 1, 64);
	n += 64;
	uint8_t sof[] = {0xff,
	                 0xc0,
	                 0,
	                 17,
	                 8,
	                 (uint8_t)(bh * 8 >> 8),
	                 (uint8_t)(bh * 8),
	                 (uint8_t)(bw * 8 >> 8),
	                 (uint8_t)(bw * 8),
	                 3,
	                 1,
	                 0x11,
	                 0,
	                 2,
	                 0x11,
	                 0,
	                 3,
	                 0x11,
	                 0};
	memcpy(out + n, sof, sizeof(sof));
	n += sizeof(sof);
	memcpy(out + n, "\xff\xc4\x00\x31\x00", 5);
	n += 5;
	memcpy(out + n, dc_bits, 16);
	n += 16;
	for (int v = 0; v < 12; v++)
		out[n++] = (uint8_t)v;
	out[n++] = 0x10;
	memset(out + n, 0, 16);
	out[n] = 1;
	n += 16;
	out[n++] = 0;
	static const uint8_t sos[] = {0xff, 0xda, 0, 12, 3, 1,  0,
	                              2,    0,    3, 0,  0, 63, 0};
	memcpy(out + n, sos, sizeof(sos));
	n += sizeof(sos);
	b.n = n;
	for (int i = 0; i < bw * bh; i++) {
		double r = rgb[i][0], g = rgb[i][1], bl = rgb[i][2];
		uint8_t ycc[3] = {jclamp(0.299 * r + 0.587 * g + 0.114 * bl),
		                  jclamp(128 - 0.168736 * r - 0.331264 * g + 0.5 * bl),
		                  jclamp(128 + 0.5 * r - 0.418688 * g - 0.081312 * bl)};

		for (int c = 0; c < 3; c++) {
			int dc = 8 * (ycc[c] - 128), diff = dc - prev[c], cat = 0;

			prev[c] = dc;
			for (int m = diff < 0 ? -diff : diff; m; m >>= 1)
				cat++;
			jput(&b, code[cat], len[cat]);
			if (cat)
				jput(&b, (uint32_t)(diff < 0 ? diff + (1 << cat) - 1 : diff),
				     cat);
			jput(&b, 0, 1);
		}
	}
	while (b.nacc)
		jput(&b, 1, 1);
	b.out[b.n++] = 0xff;
	b.out[b.n++] = 0xd9;
	return b.n;
}

#endif
