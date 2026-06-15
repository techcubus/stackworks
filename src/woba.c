#include "woba.h"
#include <stdlib.h>
#include <string.h>

/* ---- BMAP / WOBA decompression ----
 *
 * BMAP body layout (body = block + 12; all fields big-endian).
 * Offsets below are from the start of the body (= block + 12):
 *   +0x00  int32   filler (= 0)
 *   +0x04  int16×4 unknowns (third is usually 1)
 *   +0x0C  int16×4 card rect          (top, left, bottom, right)
 *   +0x14  int16×4 mask bounding rect
 *   +0x1C  int16×4 image bounding rect
 *   +0x24  int32   reserved (0)
 *   +0x28  int32   reserved (0)
 *   +0x2C  int32   mask data size
 *   +0x30  int32   image data size
 *   +0x34  byte[]  mask data (maskSize bytes), then image data (imageSize bytes)
 *
 * The compressed data uses WOBA (Wrath of Bill Atkinson), NOT PackBits.
 * The image bounding rect's left must be rounded down to a multiple of 32
 * and right rounded up before decompressing; row_bytes = (right-left)/8.
 *
 * WOBA opcode table (first byte of each instruction):
 *   0x00-0x7F  d=(byte>>4), z=(byte&0xF): write z zeros then d data bytes
 *   0x80       uncompressed row: row_bytes raw bytes follow
 *   0x81       white row (all 0x00)
 *   0x82       black row (all 0xFF)
 *   0x83 xx    row filled with byte xx; also sets last_byte[row%8]=xx
 *   0x84       row filled with last_byte[row%8]
 *              (last_byte initialised to {AA,55,AA,55,AA,55,AA,55})
 *   0x85       copy previous row
 *   0x86       copy row-2
 *   0x87       copy row-3
 *   0x88-0x8F  set (dh,dv): (16,0),(0,0),(0,1),(0,2),(1,0),(1,1),(2,2),(8,0)
 *   0x90-0x9F  unused
 *   0xA0-0xBF  101nnnnn: repeat next instruction n=(byte&0x1F) times
 *   0xC0-0xDF  110ddddd: write d=(byte&0x1F)*8 raw data bytes from stream
 *   0xE0-0xFF  111zzzzz: write z=(byte&0x1F)*16 zero bytes
 *
 * Rows filled via 0x80-0x87 bypass the diff transform.
 * All other rows: after the row buffer fills to row_bytes, apply:
 *   if dh: for i in [dh/8 .. row_bytes): row[i] ^= row[i - dh/8]
 *           (for dh=1 or 2: bit-level cumulative XOR)
 *   if dv: for i in [0 .. row_bytes): row[i] ^= row[row_num - dv][i]
 *
 * Reference: "The Definitive Guide to the HyperCard Stack File Format"
 * by Rebecca Bettencourt (2011), and Pierre Lorenzi's format notes.
 */

static uint16_t w_r16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}
static uint32_t w_r32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static int16_t w_r16s(const uint8_t *p) { return (int16_t)w_r16(p); }

/* Byte-level cumulative XOR for dh=8 or dh=16 (1-byte or 2-byte step). */
static void woba_xor_bytes(uint8_t *row, int row_bytes, int step) {
    for (int i = step; i < row_bytes; i++)
        row[i] ^= row[i - step];
}

/* Bit-level cumulative XOR for dh=1 or dh=2.
 * MSB-first bit layout: pixel 0 is bit 7 of byte 0.
 * For dh=1: each bit XOR'd with the previous bit (in pixel order).
 * For dh=2: each pair of bits XOR'd with the previous pair. */
static void woba_xor_bits(uint8_t *row, int row_bytes, int dh) {
    if (dh == 1) {
        uint8_t carry = 0; /* previous bit in bit-0 position */
        for (int i = 0; i < row_bytes; i++) {
            uint8_t b = row[i], out = 0;
            for (int k = 7; k >= 0; k--) {
                uint8_t bit = (b >> k) & 1;
                bit ^= carry;
                carry = bit;
                out |= (uint8_t)(bit << k);
            }
            row[i] = out;
        }
    } else { /* dh == 2 */
        uint8_t carry = 0; /* previous 2 bits in bits 1:0 */
        for (int i = 0; i < row_bytes; i++) {
            uint8_t b = row[i], out = 0;
            for (int k = 6; k >= 0; k -= 2) {
                uint8_t pair = (b >> k) & 3;
                pair ^= carry;
                carry = pair;
                out |= (uint8_t)(pair << k);
            }
            row[i] = out;
        }
    }
}

