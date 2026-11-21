#ifndef TDRAW_PNG_ENCODE_H
#define TDRAW_PNG_ENCODE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void be32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static uint32_t crc32_of(const uint8_t *p, size_t n) {
	uint32_t c = 0xffffffffu;

	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
	}
	return ~c;
}

static size_t chunk(uint8_t *out, const char *type, const uint8_t *data,
                    uint32_t n) {
	be32(out, n);
	memcpy(out + 4, type, 4);
	if (n)
		memcpy(out + 8, data, n);
	be32(out + 8 + n, crc32_of(out + 4, n + 4));
	return n + 12;
}

static size_t png_encode(uint8_t *out, int w, int h, const uint8_t *rgba) {
	static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	uint8_t ihdr[13] = {0}, z[4096];
	uint32_t raw = (uint32_t)h * (1 + (uint32_t)w * 4), a = 1, b = 0;
	size_t n = 8, k = 0;

	memcpy(out, sig, 8);
	be32(ihdr, (uint32_t)w);
	be32(ihdr + 4, (uint32_t)h);
	ihdr[8] = 8;
	ihdr[9] = 6;
	n += chunk(out + n, "IHDR", ihdr, 13);
	z[k++] = 0x78;
	z[k++] = 0x01;
	z[k++] = 1;
	z[k++] = (uint8_t)raw;
	z[k++] = (uint8_t)(raw >> 8);
	z[k++] = (uint8_t)~raw;
	z[k++] = (uint8_t)(~raw >> 8);
	for (int y = 0; y < h; y++)
		for (int x = -1; x < w * 4; x++) {
			uint8_t v = x < 0 ? 0 : rgba[(size_t)y * w * 4 + (size_t)x];

			z[k++] = v;
			a = (a + v) % 65521;
			b = (b + a) % 65521;
		}
	be32(z + k, b << 16 | a);
	k += 4;
	n += chunk(out + n, "IDAT", z, (uint32_t)k);
	n += chunk(out + n, "IEND", NULL, 0);
	return n;
}

#endif
