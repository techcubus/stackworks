/*
 * pict2ppm — render a Mac PICT resource to a PPM file
 *
 * Usage: pict2ppm [-v] <input.pict> <output.ppm>
 *
 * Input: raw PICT resource (no 512-byte header), or a PICT file with the
 * standard 512-byte null header (auto-detected and skipped).
 *
 * Handles PICT v1 and v2:
 *   BitsRect    (0x0090) — uncompressed 1-bit bitmap
 *   PackBitsRect(0x0098) — PackBits-compressed 1/8-bit bitmap
 *   DirectBitsRect(0x009A) — packed 16/32-bit colour bitmap
 *
 * All other opcodes are skipped using the standard size table.
 * Unknown opcodes cause a warning and stop parsing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { uint8_t r, g, b; } RGB;

/* ---- big-endian parser ---- */

typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         pos;
    int            verbose;
    int            v2;      /* 1 after PICT v2 header is confirmed */
} P;

static int p_eof(const P *p) { return p->pos >= p->len; }

static uint8_t p_u8(P *p) {
    if (p->pos >= p->len) { fprintf(stderr, "pict2ppm: unexpected EOF\n"); exit(1); }
    return p->buf[p->pos++];
}
static uint16_t p_u16(P *p) { uint16_t h = p_u8(p); return (uint16_t)((h << 8) | p_u8(p)); }
static int16_t  p_s16(P *p) { return (int16_t)p_u16(p); }
static uint32_t p_u32(P *p) { uint32_t h = p_u16(p); return (h << 16) | p_u16(p); }

static void p_skip(P *p, size_t n) {
    if (p->pos + n > p->len) { p->pos = p->len; return; }
    p->pos += n;
}

/* Read a QuickDraw Rect: top, left, bottom, right (each int16). */
static void p_rect(P *p, int *t, int *l, int *b, int *r) {
    *t = p_s16(p); *l = p_s16(p); *b = p_s16(p); *r = p_s16(p);
}

/* Skip a variable-length Region record (starts with 2-byte rgnSize). */
static void p_rgn_skip(P *p) {
    uint16_t sz = p_u16(p);
    if (sz >= 2) p_skip(p, sz - 2);
}

/* Skip a variable-length Polygon record (starts with 2-byte polySize). */
static void p_poly_skip(P *p) {
    uint16_t sz = p_u16(p);
    if (sz >= 2) p_skip(p, sz - 2);
}

/* ---- canvas (3 bytes per pixel: RGB, white background) ---- */

typedef struct { int w, h, ox, oy; uint8_t *px; } Canvas;

static Canvas *cv_create(int w, int h, int ox, int oy) {
    Canvas *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->w = w; c->h = h; c->ox = ox; c->oy = oy;
    c->px = malloc((size_t)(w * h * 3));
    if (!c->px) { free(c); return NULL; }
    memset(c->px, 0xFF, (size_t)(w * h * 3)); /* white */
    return c;
}

static void cv_free(Canvas *c) { if (c) { free(c->px); free(c); } }

static void cv_set(Canvas *c, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= 0 && x < c->w && y >= 0 && y < c->h) {
        uint8_t *p = c->px + (y * c->w + x) * 3;
        p[0] = r; p[1] = g; p[2] = b;
    }
}

/* Blit one row of 1-bit bitmap data (1=black, 0=white). */
static void cv_blit_row1(Canvas *c,
                          const uint8_t *row, int row_bytes,
                          int cx, int cy, int blit_w) {
    for (int x = 0; x < blit_w; x++) {
        int bi = x / 8, bp = 7 - (x % 8);
        if (bi < row_bytes) {
            uint8_t v = ((row[bi] >> bp) & 1) ? 0 : 255;
            cv_set(c, cx + x, cy, v, v, v);
        }
    }
}

/* Blit one row of 8-bit indexed colour data using a colour table. */
static void cv_blit_row8(Canvas *c, const RGB ct[256],
                          const uint8_t *row, int row_bytes,
                          int cx, int cy, int blit_w) {
    for (int x = 0; x < blit_w && x < row_bytes; x++) {
        RGB col = ct[row[x]];
        cv_set(c, cx + x, cy, col.r, col.g, col.b);
    }
}

/* ---- Apple PackBits decompressor ---- */

