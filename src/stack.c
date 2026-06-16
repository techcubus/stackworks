#include "stack.h"
#include "woba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- big-endian readers ---- */

static uint16_t r16(const uint8_t *p) {
    return (uint16_t)p[0] << 8 | p[1];
}
static uint32_t r32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static int32_t r32s(const uint8_t *p) { return (int32_t)r32(p); }
static int16_t r16s(const uint8_t *p) { return (int16_t)r16(p); }

/* ---- block index ---- */

typedef struct {
    uint32_t type;
    int32_t  id;
    uint32_t offset; /* from start of file */
    uint32_t size;
} BlockEntry;

typedef struct {
    const uint8_t *data;
    size_t         size;
    BlockEntry    *blocks;
    uint32_t       block_count;
    uint32_t       block_cap;
} FileCtx;

static int ctx_add(FileCtx *ctx, uint32_t type, int32_t id,
                   uint32_t offset, uint32_t size) {
    if (ctx->block_count == ctx->block_cap) {
        uint32_t cap = ctx->block_cap ? ctx->block_cap * 2 : 64;
        BlockEntry *b = realloc(ctx->blocks, cap * sizeof *b);
        if (!b) return -1;
        ctx->blocks = b;
        ctx->block_cap = cap;
    }
    BlockEntry *e = &ctx->blocks[ctx->block_count++];
    e->type = type; e->id = id; e->offset = offset; e->size = size;
    return 0;
}

static const BlockEntry *ctx_find(const FileCtx *ctx, uint32_t type, int32_t id) {
    for (uint32_t i = 0; i < ctx->block_count; i++)
        if (ctx->blocks[i].type == type && ctx->blocks[i].id == id)
            return &ctx->blocks[i];
    return NULL;
}

/* pointer to block body (past the 12-byte header) */
static const uint8_t *blkbody(const FileCtx *ctx, const BlockEntry *e) {
    return ctx->data + e->offset + 12;
}


/* ---- Part parsing ----
 *
 * Part record layout (confirmed from Practice and MacDungeonMaster stacks):
 *   +0x00  uint16  record size (includes this field)
 *   +0x02  uint16  part id
 *   +0x04  uint8   type (1=button, 2=field)
 *   +0x05  uint8   flags (bit7=hidden)
 *   +0x06  int16   rect.top
 *   +0x08  int16   rect.left
 *   +0x0A  int16   rect.bottom
 *   +0x0C  int16   rect.right
 *   +0x0E  uint8   style
 *   +0x0F  uint8   flags2
 *   +0x10  uint8[14] text properties (font, size, align, style)
 *   +0x1E  char[]  name (null-terminated)
 *   then   char[]  script (null-terminated)
 */
#define PART_HDR_MIN 0x1E

static Part *parse_parts(const uint8_t *data, uint32_t avail, uint16_t count,
                          uint32_t *bytes_consumed) {
    Part *parts = calloc(count, sizeof *parts);
    if (!parts) { if (bytes_consumed) *bytes_consumed = 0; return NULL; }

    const uint8_t *p = data;
    const uint8_t *end = data + avail;

    for (uint16_t i = 0; i < count; i++) {
        if (p + 4 > end) break;
        uint16_t rec_size = r16(p);
        if (rec_size < PART_HDR_MIN || p + rec_size > end) break;

        parts[i].id      = r16(p + 0x02);
        parts[i].type    = p[0x04];
        parts[i].visible = !(p[0x05] & 0x80);
        parts[i].rect.top    = r16s(p + 0x06);
        parts[i].rect.left   = r16s(p + 0x08);
        parts[i].rect.bottom = r16s(p + 0x0A);
        parts[i].rect.right  = r16s(p + 0x0C);
        /* Buttons: style in upper nibble of +0x0E. Fields: style in +0x0F. */
        if (parts[i].type == PART_BUTTON)
            parts[i].style = (p[0x0E] >> 4) & 0x0F;
        else
            parts[i].style = p[0x0F];

        /* font attributes — only present when record is large enough */
        if (rec_size > 0x1A) {
            parts[i].text_align  = (uint8_t)(r16(p + 0x10) & 0xFF);
            parts[i].font_id     = r16(p + 0x16);
            parts[i].text_size   = r16(p + 0x18);
            parts[i].text_style  = p[0x14];
        }

        if (rec_size > PART_HDR_MIN) {
            const char *name = (const char *)(p + PART_HDR_MIN);
            size_t max = rec_size - PART_HDR_MIN;
            size_t nlen = strnlen(name, max);
            parts[i].name = nlen < max ? strndup(name, nlen) : NULL;
        }

        p += rec_size;
    }
    if (bytes_consumed) *bytes_consumed = (uint32_t)(p - data);
    return parts;
}

