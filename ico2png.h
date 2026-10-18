#ifndef ICO2PNG_H
#define ICO2PNG_H

/*
  Converts the biggest image inside a .ico file into a .png file,
  centered on a transparent 16:10 canvas (speed dial tiles are wide,
  so a square icon would otherwise get cropped).

  Pure C, no libraries: it has its own tiny PNG reader/writer and inflate.
  ico_to_png("in.ico", "out.png")        -> 1 on success, 0 on failure
  ico_to_favicon_png("in.ico", "out.png", 64) -> small square png for a page icon
  ico_to_favicon_datauri("in.ico", 64)   -> same icon as "data:image/png;base64,..." text
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---------- small helpers ---------- */

static uint32_t i2p_rd16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t i2p_rd32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t i2p_be32(const uint8_t* p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static void i2p_put_be32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static const uint8_t I2P_PNG_SIG[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

/* ---------- PNG writer (uses "stored" zlib blocks, so no compressor is needed) ---------- */

static uint32_t i2p_crc_table[256];
static int i2p_crc_ready = 0;

static uint32_t i2p_crc_update(uint32_t crc, const uint8_t* buf, size_t n) {
	if (!i2p_crc_ready) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			i2p_crc_table[i] = c;
		}
		i2p_crc_ready = 1;
	}
	for (size_t i = 0; i < n; i++) crc = i2p_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
	return crc;
}

/* writes one PNG chunk into memory, returns the bytes used */
static size_t i2p_chunk(uint8_t* o, const char* type, const uint8_t* data, uint32_t len) {
	i2p_put_be32(o, len);
	memcpy(o + 4, type, 4);
	if (len) memcpy(o + 8, data, len);
	uint32_t crc = i2p_crc_update(0xFFFFFFFFu, o + 4, (size_t)len + 4) ^ 0xFFFFFFFFu;
	i2p_put_be32(o + 8 + len, crc);
	return (size_t)len + 12;
}

/* builds a whole PNG file in memory (caller frees) */
static uint8_t* i2p_encode_png(int w, int h, const uint8_t* rgba, size_t* outLen) {
	size_t rowLen = 1 + (size_t)w * 4;
	size_t rawSize = rowLen * (size_t)h;
	uint8_t* raw = (uint8_t*)malloc(rawSize);
	if (!raw) return NULL;
	for (int y = 0; y < h; y++) {
		raw[y * rowLen] = 0;   /* filter type 0 */
		memcpy(raw + y * rowLen + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
	}

	size_t blocks = (rawSize + 65534) / 65535;
	size_t zSize = 2 + blocks * 5 + rawSize + 4;
	uint8_t* z = (uint8_t*)malloc(zSize);
	if (!z) { free(raw); return NULL; }

	size_t zp = 0;
	z[zp++] = 0x78; z[zp++] = 0x01;
	size_t done = 0;
	while (done < rawSize) {
		size_t chunk = rawSize - done;
		if (chunk > 65535) chunk = 65535;
		z[zp++] = (done + chunk == rawSize) ? 1 : 0;
		z[zp++] = (uint8_t)(chunk & 0xFF);  z[zp++] = (uint8_t)(chunk >> 8);
		z[zp++] = (uint8_t)(~chunk & 0xFF); z[zp++] = (uint8_t)((~chunk >> 8) & 0xFF);
		memcpy(z + zp, raw + done, chunk);
		zp += chunk; done += chunk;
	}
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < rawSize; i++) { a = (a + raw[i]) % 65521u; b = (b + a) % 65521u; }
	i2p_put_be32(z + zp, (b << 16) | a);
	zp += 4;
	free(raw);

	uint8_t* png = (uint8_t*)malloc(8 + 25 + zp + 12 + 12);
	if (!png) { free(z); return NULL; }
	uint8_t ihdr[13];
	i2p_put_be32(ihdr, (uint32_t)w); i2p_put_be32(ihdr + 4, (uint32_t)h);
	ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
	memcpy(png, I2P_PNG_SIG, 8);
	size_t p = 8;
	p += i2p_chunk(png + p, "IHDR", ihdr, 13);
	p += i2p_chunk(png + p, "IDAT", z, (uint32_t)zp);
	p += i2p_chunk(png + p, "IEND", NULL, 0);
	free(z);
	*outLen = p;
	return png;
}