/* Apply differential transform to one completed row buffer.
 * prev[0] = most recent row, prev[1] = two rows back, prev[2] = three back. */
static void woba_diff(uint8_t *row, int row_bytes, int dh, int dv,
                      uint8_t prev[3][512/8]) {
    if (dh >= 8)
        woba_xor_bytes(row, row_bytes, dh / 8);
    else if (dh > 0)
        woba_xor_bits(row, row_bytes, dh);

    if (dv > 0 && dv <= 3)
        for (int i = 0; i < row_bytes; i++)
            row[i] ^= prev[dv - 1][i];
}

/* Decompress one WOBA plane into dst.
 * src/src_len: compressed stream.
 * bnd_*: bounding rect (left/right already rounded to nearest 32).
 * card_row_bytes: bytes per row in dst.
 * card_h: total rows in dst. */
static void woba_decode_plane(const uint8_t *src, uint32_t src_len,
                               int16_t bnd_top, int16_t bnd_left,
                               int16_t bnd_bottom, int16_t bnd_right,
                               uint8_t *dst, int card_row_bytes, int card_h) {
    int bnd_h         = bnd_bottom - bnd_top;
    int bnd_row_bytes = (bnd_right - bnd_left) / 8;
    int bnd_left_byte = bnd_left / 8;

    if (bnd_h <= 0 || bnd_row_bytes <= 0 || src_len == 0) return;

    /* Three-row history for dv (prev[0] = most recent completed row). */
    uint8_t prev[3][512 / 8];
    memset(prev, 0, sizeof prev);

    uint8_t last_byte[8] = {0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55};
    int dh = 0, dv = 0;

    uint8_t row_buf[512 / 8];
    memset(row_buf, 0, sizeof row_buf);
    int col     = 0; /* byte position within row_buf */
    int cur_row = 0; /* current row within the bounding rect */

    const uint8_t *p   = src;
    const uint8_t *end = src + src_len;

    /* Complete a partial-fill row: apply diff transform, write to dst,
     * push into the 3-slot history, reset the row buffer. */
#define FLUSH_ROW(bypass_diff) do {                                          \
    if (!(bypass_diff)) woba_diff(row_buf, bnd_row_bytes, dh, dv, prev);    \
    int abs_row = bnd_top + cur_row;                                         \
    if (abs_row >= 0 && abs_row < card_h) {                                  \
        uint8_t *dst_row = dst + abs_row * card_row_bytes + bnd_left_byte;   \
        memcpy(dst_row, row_buf, (size_t)bnd_row_bytes);                     \
    }                                                                        \
    memmove(prev[2], prev[1], (size_t)bnd_row_bytes);                       \
    memmove(prev[1], prev[0], (size_t)bnd_row_bytes);                       \
    memcpy (prev[0], row_buf, (size_t)bnd_row_bytes);                       \
    memset (row_buf, 0,       (size_t)bnd_row_bytes);                       \
    col = 0; cur_row++;                                                      \
} while (0)

    /* Execute one opcode (may be called once directly or n times via 0xA0-0xBF). */