/* Parse the field content section that follows card-layer part records.
 * Layout: uint16 entry_count, uint16 unknown, uint16 total_size,
 *         then entries: uint16 part_id, uint16 text_size, uint8 style_flag,
 *                       char[text_size] text (Mac Roman, \r-separated, null-term).
 * style_flag 0x00 = plain; non-zero = styled text with embedded run table (skipped). */
static uint16_t parse_card_content(const uint8_t *p, uint32_t avail,
                                    FieldContent **out) {
    *out = NULL;
    if (avail < 6) return 0;

    uint16_t entry_count = r16(p);
    /* p[2-3] = unknown */
    uint16_t total_size  = r16(p + 4);

    if (entry_count == 0 || total_size == 0) return 0;
    if (avail < (uint32_t)(6 + total_size)) return 0;

    FieldContent *fc = calloc(entry_count, sizeof *fc);
    if (!fc) return 0;

    const uint8_t *ep  = p + 6;
    const uint8_t *end = ep + total_size;
    uint16_t count = 0;

    for (uint16_t i = 0; i < entry_count && ep + 5 <= end; i++) {
        uint16_t part_id   = r16(ep);
        uint16_t text_size = r16(ep + 2);
        uint8_t  style     = ep[4];
        ep += 5;

        if (ep + text_size > end) break;

        fc[count].part_id = part_id;
        if (style == 0x00 && text_size > 0) {
            size_t len = strnlen((const char *)ep, text_size);
            fc[count].text = strndup((const char *)ep, len);
        }
        /* style != 0: styled text (embedded run table before text); skip */

        ep += text_size;
        /* Each entry's null terminator doubles as the 0x00 high byte of the
         * next entry's part_id (part IDs always fit in a byte).  Back up by
         * one so the next iteration reads the full uint16 correctly. */
        if (ep > p + 6) ep--;
        count++;
    }

    *out = fc;
    return count;
}

/* ---- CARD / BKGD body layout ----
 *
 * Both share a common prefix:
 *   +0x00  int32   reserved (0)
 *   +0x04  int32   bmap_id  — BMAP block to composite for this card/background
 *   +0x08  uint32  unknown (flags?)
 *   +0x0C  uint32  unknown
 *   +0x10  int32   unknown linked-list id
 *   +0x14  int32   unknown linked-list id
 *
 * CARD continues:
 *   +0x18  int32   bkgd_id
 *   +0x1C  uint16  part_count
 *   +0x1E  uint16  part_data_size  (bytes of field content after part records)
 *   +0x20  uint32  script_size
 *   +0x24  part records (variable)
 *   then   part data
 *   then   card script
 *
 * BKGD continues (no bkgd_id field):
 *   +0x18  uint16  part_count
 *   +0x1A  uint16  part_data_size
 *   +0x1C  uint32  script_size
 *   +0x20  uint8[6] unknown (possibly includes background name)
 *   +0x26  part records (variable)
 *   then   part data
 *   then   background script
 *
 * Confirmed from hex dump of Practice.hc and MacDungeonMaster.hc.
 * Note: BKGD_OFF_PARTS=0x26 holds for empty-named backgrounds; a named
 * background may push parts later. If rec_size at 0x26 reads as 0, skip
 * forward 2 bytes (see parse_bkgd).
 */

/* offsets in CARD body */
#define CARD_OFF_BMAP_ID   0x04
#define CARD_OFF_BKGD_ID   0x18
#define CARD_OFF_PART_CNT  0x1C
#define CARD_OFF_PDATA_SZ  0x1E
#define CARD_OFF_SCRIPT_SZ 0x20
#define CARD_OFF_PARTS     0x24