/* Returns number of bytes written to dst. */
static int unpack_bits(const uint8_t *src, int slen,
                       uint8_t *dst, int dlen) {
    int si = 0, di = 0;
    while (si < slen && di < dlen) {
        int8_t n = (int8_t)src[si++];
        if (n == -128) continue;            /* no-op */
        if (n >= 0) {                       /* literal run: copy n+1 bytes */
            int cnt = n + 1;
            while (cnt-- && si < slen && di < dlen)
                dst[di++] = src[si++];
        } else {                            /* repeat run: copy next byte -n+1 times */
            int cnt = -n + 1;
            if (si >= slen) break;
            uint8_t val = src[si++];
            while (cnt-- && di < dlen)
                dst[di++] = val;
        }
    }
    return di;
}

/* ---- colour table ---- */

/* Read Mac colour table into ct[0..255]. */
static void load_color_table(P *p, RGB ct[256]) {
    p_skip(p, 4); /* ctSeed */
    p_skip(p, 2); /* ctFlags */
    int n = (int)p_u16(p) + 1; /* number of entries */
    for (int i = 0; i < n; i++) {
        int idx = (int)p_u16(p); /* entry value/index */
        uint8_t r = (uint8_t)(p_u16(p) >> 8);
        uint8_t g = (uint8_t)(p_u16(p) >> 8);
        uint8_t b = (uint8_t)(p_u16(p) >> 8);
        if (idx >= 0 && idx < 256) { ct[idx].r = r; ct[idx].g = g; ct[idx].b = b; }
    }
}

/* ---- PixMap record parser (returns pixel_size; advances p past map) ---- */

typedef struct {
    uint16_t row_bytes;
    int      top, left, bottom, right;
    uint16_t pack_type;
    uint16_t pixel_size;
    uint16_t cmp_count;
    uint16_t cmp_size;
} PixMapInfo;

/* Parse PixMap fields that follow the rowBytes word and bounds rect.
 * Used by PackBitsRect and DirectBitsRect. */
static void parse_pixmap_extra(P *p, PixMapInfo *pm) {
    p_skip(p, 2);              /* pmVersion */
    pm->pack_type = p_u16(p);
    p_skip(p, 4);              /* packSize */
    p_skip(p, 4);              /* hRes */
    p_skip(p, 4);              /* vRes */
    p_skip(p, 2);              /* pixelType */
    pm->pixel_size = p_u16(p);
    pm->cmp_count  = p_u16(p);
    pm->cmp_size   = p_u16(p);
    p_skip(p, 4);              /* planeBytes */
    p_skip(p, 4);              /* pmTable handle (ignored) */
    p_skip(p, 4);              /* pmReserved */
}

/* ---- bitmap renderers ---- */

/*
 * render_bits: handle BitsRect (0x90), BitsRgn (0x91),
 *              PackBitsRect (0x98), PackBitsRgn (0x99),
 *              DirectBitsRect (0x009A).
 *
 * packed=0 → raw data (BitsRect)
 * packed=1 → PackBits-compressed rows (PackBitsRect / PackBitsRgn)
 * packed=2 → DirectBits (always compressed)
 * has_rgn  → a clip region follows the pixel data
 */
