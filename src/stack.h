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

typedef struct {
    int16_t top, left, bottom, right;
} HCRect;

typedef struct {
    uint16_t id;
    uint8_t  type;     /* PART_BUTTON or PART_FIELD */
    uint8_t  visible;
    HCRect   rect;
    uint8_t  style;
    char    *name;
    char    *script;
} Part;

typedef struct {
    uint32_t  id;
    uint16_t  part_count;
    Part     *parts;
    uint8_t  *bitmap;  /* decompressed 1-bit rows, NULL if none */
    char     *script;
} Background;

typedef struct {
    uint32_t  id;
    uint32_t  bkgd_id;
    uint16_t  part_count;
    Part     *parts;
    char    **field_text;  /* parallel to parts[], NULL for buttons */
    uint8_t  *bitmap;
    char     *script;
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
void        stack_dump(const Stack *s, FILE *out);

#endif /* STACK_H */