/* offsets in BKGD body */
#define BKGD_OFF_BMAP_ID   0x04
#define BKGD_OFF_PART_CNT  0x18
#define BKGD_OFF_PDATA_SZ  0x1A
#define BKGD_OFF_SCRIPT_SZ 0x1C
#define BKGD_OFF_PARTS     0x26

static char *extract_script(const uint8_t *base, uint32_t body_size,
                             uint32_t parts_start, uint16_t part_count,
                             uint16_t pdata_size, uint32_t script_size) {
    if (script_size == 0) return NULL;
    /* script follows all part records and part data */
    /* we don't know exact part records size without parsing them,
       so skip to pdata and then script */
    (void)parts_start; (void)part_count; /* used indirectly via pdata_size */
    /* approximate: script is at body end - script_size */
    if (script_size > body_size) return NULL;
    uint32_t script_off = body_size - script_size;
    (void)pdata_size;
    const char *src = (const char *)(base + script_off);
    return strndup(src, script_size);
}

static Card *parse_card(const FileCtx *ctx, const BlockEntry *e,
                         uint16_t w, uint16_t h) {
    Card *card = calloc(1, sizeof *card);
    if (!card) return NULL;

    const uint8_t *body = blkbody(ctx, e);
    uint32_t body_size = e->size - 12;
    if (body_size < CARD_OFF_PARTS) { free(card); return NULL; }

    card->id      = (uint32_t)e->id;
    card->bkgd_id = (uint32_t)r32s(body + CARD_OFF_BKGD_ID);

    uint16_t part_count = r16(body + CARD_OFF_PART_CNT);
    uint16_t pdata_size = r16(body + CARD_OFF_PDATA_SZ);
    uint32_t script_sz  = r32(body + CARD_OFF_SCRIPT_SZ);

    /* BMAP id is stored in the card body, not the same as the card block id */
    int32_t bmap_id = r32s(body + CARD_OFF_BMAP_ID);
    const BlockEntry *bmap = (bmap_id != 0) ? ctx_find(ctx, BT_BMAP, bmap_id) : NULL;
    if (bmap)
        card->bitmap = bmap_decompress(blkbody(ctx, bmap), bmap->size - 12, w, h);

    uint32_t parts_bytes = 0;
    if (part_count && body_size > CARD_OFF_PARTS) {
        card->part_count = part_count;
        card->parts = parse_parts(body + CARD_OFF_PARTS,
                                   body_size - CARD_OFF_PARTS, part_count,
                                   &parts_bytes);
    }

    /* content section (field text) follows card-layer part records */
    uint32_t content_off = CARD_OFF_PARTS + parts_bytes;
    if (content_off + 6 <= body_size) {
        card->content_count = parse_card_content(body + content_off,
                                                  body_size - content_off,
                                                  &card->content);
    }

    card->script = extract_script(body, body_size, CARD_OFF_PARTS,
                                   part_count, pdata_size, script_sz);
    return card;
}

static Background *parse_bkgd(const FileCtx *ctx, const BlockEntry *e,
                                uint16_t w, uint16_t h) {
    Background *bg = calloc(1, sizeof *bg);
    if (!bg) return NULL;

    const uint8_t *body = blkbody(ctx, e);
    uint32_t body_size = e->size - 12;
    if (body_size < BKGD_OFF_PARTS) { free(bg); return NULL; }

    bg->id = (uint32_t)e->id;

    uint16_t part_count = r16(body + BKGD_OFF_PART_CNT);
    uint16_t pdata_size = r16(body + BKGD_OFF_PDATA_SZ);
    uint32_t script_sz  = r32(body + BKGD_OFF_SCRIPT_SZ);

    /* BMAP id is stored in the background body, not the same as the block id */
    int32_t bmap_id = r32s(body + BKGD_OFF_BMAP_ID);
    const BlockEntry *bmap = (bmap_id != 0) ? ctx_find(ctx, BT_BMAP, bmap_id) : NULL;
    if (bmap)
        bg->bitmap = bmap_decompress(blkbody(ctx, bmap), bmap->size - 12, w, h);

    if (part_count && body_size > BKGD_OFF_PARTS) {
        /* Some backgrounds store a name between the fixed header and parts,
         * causing parts to start at 0x28 instead of 0x26. Detect this by
         * checking whether the rec_size at 0x26 looks valid (>= PART_HDR_MIN). */
        uint32_t parts_off = BKGD_OFF_PARTS;
        if (body_size > parts_off + 2 && r16(body + parts_off) < PART_HDR_MIN)
            parts_off += 2;
        bg->part_count = part_count;
        bg->parts = parse_parts(body + parts_off,
                                 body_size - parts_off, part_count, NULL);
    }

    bg->script = extract_script(body, body_size, BKGD_OFF_PARTS,
                                 part_count, pdata_size, script_sz);
    return bg;
}