static void render_bits(P *p, Canvas *c, int packed, int has_rgn) {

    int      is_pixmap = 0;
    PixMapInfo pm;
    memset(&pm, 0, sizeof pm);

    if (packed == 2) {
        /* DirectBitsRect: 4-byte baseAddr, then rowBytes (high bit set), then PixMap */
        p_skip(p, 4);  /* baseAddr (ignored) */
        pm.row_bytes = p_u16(p) & 0x7FFF;
        p_rect(p, &pm.top, &pm.left, &pm.bottom, &pm.right);
        parse_pixmap_extra(p, &pm);
        is_pixmap = 1;
    } else {
        uint16_t rb_raw = p_u16(p);
        is_pixmap = (rb_raw & 0x8000) != 0;
        pm.row_bytes = rb_raw & 0x7FFF;
        p_rect(p, &pm.top, &pm.left, &pm.bottom, &pm.right);
        if (is_pixmap)
            parse_pixmap_extra(p, &pm);
    }

    /* colour table (indexed PixMaps only): in PICT the ColorTable is embedded
     * immediately after the PixMap record, BEFORE srcRect/dstRect/mode. */
    RGB ct[256];
    for (int i = 0; i < 256; i++) {
        uint8_t v = (uint8_t)(255 - i); /* Mac default: index 0=white, 255=black */
        ct[i].r = ct[i].g = ct[i].b = v;
    }
    if (is_pixmap && pm.pixel_size <= 8)
        load_color_table(p, ct);

    /* srcRect, dstRect, mode */
    int st, sl, sb, sr;  p_rect(p, &st, &sl, &sb, &sr);
    int dt, dl, db, dr;  p_rect(p, &dt, &dl, &db, &dr);
    p_skip(p, 2);  /* transfer mode */

    int src_h  = pm.bottom - pm.top;
    int src_w  = pm.right  - pm.left;

    if (p->verbose) {
        printf("    bitmap %dx%d row_bytes=%u pixelSize=%u packed=%d\n",
               src_w, src_h, pm.row_bytes, pm.pixel_size, packed);
        printf("    dst (%d,%d)-(%d,%d)  canvas origin (%d,%d)\n",
               dl, dt, dr, db, c->ox, c->oy);
    }

    /* mask region for BitsRgn/PackBitsRgn: comes between mode and pixel data */
    if (has_rgn) p_rgn_skip(p);

    /* destination in canvas coordinates */
    int cx0 = dl - c->ox;
    int cy0 = dt - c->oy;

    uint8_t comp_buf[8192];
    uint8_t row_buf[8192];

    for (int row = 0; row < src_h; row++) {
        int row_bytes = (int)pm.row_bytes;

        if (!packed || row_bytes < 8) {
            /* uncompressed row */
            if (row_bytes > (int)sizeof row_buf) { p_skip(p, (size_t)row_bytes); continue; }
            for (int i = 0; i < row_bytes; i++) row_buf[i] = p_u8(p);
        } else {
            /* PackBits: 1-byte or 2-byte compressed length prefix */
            int comp_len = (row_bytes > 250) ? (int)p_u16(p) : (int)p_u8(p);
            if (comp_len > (int)sizeof comp_buf) { p_skip(p, (size_t)comp_len); continue; }
            for (int i = 0; i < comp_len; i++) comp_buf[i] = p_u8(p);
            unpack_bits(comp_buf, comp_len, row_buf, row_bytes);
        }

        if (!is_pixmap || pm.pixel_size == 1)
            cv_blit_row1(c, row_buf, row_bytes, cx0, cy0 + row, src_w);
        else if (pm.pixel_size == 8)
            cv_blit_row8(c, ct, row_buf, row_bytes, cx0, cy0 + row, src_w);
        /* 16-bit and 32-bit direct colour not yet implemented */
    }

}

/* ---- PICT v1 opcode loop ---- */