static int i2p_write_png(const char* path, int w, int h, const uint8_t* rgba) {
	size_t n = 0;
	uint8_t* png = i2p_encode_png(w, h, rgba, &n);
	if (!png) return 0;
	FILE* f = fopen(path, "wb");
	if (!f) { free(png); return 0; }
	int ok = (fwrite(png, 1, n, f) == n);
	ok = (fclose(f) == 0) && ok;
	free(png);
	return ok;
}

/* ---------- inflate (reads zlib data, needed for PNG-compressed icons) ---------- */

typedef struct {
	const uint8_t* in; size_t inLen, inPos;
	uint32_t bitBuf; int bitCnt;
	uint8_t* out; size_t outCap, outPos;
	int err;
} I2PInf;

typedef struct { short count[16]; short symbol[288]; } I2PHuff;

static int i2p_bits(I2PInf* s, int need) {
	uint32_t val = s->bitBuf;
	while (s->bitCnt < need) {
		if (s->inPos >= s->inLen) { s->err = 1; return 0; }
		val |= (uint32_t)s->in[s->inPos++] << s->bitCnt;
		s->bitCnt += 8;
	}
	s->bitBuf = val >> need;
	s->bitCnt -= need;
	return (int)(val & ((1u << need) - 1));
}

static int i2p_decode(I2PInf* s, const I2PHuff* h) {
	int code = 0, first = 0, index = 0;
	for (int len = 1; len <= 15; len++) {
		code |= i2p_bits(s, 1);
		if (s->err) return -1;
		int count = h->count[len];
		if (code - count < first) return h->symbol[index + (code - first)];
		index += count; first += count; first <<= 1; code <<= 1;
	}
	return -1;
}

static int i2p_construct(I2PHuff* h, const short* length, int n) {
	int offs[16];
	for (int len = 0; len <= 15; len++) h->count[len] = 0;
	for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
	if (h->count[0] == n) return 0;
	int left = 1;
	for (int len = 1; len <= 15; len++) {
		left <<= 1; left -= h->count[len];
		if (left < 0) return left;
	}
	offs[1] = 0;
	for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
	for (int sym = 0; sym < n; sym++)
		if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (short)sym;
	return left;
}

