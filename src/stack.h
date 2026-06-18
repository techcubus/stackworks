#ifndef STACK_H
#define STACK_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define FOURCC(a,b,c,d) \
    ((uint32_t)(a)<<24 | (uint32_t)(b)<<16 | (uint32_t)(c)<<8 | (uint32_t)(d))

#define BT_STAK  FOURCC('S','T','A','K')
#define BT_MAST  FOURCC('M','A','S','T')
#define BT_LIST  FOURCC('L','I','S','T')
#define BT_PAGE  FOURCC('P','A','G','E')
#define BT_CARD  FOURCC('C','A','R','D')
#define BT_BKGD  FOURCC('B','K','G','D')
#define BT_BMAP  FOURCC('B','M','A','P')
#define BT_STBL  FOURCC('S','T','B','L')
#define BT_FTBL  FOURCC('F','T','B','L')

#define PART_BUTTON  1
#define PART_FIELD   2

/* Button style — upper nibble of part record +0x0E.
 * Confirmed: rectangle=2 (Home/Prev/Next), shadow=3 (Find). */
#define BTN_STYLE_RECTANGLE    2
#define BTN_STYLE_SHADOW       3

/* Field style — byte at part record +0x0F.
 * Confirmed from field_day.hc hex + Mac screenshot comparison. */
#define FLD_STYLE_TRANSPARENT  0  /* no fill, no border — text over bitmap */
#define FLD_STYLE_OPAQUE       1  /* white fill, no border */
#define FLD_STYLE_RECTANGLE    2  /* white fill + plain border */
#define FLD_STYLE_SHADOW       4  /* white fill + border + drop shadow */
#define FLD_STYLE_SCROLLING    7  /* white fill + border + scroll bar */

typedef struct {
    int16_t top, left, bottom, right;
} HCRect;

typedef struct {
    uint16_t id;
    uint8_t  type;       /* PART_BUTTON or PART_FIELD */
    uint8_t  visible;
    HCRect   rect;
    uint8_t  style;
    uint16_t font_id;    /* Mac font family ID: 3=Geneva, 21=Helvetica, etc. */
    uint16_t text_size;  /* point size (0 = use stack default) */
    uint8_t  text_style; /* bold=0x01, italic=0x02, underline=0x04, ... */
    uint8_t  text_align; /* 0=left, 1=center, 2=right */
    char    *name;
    char    *script;
} Part;

typedef struct {
    uint16_t  part_id;  /* background field's part id */
    char     *text;     /* Mac Roman, \r-separated, null-term; NULL = empty */
} FieldContent;

typedef struct {
    uint32_t      id;
    uint16_t      part_count;
    Part         *parts;
    uint16_t      content_count;
    FieldContent *content;   /* bg-level field text, same on every card */
    uint8_t      *bitmap;    /* decompressed 1-bit rows, NULL if none */
    char         *script;
} Background;

typedef struct {
    uint32_t      id;
    uint32_t      bkgd_id;
    uint16_t      part_count;
    Part         *parts;
    uint16_t      content_count;
    FieldContent *content;   /* per-bkgd-field text, keyed by part_id */
    uint8_t      *bitmap;
    char         *script;
} Card;

typedef struct {
    uint16_t    card_width;
    uint16_t    card_height;
    uint32_t    card_count;
    Card       *cards;
    uint32_t    bkgd_count;
    Background *bkgds;
    /* raw file bytes — kept for stack_dump(), freed by stack_free() */
    uint8_t    *raw_data;
    size_t      raw_size;
} Stack;

Stack      *stack_load(const char *path);
void        stack_free(Stack *s);
Background *stack_find_bkgd(const Stack *s, uint32_t id);
const char *card_field_text(const Card *c, uint16_t part_id);
const char *bkgd_field_text(const Background *bg, uint16_t part_id);
void        stack_dump(const Stack *s, FILE *out);

#endif /* STACK_H */