/* ---- STAK block offsets ----
 *
 * Offsets are from the body start (body = block + 12).
 * Confirmed by cross-referencing Bettencourt's format doc with
 * hex dumps of practice_test.hc and MacDungeonMaster.hc.
 *
 * The Bettencourt doc gives block offsets 0x01B8/0x01BA for height/width;
 * that maps to body[0x01AC/0x01AE], but empirical dumps place the values
 * 2 bytes later at body[0x01AE/0x01B0] — consistent across both test stacks.
 */
#define STAK_OFF_BKGD_COUNT   0x18  /* uint32: number of BKGD blocks */
#define STAK_OFF_TOTAL_CARDS  0x20  /* uint32: number of CARD blocks */
#define STAK_OFF_CARD_HEIGHT  0x1AC /* uint16: card height in pixels  */
#define STAK_OFF_CARD_WIDTH   0x1AE /* uint16: card width in pixels   */

/* ---- top-level loader ---- */

Stack *stack_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    rewind(f);
    if (fsz <= 0) { fclose(f); return NULL; }

    uint8_t *data = malloc((size_t)fsz);
    if (!data || fread(data, 1, (size_t)fsz, f) != (size_t)fsz) {
        free(data); fclose(f); return NULL;
    }
    fclose(f);

    /* build block index by linear scan */
    FileCtx ctx = { .data = data, .size = (size_t)fsz };
    uint32_t off = 0;
    while (off + 12 <= (uint32_t)fsz) {
        uint32_t bsize = r32(data + off);
        uint32_t btype = r32(data + off + 4);
        int32_t  bid   = r32s(data + off + 8);
        if (bsize < 12 || off + bsize > (uint32_t)fsz) break;
        ctx_add(&ctx, btype, bid, off, bsize);
        off += bsize;
    }

    Stack *s = calloc(1, sizeof *s);
    if (!s) goto fail;

    /* parse stack metadata */
    const BlockEntry *stak = ctx_find(&ctx, BT_STAK, -1);
    if (!stak) { fprintf(stderr, "stackworks: no STAK block in %s\n", path); goto fail; }
    const uint8_t *sb = blkbody(&ctx, stak);
    uint32_t stak_body_size = stak->size - 12;
    if (stak_body_size > STAK_OFF_CARD_WIDTH + 2) {
        s->card_height = r16(sb + STAK_OFF_CARD_HEIGHT);
        s->card_width  = r16(sb + STAK_OFF_CARD_WIDTH);
    }
    if (s->card_width == 0 || s->card_height == 0) {
        s->card_width = 512; s->card_height = 342; /* classic Mac screen */
    }

    /* allocate worst-case arrays (at most one per block) */
    s->bkgds = calloc(ctx.block_count, sizeof *s->bkgds);
    s->cards = calloc(ctx.block_count, sizeof *s->cards);
    if (!s->bkgds || !s->cards) goto fail;

    for (uint32_t i = 0; i < ctx.block_count; i++) {
        const BlockEntry *e = &ctx.blocks[i];
        if (e->type == BT_BKGD) {
            Background *bg = parse_bkgd(&ctx, e, s->card_width, s->card_height);
            if (bg) { s->bkgds[s->bkgd_count++] = *bg; free(bg); }
        } else if (e->type == BT_CARD) {
            Card *card = parse_card(&ctx, e, s->card_width, s->card_height);
            if (card) { s->cards[s->card_count++] = *card; free(card); }
        }
    }

    /* sanity check: STAK's declared card count vs. what we found by scanning */
    uint32_t expected_cards = r32(sb + STAK_OFF_TOTAL_CARDS);
    if (expected_cards && s->card_count != expected_cards)
        fprintf(stderr, "stackworks: STAK says %u cards, found %u\n",
                expected_cards, s->card_count);

    free(ctx.blocks);
    s->raw_data = data;  /* kept for stack_dump(); freed by stack_free() */
    s->raw_size = (size_t)fsz;
    return s;