static int i2p_codes(I2PInf* s, const I2PHuff* lc, const I2PHuff* dc) {
	static const short lens[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
	static const short lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
	static const short dists[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
	static const short dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

	for (;;) {
		int sym = i2p_decode(s, lc);
		if (sym < 0) return 1;
		if (sym < 256) {
			if (s->outPos >= s->outCap) return 1;
			s->out[s->outPos++] = (uint8_t)sym;
		}
		else if (sym == 256) {
			return 0;
		}
		else {
			sym -= 257;
			if (sym >= 29) return 1;
			int len = lens[sym] + i2p_bits(s, lext[sym]);
			int ds = i2p_decode(s, dc);
			if (ds < 0 || ds >= 30) return 1;
			size_t dist = (size_t)dists[ds] + (size_t)i2p_bits(s, dext[ds]);
			if (s->err || dist > s->outPos || s->outPos + len > s->outCap) return 1;
			while (len--) { s->out[s->outPos] = s->out[s->outPos - dist]; s->outPos++; }
		}
	}
}

static int i2p_dynamic(I2PInf* s) {
	static const short order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
	short lengths[320];
	I2PHuff lc, dc;

	int nlen = i2p_bits(s, 5) + 257;
	int ndist = i2p_bits(s, 5) + 1;
	int ncode = i2p_bits(s, 4) + 4;
	if (s->err || nlen > 286 || ndist > 30) return 1;

	int i;
	for (i = 0; i < ncode; i++) lengths[order[i]] = (short)i2p_bits(s, 3);
	for (; i < 19; i++) lengths[order[i]] = 0;
	if (i2p_construct(&lc, lengths, 19) != 0) return 1;

	int index = 0;
	while (index < nlen + ndist) {
		int sym = i2p_decode(s, &lc);
		if (sym < 0) return 1;
		if (sym < 16) {
			lengths[index++] = (short)sym;
		}
		else {
			int len = 0;
			if (sym == 16) {
				if (index == 0) return 1;
				len = lengths[index - 1];
				sym = 3 + i2p_bits(s, 2);
			}
			else if (sym == 17) sym = 3 + i2p_bits(s, 3);
			else sym = 11 + i2p_bits(s, 7);
			if (index + sym > nlen + ndist) return 1;
			while (sym--) lengths[index++] = (short)len;
		}
	}
	if (s->err || lengths[256] == 0) return 1;

	int err = i2p_construct(&lc, lengths, nlen);
	if (err && (err < 0 || nlen != lc.count[0] + lc.count[1])) return 1;
	err = i2p_construct(&dc, lengths + nlen, ndist);
	if (err && (err < 0 || ndist != dc.count[0] + dc.count[1])) return 1;
	return i2p_codes(s, &lc, &dc);
}

/* returns 1 if exactly outCap bytes were produced */
static int i2p_zlib_inflate(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCap) {
	if (inLen < 6) return 0;
	I2PInf s;
	memset(&s, 0, sizeof s);
	s.in = in + 2; s.inLen = inLen - 2;
	s.out = out; s.outCap = outCap;

	int last;
	do {
		last = i2p_bits(&s, 1);
		int type = i2p_bits(&s, 2);
		if (s.err) return 0;
		if (type == 0) {
			s.bitBuf = 0; s.bitCnt = 0;
			if (s.inPos + 4 > s.inLen) return 0;
			uint32_t len = i2p_rd16(s.in + s.inPos);
			s.inPos += 4;
			if (s.inPos + len > s.inLen || s.outPos + len > s.outCap) return 0;
			memcpy(s.out + s.outPos, s.in + s.inPos, len);
			s.inPos += len; s.outPos += len;
		}
		else if (type == 1) {
			I2PHuff lc, dc;
			short l[288];
			int i;
			for (i = 0; i < 144; i++) l[i] = 8;
			for (; i < 256; i++) l[i] = 9;
			for (; i < 280; i++) l[i] = 7;
			for (; i < 288; i++) l[i] = 8;
			i2p_construct(&lc, l, 288);
			for (i = 0; i < 30; i++) l[i] = 5;
			i2p_construct(&dc, l, 30);
			if (i2p_codes(&s, &lc, &dc)) return 0;
		}
		else if (type == 2) {
			if (i2p_dynamic(&s)) return 0;
		}
		else return 0;
	} while (!last);

	return s.outPos == s.outCap;
}

/* ---------- PNG reader (8-bit, not interlaced: RGBA, RGB or palette) ---------- */

static uint8_t* i2p_png_to_rgba(const uint8_t* d, size_t n, int* ow, int* oh) {
	if (n < 33 || memcmp(d, I2P_PNG_SIG, 8) != 0) return NULL;

	int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
	uint8_t pal[256 * 3];
	int palN = 0;
	uint8_t* idat = NULL; size_t idatLen = 0;

	size_t pos = 8;
	while (pos + 12 <= n) {
		uint32_t len = i2p_be32(d + pos);
		const uint8_t* type = d + pos + 4;
		const uint8_t* data = d + pos + 8;
		if ((size_t)len > n - pos - 12) break;

		if (memcmp(type, "IHDR", 4) == 0 && len >= 13) {
			w = (int)i2p_be32(data); h = (int)i2p_be32(data + 4);
			depth = data[8]; ctype = data[9]; interlace = data[12];
		}
		else if (memcmp(type, "PLTE", 4) == 0 && len <= 768) {
			memcpy(pal, data, len); palN = (int)(len / 3);
		}
		else if (memcmp(type, "IDAT", 4) == 0) {
			uint8_t* t = (uint8_t*)realloc(idat, idatLen + len);
			if (!t) { free(idat); return NULL; }
			idat = t; memcpy(idat + idatLen, data, len); idatLen += len;
		}
		else if (memcmp(type, "IEND", 4) == 0) break;
		pos += 12 + (size_t)len;
	}

	int bpp = (ctype == 6) ? 4 : (ctype == 2) ? 3 : (ctype == 3) ? 1 : 0;
	if (!idat || bpp == 0 || depth != 8 || interlace != 0 || w <= 0 || h <= 0 || w > 4096 || h > 4096
		|| (ctype == 3 && palN == 0)) {
		free(idat); return NULL;
	}

	size_t rowLen = 1 + (size_t)w * bpp;
	size_t rawSize = rowLen * (size_t)h;
	uint8_t* raw = (uint8_t*)malloc(rawSize);
	if (!raw || !i2p_zlib_inflate(idat, idatLen, raw, rawSize)) { free(idat); free(raw); return NULL; }
	free(idat);

	/* undo the PNG row filters */
	for (int y = 0; y < h; y++) {
		uint8_t* row = raw + (size_t)y * rowLen + 1;
		const uint8_t* prev = y ? raw + (size_t)(y - 1) * rowLen + 1 : NULL;
		int ft = raw[(size_t)y * rowLen];
		for (int i = 0; i < w * bpp; i++) {
			int a = i >= bpp ? row[i - bpp] : 0;
			int b = prev ? prev[i] : 0;
			int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
			int add = 0;
			if (ft == 1) add = a;
			else if (ft == 2) add = b;
			else if (ft == 3) add = (a + b) >> 1;
			else if (ft == 4) {
				int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
				add = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
			}
			else if (ft != 0) { free(raw); return NULL; }
			row[i] = (uint8_t)(row[i] + add);
		}
	}

	uint8_t* rgba = (uint8_t*)malloc((size_t)w * h * 4);
	if (!rgba) { free(raw); return NULL; }
	for (int y = 0; y < h; y++) {
		const uint8_t* row = raw + (size_t)y * rowLen + 1;
		for (int x = 0; x < w; x++) {
			uint8_t* o = rgba + ((size_t)y * w + x) * 4;
			if (ctype == 6) { o[0] = row[x * 4]; o[1] = row[x * 4 + 1]; o[2] = row[x * 4 + 2]; o[3] = row[x * 4 + 3]; }
			else if (ctype == 2) { o[0] = row[x * 3]; o[1] = row[x * 3 + 1]; o[2] = row[x * 3 + 2]; o[3] = 255; }
			else {
				int idx = row[x]; if (idx >= palN) idx = 0;
				o[0] = pal[idx * 3]; o[1] = pal[idx * 3 + 1]; o[2] = pal[idx * 3 + 2]; o[3] = 255;
			}
		}
	}
	free(raw);
	*ow = w; *oh = h;
	return rgba;
}

/* ---------- BMP-style icon image ("DIB") reader ---------- */

static uint8_t* i2p_dib_to_rgba(const uint8_t* d, size_t size, int* ow, int* oh) {
	if (size < 40) return NULL;
	uint32_t hdr = i2p_rd32(d);
	int w = (int)i2p_rd32(d + 4);
	int h = (int)i2p_rd32(d + 8) / 2;           /* icon DIBs store height twice (image + mask) */
	int bpp = (int)i2p_rd16(d + 14);
	uint32_t comp = i2p_rd32(d + 16);
	uint32_t clrUsed = i2p_rd32(d + 32);

	if (hdr < 40 || w <= 0 || h <= 0 || w > 1024 || h > 1024) return NULL;
	if (comp != 0 && comp != 3) return NULL;
	if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return NULL;

	size_t palN = (bpp <= 8) ? (clrUsed ? clrUsed : (1u << bpp)) : 0;
	if (palN > 256) return NULL;
	size_t palOff = hdr;
	size_t pixOff = palOff + palN * 4;
	if (comp == 3 && hdr == 40) pixOff += 12;

	size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
	size_t maskStride = (((size_t)w + 31) / 32) * 4;
	size_t maskOff = pixOff + stride * h;
	if (maskOff > size) return NULL;
	int hasMask = (maskOff + maskStride * h <= size);

	uint8_t* rgba = (uint8_t*)malloc((size_t)w * h * 4);
	if (!rgba) return NULL;

	int anyAlpha = 0;
	for (int y = 0; y < h; y++) {
		const uint8_t* row = d + pixOff + (size_t)(h - 1 - y) * stride;
		for (int x = 0; x < w; x++) {
			uint8_t* o = rgba + ((size_t)y * w + x) * 4;
			uint8_t r, g, b, a = 255;
			if (bpp == 32) { b = row[x * 4]; g = row[x * 4 + 1]; r = row[x * 4 + 2]; a = row[x * 4 + 3]; if (a) anyAlpha = 1; }
			else if (bpp == 24) { b = row[x * 3]; g = row[x * 3 + 1]; r = row[x * 3 + 2]; }
			else {
				size_t idx;
				if (bpp == 8) idx = row[x];
				else if (bpp == 4) idx = (x & 1) ? (row[x / 2] & 15) : (row[x / 2] >> 4);
				else idx = (row[x / 8] >> (7 - (x & 7))) & 1;
				if (idx >= palN) idx = 0;
				const uint8_t* p = d + palOff + idx * 4;
				b = p[0]; g = p[1]; r = p[2];
			}
			o[0] = r; o[1] = g; o[2] = b; o[3] = a;
		}
	}

	/* 32-bit icons normally carry their own alpha; everything else uses the 1-bit mask */
	int useMask = hasMask && !(bpp == 32 && anyAlpha);
	if (bpp == 32 && !anyAlpha)
		for (size_t i = 0; i < (size_t)w * h; i++) rgba[i * 4 + 3] = 255;
	if (useMask) {
		for (int y = 0; y < h; y++) {
			const uint8_t* m = d + maskOff + (size_t)(h - 1 - y) * maskStride;
			for (int x = 0; x < w; x++)
				if ((m[x / 8] >> (7 - (x & 7))) & 1) rgba[((size_t)y * w + x) * 4 + 3] = 0;
		}
	}
	*ow = w; *oh = h;
	return rgba;
}

/* ---------- shared helpers ---------- */

/* reads the .ico and finds its biggest image; caller frees the returned buffer */
static uint8_t* i2p_open_ico(const char* path, const uint8_t** img, size_t* imgSize) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long fsz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsz < 22 || fsz > 16 * 1024 * 1024) { fclose(f); return NULL; }
	uint8_t* buf = (uint8_t*)malloc((size_t)fsz);
	if (!buf || fread(buf, 1, (size_t)fsz, f) != (size_t)fsz) { fclose(f); free(buf); return NULL; }
	fclose(f);
	size_t size = (size_t)fsz;

	if (i2p_rd16(buf) != 0 || i2p_rd16(buf + 2) != 1) { free(buf); return NULL; }
	int count = (int)i2p_rd16(buf + 4);
	if (count < 1 || 6 + (size_t)count * 16 > size) { free(buf); return NULL; }

	int best = -1, bestW = -1, bestBits = -1;
	for (int i = 0; i < count; i++) {
		const uint8_t* e = buf + 6 + (size_t)i * 16;
		int w = e[0] ? e[0] : 256;
		int bits = (int)i2p_rd16(e + 6);
		if (w > bestW || (w == bestW && bits > bestBits)) { best = i; bestW = w; bestBits = bits; }
	}
	const uint8_t* e = buf + 6 + (size_t)best * 16;
	size_t sz = i2p_rd32(e + 8), off = i2p_rd32(e + 12);
	if (off > size || sz > size - off) { free(buf); return NULL; }
	*img = buf + off;
	*imgSize = sz;
	return buf;
}