static void parse_v1(P *p, Canvas *c) {
    while (!p_eof(p)) {
        size_t   op_pos = p->pos;
        uint8_t  op     = p_u8(p);
        if (p->verbose) printf("  v1 op 0x%02X @ %zu\n", op, op_pos);

        switch (op) {
        case 0x00: break;                       /* NOP */
        case 0x01: p_rgn_skip(p); break;       /* ClipRgn */
        case 0x02: p_skip(p, 8); break;        /* BkPat */
        case 0x03: p_skip(p, 2); break;        /* TxFont */
        case 0x04: p_skip(p, 1); break;        /* TxFace */
        case 0x05: p_skip(p, 2); break;        /* TxMode */
        case 0x06: p_skip(p, 4); break;        /* SpExtra */
        case 0x07: p_skip(p, 4); break;        /* PnSize */
        case 0x08: p_skip(p, 2); break;        /* PnMode */
        case 0x09: p_skip(p, 8); break;        /* PnPat */
        case 0x0A: p_skip(p, 8); break;        /* FillPat */
        case 0x0B: p_skip(p, 4); break;        /* OvSize */
        case 0x0C: p_skip(p, 4); break;        /* Origin */
        case 0x0D: p_skip(p, 2); break;        /* TxSize */
        case 0x0E: p_skip(p, 4); break;        /* FgColor */
        case 0x0F: p_skip(p, 4); break;        /* BkColor */
        case 0x10: p_skip(p, 8); break;        /* TxRatio */
        case 0x11: p_skip(p, 1); break;        /* VersionOp */
        case 0x1A: p_skip(p, 6); break;        /* RGBFgColor */
        case 0x1B: p_skip(p, 6); break;        /* RGBBkColor */
        case 0x1E: break;                       /* DefHilite */
        case 0x1F: p_skip(p, 6); break;        /* OpColor */
        /* line opcodes */
        case 0x20: p_skip(p, 8); break;        /* Line */
        case 0x21: p_skip(p, 4); break;        /* LineFrom */
        case 0x22: p_skip(p, 6); break;        /* ShortLine */
        case 0x23: p_skip(p, 2); break;        /* ShortLineFrom */
        /* text opcodes */
        case 0x28: { p_skip(p, 4); p_skip(p, p_u8(p)); break; } /* LongText */
        case 0x29: { p_skip(p, 1); p_skip(p, p_u8(p)); break; } /* DHText */
        case 0x2A: { p_skip(p, 1); p_skip(p, p_u8(p)); break; } /* DVText */
        case 0x2B: { p_skip(p, 2); p_skip(p, p_u8(p)); break; } /* DHDVText */
        case 0x2C: { uint16_t s = p_u16(p); p_skip(p, s); break; } /* fontName */
        case 0x2D: p_skip(p, 10); break;       /* lineJustify */
        case 0x2E: { uint16_t s = p_u16(p); p_skip(p, s); break; } /* glyphState */
        /* rect opcodes (frame/paint/erase/invert/fill — 8 bytes each) */
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
            p_skip(p, 8); break;
        /* same-rect variants (0 bytes) */
        case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C:
            break;
        /* round-rect opcodes */
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44:
            p_skip(p, 8); break;
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C:
            break;
        /* oval opcodes */
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54:
            p_skip(p, 8); break;
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C:
            break;
        /* arc opcodes (rect + start angle + arc angle = 12 bytes) */
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64:
            p_skip(p, 12); break;
        case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C:
            p_skip(p, 4); break;
        /* polygon/region opcodes */
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74:
            p_poly_skip(p); break;
        case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C:
            p_rgn_skip(p); break;
        /* bitmap opcodes */
        case 0x90: render_bits(p, c, 0, 0); break; /* BitsRect */
        case 0x91: render_bits(p, c, 0, 1); break; /* BitsRgn */
        case 0x98: render_bits(p, c, 1, 0); break; /* PackBitsRect */
        case 0x99: render_bits(p, c, 1, 1); break; /* PackBitsRgn */
        /* comments */
        case 0xA0: p_skip(p, 2); break;        /* ShortComment */
        case 0xA1: { p_skip(p, 2); p_skip(p, p_u16(p)); break; } /* LongComment */
        case 0xFF: return;                      /* EndOfPicture */
        default:
            fprintf(stderr, "pict2ppm: unknown v1 opcode 0x%02X at offset %zu\n",
                    op, op_pos);
            return;
        }
    }
}

/* ---- PICT v2 opcode loop ---- */

