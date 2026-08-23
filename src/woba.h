#ifndef WOBA_H
#define WOBA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decompress a BMAP block body (starting at the byte after the 12-byte block
 * header) into a 1-bit bitmap for the full card.
 *
 * Returns a calloc'd buffer of card_h * ((card_w+7)/8) bytes, or NULL on
 * failure (bad data, mask-only BMAP with no image, or allocation error).
 * Caller must free() the result. */
uint8_t *bmap_decompress(const uint8_t *body, uint32_t body_size,
                          uint16_t card_w, uint16_t card_h);

#ifdef __cplusplus
}
#endif

#endif /* WOBA_H */