static uint8_t* i2p_decode_image(const uint8_t* img, size_t imgSize, int* w, int* h) {
	if (imgSize >= 8 && memcmp(img, I2P_PNG_SIG, 8) == 0) return i2p_png_to_rgba(img, imgSize, w, h);
	return i2p_dib_to_rgba(img, imgSize, w, h);
}

/* ---------- thumbnail: ico -> png file on a transparent 16:10 canvas ---------- */

static inline int ico_to_png(const char* icoPath, const char* pngPath) {
	const uint8_t* img = NULL;
	size_t imgSize = 0;
	uint8_t* buf = i2p_open_ico(icoPath, &img, &imgSize);
	if (!buf) return 0;

	int isPng = (imgSize >= 8 && memcmp(img, I2P_PNG_SIG, 8) == 0);
	int w = 0, h = 0;
	uint8_t* rgba = i2p_decode_image(img, imgSize, &w, &h);

	int ok = 0;
	if (rgba) {
		int cw = w, ch = h;
		if ((long)w * 10 < (long)h * 16) cw = (h * 16 + 9) / 10;
		else ch = (w * 10 + 15) / 16;

		uint8_t* canvas = (uint8_t*)calloc((size_t)cw * ch, 4);
		if (canvas) {
			int x0 = (cw - w) / 2, y0 = (ch - h) / 2;
			for (int y = 0; y < h; y++)
				memcpy(canvas + ((size_t)(y + y0) * cw + x0) * 4, rgba + (size_t)y * w * 4, (size_t)w * 4);
			ok = i2p_write_png(pngPath, cw, ch, canvas);
			free(canvas);
		}
		free(rgba);
	}
	else if (isPng) {
		FILE* o = fopen(pngPath, "wb");
		if (o) { ok = (fwrite(img, 1, imgSize, o) == imgSize); fclose(o); }
	}
	free(buf);
	return ok;
}