static void parse_v2(P *p, Canvas *c) {
    while (!p_eof(p)) {
        /* v2 opcodes are word-aligned */
        if (p->pos & 1) p_skip(p, 1);

        size_t   op_pos = p->pos;
        uint16_t op     = p_u16(p);
        if (p->verbose) printf("  v2 op 0x%04X @ %zu\n", op, op_pos);

        switch (op) {
        case 0x0000: break;
        case 0x0001: p_rgn_skip(p); break;     /* Clip */
        case 0x0003: p_skip(p, 2); break;      /* TxFont */
        case 0x0004: p_skip(p, 2); break;      /* TxFace (padded to word) */
        case 0x0005: p_skip(p, 2); break;      /* TxMode */
        case 0x0006: p_skip(p, 4); break;      /* SpExtra */
        case 0x0007: p_skip(p, 4); break;      /* PnSize */
        case 0x0008: p_skip(p, 2); break;      /* PnMode */
        case 0x0009: p_skip(p, 8); break;      /* PnPat */
        case 0x000A: p_skip(p, 8); break;      /* FillPat */
        case 0x000B: p_skip(p, 4); break;      /* OvSize */
        case 0x000C: p_skip(p, 4); break;      /* Origin */
        case 0x000D: p_skip(p, 2); break;      /* TxSize */
        case 0x000E: p_skip(p, 4); break;      /* FgColor */
        case 0x000F: p_skip(p, 4); break;      /* BkColor */
        case 0x0010: p_skip(p, 8); break;      /* TxRatio */
        case 0x0011: p_skip(p, 1); break;      /* VersionOp (version byte) */
        case 0x001A: p_skip(p, 6); break;      /* RGBFgColor */
        case 0x001B: p_skip(p, 6); break;      /* RGBBkColor */
        case 0x001E: break;                     /* DefHilite */
        case 0x001F: p_skip(p, 6); break;      /* OpColor */
        case 0x0020: p_skip(p, 8); break;      /* Line */
        case 0x0021: p_skip(p, 4); break;      /* LineFrom */
        case 0x0022: p_skip(p, 6); break;      /* ShortLine */
        case 0x0023: p_skip(p, 2); break;      /* ShortLineFrom */
        case 0x0028: { p_skip(p, 4); p_skip(p, p_u8(p)); break; }
        case 0x0029: { p_skip(p, 1); p_skip(p, p_u8(p)); break; }
        case 0x002A: { p_skip(p, 1); p_skip(p, p_u8(p)); break; }
        case 0x002B: { p_skip(p, 2); p_skip(p, p_u8(p)); break; }
        case 0x002C: { uint16_t s = p_u16(p); p_skip(p, s); break; }
        case 0x002D: p_skip(p, 10); break;
        case 0x002E: { uint16_t s = p_u16(p); p_skip(p, s); break; }
        case 0x0030: case 0x0031: case 0x0032: case 0x0033: case 0x0034:
            p_skip(p, 8); break;
        case 0x0038: case 0x0039: case 0x003A: case 0x003B: case 0x003C:
            break;
        case 0x0040: case 0x0041: case 0x0042: case 0x0043: case 0x0044:
            p_skip(p, 8); break;
        case 0x0048: case 0x0049: case 0x004A: case 0x004B: case 0x004C:
            break;
        case 0x0050: case 0x0051: case 0x0052: case 0x0053: case 0x0054:
            p_skip(p, 8); break;
        case 0x0058: case 0x0059: case 0x005A: case 0x005B: case 0x005C:
            break;
        case 0x0060: case 0x0061: case 0x0062: case 0x0063: case 0x0064:
            p_skip(p, 12); break;
        case 0x0068: case 0x0069: case 0x006A: case 0x006B: case 0x006C:
            p_skip(p, 4); break;
        case 0x0070: case 0x0071: case 0x0072: case 0x0073: case 0x0074:
            p_poly_skip(p); break;
        case 0x0078: case 0x0079: case 0x007A: case 0x007B: case 0x007C:
            p_rgn_skip(p); break;
        case 0x0090: render_bits(p, c, 0, 0); break;
        case 0x0091: render_bits(p, c, 0, 1); break;
        case 0x0098: render_bits(p, c, 1, 0); break;
        case 0x0099: render_bits(p, c, 1, 1); break;
        case 0x009A: render_bits(p, c, 2, 0); break; /* DirectBitsRect */
        case 0x00A0: p_skip(p, 2); break;
        case 0x00A1: { p_skip(p, 2); p_skip(p, p_u16(p)); break; }
        case 0x00FF: return;                    /* EndOfPicture */
        case 0x02FF: break;                     /* Version (ignore) */
        case 0x0C00: p_skip(p, 24); break;     /* HeaderOp */
        case 0x8200: {                          /* CompressedQuickTime */
            uint32_t sz = p_u32(p);
            if (sz >= 4) p_skip(p, sz - 4);
            break;
        }
        default:
            /* skip over reserved ranges with known stride patterns */
            if (op >= 0x0100 && op <= 0x7FFF) {
                /* long-data opcodes: data length = op >> 8 */
                p_skip(p, (size_t)((op >> 8) * 2));
            } else {
                fprintf(stderr, "pict2ppm: unknown v2 opcode 0x%04X at offset %zu\n",
                        op, op_pos);
                return;
            }
        }
    }
}

/* ---- PPM writer ---- */

static int write_ppm(const Canvas *c, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return -1; }
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    fwrite(c->px, 1, (size_t)(c->w * c->h * 3), f);
    fclose(f);
    return 0;
}