fail:
    free(ctx.blocks);
    free(data);
    if (s) { free(s->bkgds); free(s->cards); free(s); }
    return NULL;
}

const char *card_field_text(const Card *c, uint16_t part_id) {
    for (uint16_t i = 0; i < c->content_count; i++)
        if (c->content[i].part_id == part_id)
            return c->content[i].text;
    return NULL;
}

Background *stack_find_bkgd(const Stack *s, uint32_t id) {
    for (uint32_t i = 0; i < s->bkgd_count; i++)
        if (s->bkgds[i].id == id) return &s->bkgds[i];
    return NULL;
}

void stack_free(Stack *s) {
    if (!s) return;
    for (uint32_t i = 0; i < s->card_count; i++) {
        Card *c = &s->cards[i];
        for (uint16_t j = 0; j < c->part_count; j++) {
            free(c->parts[j].name);
            free(c->parts[j].script);
        }
        free(c->parts);
        if (c->content) {
            for (uint16_t j = 0; j < c->content_count; j++)
                free(c->content[j].text);
            free(c->content);
        }
        free(c->bitmap);
        free(c->script);
    }
    free(s->cards);
    for (uint32_t i = 0; i < s->bkgd_count; i++) {
        Background *bg = &s->bkgds[i];
        for (uint16_t j = 0; j < bg->part_count; j++) {
            free(bg->parts[j].name);
            free(bg->parts[j].script);
        }
        free(bg->parts);
        free(bg->bitmap);
        free(bg->script);
    }
    free(s->bkgds);
    free(s->raw_data);
    free(s);
}

/* ---- dump ---- */

static void fourcc_str(uint32_t t, char out[5]) {
    out[0] = (t >> 24) & 0xFF;
    out[1] = (t >> 16) & 0xFF;
    out[2] = (t >>  8) & 0xFF;
    out[3] =  t        & 0xFF;
    out[4] = '\0';
    for (int i = 0; i < 4; i++)
        if (out[i] < 0x20 || out[i] > 0x7E) out[i] = '.';
}