/* ---------- favicon: ico -> small square png -> "data:image/png;base64,..." text ---------- */

static const char I2P_B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* returns a malloc'ed string (caller frees) or NULL. maxSize = biggest side in pixels, e.g. 64 */
static uint8_t* i2p_favicon_png(const char* icoPath, int maxSize, size_t* pngLen) {
	const uint8_t* img = NULL;
	size_t imgSize = 0;
	uint8_t* buf = i2p_open_ico(icoPath, &img, &imgSize);
	if (!buf) return NULL;

	int w = 0, h = 0;
	uint8_t* rgba = i2p_decode_image(img, imgSize, &w, &h);
	free(buf);
	if (!rgba) return NULL;

	/* shrink so the biggest side is maxSize (never enlarge) */
	int dw = w, dh = h;
	if (w > maxSize || h > maxSize) {
		if (w >= h) { dw = maxSize; dh = (h * maxSize + w / 2) / w; }
		else { dh = maxSize; dw = (w * maxSize + h / 2) / h; }
		if (dw < 1) dw = 1;
		if (dh < 1) dh = 1;
	}
	int side = dw > dh ? dw : dh;
	uint8_t* canvas = (uint8_t*)calloc((size_t)side * side, 4);
	if (!canvas) { free(rgba); return NULL; }
	int ox = (side - dw) / 2, oy = (side - dh) / 2;

	/* box filter, weighted by alpha so see-through edges don't get dark fringes */
	for (int dy = 0; dy < dh; dy++) {
		int sy0 = dy * h / dh, sy1 = (dy + 1) * h / dh;
		if (sy1 <= sy0) sy1 = sy0 + 1;
		for (int dx = 0; dx < dw; dx++) {
			int sx0 = dx * w / dw, sx1 = (dx + 1) * w / dw;
			if (sx1 <= sx0) sx1 = sx0 + 1;
			unsigned long long r = 0, g = 0, b = 0, a = 0, cnt = 0;
			for (int sy = sy0; sy < sy1; sy++)
				for (int sx = sx0; sx < sx1; sx++) {
					const uint8_t* p = rgba + ((size_t)sy * w + sx) * 4;
					r += (unsigned long long)p[0] * p[3];
					g += (unsigned long long)p[1] * p[3];
					b += (unsigned long long)p[2] * p[3];
					a += p[3]; cnt++;
				}
			uint8_t* o = canvas + ((size_t)(dy + oy) * side + (dx + ox)) * 4;
			if (a > 0) {
				o[0] = (uint8_t)(r / a); o[1] = (uint8_t)(g / a); o[2] = (uint8_t)(b / a);
				o[3] = (uint8_t)(a / cnt);
			}
		}
	}
	free(rgba);

	uint8_t* png = i2p_encode_png(side, side, canvas, pngLen);
	free(canvas);
	return png;
}