#define EXEC_OPCODE(op) do {                                                  \
    uint8_t _op = (op);                                                       \
    if (_op <= 0x7F) {                                                        \
        /* partial row: z zero bytes, then d data bytes */                    \
        int _d = _op >> 4, _z = _op & 0xF;                                   \
        int _zz = (col + _z > bnd_row_bytes) ? bnd_row_bytes - col : _z;     \
        memset(row_buf + col, 0, (size_t)_zz); col += _z;                    \
        if (p + _d > end) goto done;                                          \
        int _dd = (col + _d > bnd_row_bytes) ? bnd_row_bytes - col : _d;     \
        memcpy(row_buf + col, p, (size_t)_dd); col += _d; p += _d;           \
        if (col >= bnd_row_bytes) FLUSH_ROW(0);                               \
    } else if (_op == 0x80) {                                                 \
        if (p + bnd_row_bytes > end) goto done;                               \
        memcpy(row_buf, p, (size_t)bnd_row_bytes); p += bnd_row_bytes;       \
        FLUSH_ROW(1);                                                         \
    } else if (_op == 0x81) {                                                 \
        memset(row_buf, 0x00, (size_t)bnd_row_bytes); FLUSH_ROW(1);          \
    } else if (_op == 0x82) {                                                 \
        memset(row_buf, 0xFF, (size_t)bnd_row_bytes); FLUSH_ROW(1);          \
    } else if (_op == 0x83) {                                                 \
        if (p >= end) goto done;                                              \
        uint8_t _b = *p++;                                                    \
        last_byte[cur_row % 8] = _b;                                          \
        memset(row_buf, _b, (size_t)bnd_row_bytes); FLUSH_ROW(1);            \
    } else if (_op == 0x84) {                                                 \
        memset(row_buf, last_byte[cur_row % 8], (size_t)bnd_row_bytes);      \
        FLUSH_ROW(1);                                                         \
    } else if (_op == 0x85) {                                                 \
        memcpy(row_buf, prev[0], (size_t)bnd_row_bytes); FLUSH_ROW(1);       \
    } else if (_op == 0x86) {                                                 \
        memcpy(row_buf, prev[1], (size_t)bnd_row_bytes); FLUSH_ROW(1);       \
    } else if (_op == 0x87) {                                                 \
        memcpy(row_buf, prev[2], (size_t)bnd_row_bytes); FLUSH_ROW(1);       \
    } else if (_op >= 0x88 && _op <= 0x8F) {                                 \
        static const int8_t _dh[8] = {16, 0, 0, 0, 1, 1, 2, 8};             \
        static const int8_t _dv[8] = { 0, 0, 1, 2, 0, 1, 2, 0};             \
        dh = _dh[_op - 0x88]; dv = _dv[_op - 0x88];                         \
    } else if (_op >= 0xC0 && _op <= 0xDF) {                                 \
        /* d*8 raw data bytes */                                              \
        int _d = (_op & 0x1F) * 8;                                           \
        if (p + _d > end) goto done;                                          \
        int _dd = (col + _d > bnd_row_bytes) ? bnd_row_bytes - col : _d;     \
        memcpy(row_buf + col, p, (size_t)_dd); col += _d; p += _d;           \
        if (col >= bnd_row_bytes) FLUSH_ROW(0);                               \
    } else if (_op >= 0xE0) {                                                 \
        /* z*16 zero bytes */                                                 \
        int _z = (_op & 0x1F) * 16;                                          \
        int _zz = (col + _z > bnd_row_bytes) ? bnd_row_bytes - col : _z;     \
        memset(row_buf + col, 0, (size_t)_zz); col += _z;                    \
        if (col >= bnd_row_bytes) FLUSH_ROW(0);                               \
    }                                                                         \
} while (0)

    while (p < end && cur_row < bnd_h) {
        uint8_t op = *p++;
        if (op >= 0xA0 && op <= 0xBF) {
            int n = op & 0x1F;
            if (p >= end) goto done;
            uint8_t next = *p++;
            for (int i = 0; i < n && cur_row < bnd_h; i++)
                EXEC_OPCODE(next);
        } else {
            EXEC_OPCODE(op);
        }
    }
done:
#undef FLUSH_ROW
#undef EXEC_OPCODE
    return;
}

uint8_t *bmap_decompress(const uint8_t *body, uint32_t body_size,
                          uint16_t card_w, uint16_t card_h) {
    if (body_size < 0x34) return NULL;

    /* image bounding rect at body+0x1C; card rect at body+0x0C */
    int16_t img_top    = w_r16s(body + 0x1C);
    int16_t img_left   = w_r16s(body + 0x1E);
    int16_t img_bottom = w_r16s(body + 0x20);
    int16_t img_right  = w_r16s(body + 0x22);

    /* round bounding rect left down and right up to nearest 32 pixels */
    int16_t left_r  = (int16_t)((img_left  / 32) * 32);
    int16_t right_r = (int16_t)(((img_right + 31) / 32) * 32);

    uint32_t mask_size  = w_r32(body + 0x2C);
    uint32_t image_size = w_r32(body + 0x30);

    if ((uint64_t)0x34 + mask_size + image_size > body_size) return NULL;

    int card_row_bytes = (card_w + 7) / 8;
    uint8_t *out = calloc((size_t)card_h, (size_t)card_row_bytes);
    if (!out) return NULL;

    const uint8_t *image_data = body + 0x34 + mask_size;
    woba_decode_plane(image_data, image_size,
                      img_top, left_r, img_bottom, right_r,
                      out, card_row_bytes, (int)card_h);
    return out;
}