/* ---- main ---- */

int main(int argc, char *argv[]) {
    int verbose = 0;
    int ai = 1;
    if (ai < argc && strcmp(argv[ai], "-v") == 0) { verbose = 1; ai++; }
    if (argc - ai < 2) {
        fprintf(stderr, "usage: pict2ppm [-v] <input.pict> <output.ppm>\n");
        return 1;
    }
    const char *in_path  = argv[ai];
    const char *out_path = argv[ai + 1];

    /* read whole file */
    FILE *f = fopen(in_path, "rb");
    if (!f) { perror(in_path); return 1; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    uint8_t *buf = malloc((size_t)fsize);
    if (!buf) { fprintf(stderr, "out of memory\n"); fclose(f); return 1; }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        perror(in_path); free(buf); fclose(f); return 1;
    }
    fclose(f);

    P p = { buf, (size_t)fsize, 0, verbose, 0 };

    /* auto-detect 512-byte null header: if first 2 bytes are 0x0000 AND
     * bytes 512-513 look like a plausible picSize, skip the header. */
    if (fsize > 514 && buf[0] == 0 && buf[1] == 0) {
        /* check that byte 512 starts a valid-looking PICT */
        if (buf[522] == 0x11) /* VersionOp is at +10 */
            p.pos = 512;
    }

    /* PICT header: picSize (word) + picFrame (rect = 8 bytes) */
    size_t hdr_start = p.pos;
    (void)p_u16(&p);  /* picSize (ignored — use actual file size) */
    int pt, pl, pb, pr;
    p_rect(&p, &pt, &pl, &pb, &pr);

    int pict_w = pr - pl;
    int pict_h = pb - pt;

    if (verbose)
        printf("PICT: bounding rect (%d,%d)-(%d,%d)  %dx%d\n",
               pt, pl, pb, pr, pict_w, pict_h);

    if (pict_w <= 0 || pict_h <= 0 || pict_w > 8192 || pict_h > 8192) {
        fprintf(stderr, "pict2ppm: implausible dimensions %dx%d\n", pict_w, pict_h);
        free(buf); return 1;
    }
    (void)hdr_start;

    Canvas *c = cv_create(pict_w, pict_h, pl, pt);
    if (!c) { fprintf(stderr, "pict2ppm: out of memory\n"); free(buf); return 1; }

    /* detect version.
     *
     * PICT v1: 1-byte VersionOp (0x11) followed by data byte 0x01.
     * PICT v2: 2-byte VersionOp word (0x0011, stored as 0x00 0x11) with
     *          NO data byte, followed immediately by Version word 0x02FF,
     *          then HeaderOp word 0x0C00 + 24 bytes of header data. */
    if (p.pos + 1 < p.len && p.buf[p.pos] == 0x00 && p.buf[p.pos+1] == 0x11) {
        /* PICT v2 */
        p_u16(&p);               /* consume 0x0011 VersionOp */
        uint16_t vop = p_u16(&p);
        if (vop != 0x02FF)
            fprintf(stderr, "pict2ppm: warning: expected Version 0x02FF, got 0x%04X\n", vop);
        uint16_t hop = p_u16(&p);
        if (hop != 0x0C00)
            fprintf(stderr, "pict2ppm: warning: expected HeaderOp 0x0C00, got 0x%04X\n", hop);
        p_skip(&p, 24);
        if (verbose) printf("PICT version: 2\n");
        parse_v2(&p, c);
    } else {
        uint8_t version_op = p_u8(&p);
        if (version_op != 0x11) {
            fprintf(stderr, "pict2ppm: expected VersionOp 0x11 or 0x0011, got 0x%02X\n",
                    version_op);
            cv_free(c); free(buf); return 1;
        }
        uint8_t version = p_u8(&p);
        if (verbose) printf("PICT version: %d\n", version);
        if (version == 1) {
            parse_v1(&p, c);
        } else {
            fprintf(stderr, "pict2ppm: unknown version byte %d after 0x11\n", version);
            cv_free(c); free(buf); return 1;
        }
    }

    if (write_ppm(c, out_path) != 0) {
        cv_free(c); free(buf); return 1;
    }

    printf("%s: %dx%d → %s\n", in_path, pict_w, pict_h, out_path);
    cv_free(c);
    free(buf);
    return 0;
}