static void hex_dump(FILE *out, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (i % 16 == 0)  fprintf(out, "    %04x: ", i);
        fprintf(out, "%02x ", data[i]);
        if (i % 16 == 7)  fprintf(out, " ");
        if (i % 16 == 15) {
            fprintf(out, " |");
            for (uint32_t j = i - 15; j <= i; j++) {
                uint8_t c = data[j];
                fprintf(out, "%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
            }
            fprintf(out, "|\n");
        }
    }
    if (len % 16) {
        uint32_t rem = len % 16;
        /* pad hex columns */
        for (uint32_t i = rem; i < 16; i++) {
            fprintf(out, "   ");
            if (i == 7) fprintf(out, " ");
        }
        fprintf(out, " |");
        for (uint32_t j = len - rem; j < len; j++) {
            uint8_t c = data[j];
            fprintf(out, "%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
        }
        fprintf(out, "|\n");
    }
}

void stack_dump(const Stack *s, FILE *out) {
    if (!s || !s->raw_data) return;
    const uint8_t *data = s->raw_data;
    size_t fsz = s->raw_size;

    /* re-scan block index from raw data */
    BlockEntry blocks[512];
    uint32_t block_count = 0;
    uint32_t off = 0;
    while (off + 12 <= (uint32_t)fsz && block_count < 512) {
        uint32_t bsize = r32(data + off);
        uint32_t btype = r32(data + off + 4);
        int32_t  bid   = r32s(data + off + 8);
        if (bsize < 12 || off + bsize > (uint32_t)fsz) break;
        blocks[block_count++] = (BlockEntry){ btype, bid, off, bsize };
        off += bsize;
    }

    /* --- block index --- */
    fprintf(out, "\n=== Block Index (%u blocks) ===\n", block_count);
    for (uint32_t i = 0; i < block_count; i++) {
        char tc[5];
        fourcc_str(blocks[i].type, tc);
        fprintf(out, "  [%03u] %-4s  id=%-6d  offset=0x%06x  size=%u\n",
                i, tc, blocks[i].id, blocks[i].offset, blocks[i].size);
    }

    /* --- full body dump for each block type of interest --- */
    static const uint32_t dump_types[] = {
        BT_STAK, BT_BKGD, BT_CARD, BT_BMAP
    };
    static const int dump_max[] = { 1, 8, 4, 2 }; /* max blocks to dump per type */

    for (int ti = 0; ti < 4; ti++) {
        uint32_t want = dump_types[ti];
        char tc[5]; fourcc_str(want, tc);
        int shown = 0;
        for (uint32_t i = 0; i < block_count && shown < dump_max[ti]; i++) {
            if (blocks[i].type != want) continue;
            const uint8_t *body = data + blocks[i].offset + 12;
            uint32_t body_size  = blocks[i].size - 12;
            fprintf(out, "\n--- %s  id=%-6d  body size=%u ---\n",
                    tc, blocks[i].id, body_size);
            hex_dump(out, body, body_size);
            shown++;
        }
    }

    /* --- parsed structure summary --- */
    fprintf(out, "\n=== Parsed Structure ===\n");
    fprintf(out, "  Stack: %ux%u  cards=%u  bkgds=%u\n",
            s->card_width, s->card_height, s->card_count, s->bkgd_count);

    for (uint32_t i = 0; i < s->bkgd_count; i++) {
        const Background *bg = &s->bkgds[i];
        fprintf(out, "\n  BKGD[%u] id=%u  parts=%u  bitmap=%s\n",
                i, bg->id, bg->part_count, bg->bitmap ? "yes" : "no");
        for (uint16_t j = 0; j < bg->part_count; j++) {
            const Part *p = &bg->parts[j];
            fprintf(out, "    part[%u] id=%u type=%u vis=%u "
                    "rect=(%d,%d,%d,%d) style=%u font=%u size=%u name=%s\n",
                    j, p->id, p->type, p->visible,
                    p->rect.top, p->rect.left, p->rect.bottom, p->rect.right,
                    p->style, p->font_id, p->text_size,
                    p->name ? p->name : "(none)");
        }
        if (bg->script)
            fprintf(out, "    script[%zu]: %.120s%s\n",
                    strlen(bg->script), bg->script,
                    strlen(bg->script) > 120 ? "..." : "");
    }

    for (uint32_t i = 0; i < s->card_count; i++) {
        const Card *c = &s->cards[i];
        fprintf(out, "\n  CARD[%u] id=%u  bkgd=%u  parts=%u  content=%u  bitmap=%s\n",
                i, c->id, c->bkgd_id, c->part_count, c->content_count,
                c->bitmap ? "yes" : "no");
        for (uint16_t j = 0; j < c->part_count; j++) {
            const Part *p = &c->parts[j];
            fprintf(out, "    part[%u] id=%u type=%u vis=%u "
                    "rect=(%d,%d,%d,%d) style=%u font=%u size=%u name=%s\n",
                    j, p->id, p->type, p->visible,
                    p->rect.top, p->rect.left, p->rect.bottom, p->rect.right,
                    p->style, p->font_id, p->text_size,
                    p->name ? p->name : "(none)");
        }
        for (uint16_t j = 0; j < c->content_count; j++) {
            const FieldContent *fc = &c->content[j];
            const char *t = fc->text ? fc->text : "(none)";
            fprintf(out, "    content[%u] part_id=%u: %.60s%s\n",
                    j, fc->part_id, t, strlen(t) > 60 ? "..." : "");
        }
        if (c->script)
            fprintf(out, "    script[%zu]: %.120s%s\n",
                    strlen(c->script), c->script,
                    strlen(c->script) > 120 ? "..." : "");
    }
    fprintf(out, "\n");
}