/* writes the small square icon (biggest side = maxSize) as a png file */
static int ico_to_favicon_png(const char* icoPath, const char* pngPath, int maxSize) {
	size_t n = 0;
	uint8_t* png = i2p_favicon_png(icoPath, maxSize, &n);
	if (!png) return 0;
	FILE* f = fopen(pngPath, "wb");
	if (!f) { free(png); return 0; }
	int ok = (fwrite(png, 1, n, f) == n);
	ok = (fclose(f) == 0) && ok;
	free(png);
	return ok;
}

/* the same icon as "data:image/png;base64,..." text (free() it), or NULL */
static inline char* ico_to_favicon_datauri(const char* icoPath, int maxSize) {
	size_t pngLen = 0;
	uint8_t* png = i2p_favicon_png(icoPath, maxSize, &pngLen);
	if (!png) return NULL;

	const char prefix[] = "data:image/png;base64,";
	size_t plen = sizeof(prefix) - 1;
	char* out = (char*)malloc(plen + 4 * ((pngLen + 2) / 3) + 1);
	if (!out) { free(png); return NULL; }
	memcpy(out, prefix, plen);
	size_t o = plen;
	for (size_t i = 0; i < pngLen; i += 3) {
		uint32_t v = (uint32_t)png[i] << 16;
		if (i + 1 < pngLen) v |= (uint32_t)png[i + 1] << 8;
		if (i + 2 < pngLen) v |= png[i + 2];
		out[o++] = I2P_B64[(v >> 18) & 63];
		out[o++] = I2P_B64[(v >> 12) & 63];
		out[o++] = (i + 1 < pngLen) ? I2P_B64[(v >> 6) & 63] : '=';
		out[o++] = (i + 2 < pngLen) ? I2P_B64[v & 63] : '=';
	}
	out[o] = '\0';
	free(png);
	return out;
}

#endif
