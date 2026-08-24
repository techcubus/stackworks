#ifndef RSRC_H
#define RSRC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One entry from a classic Mac resource fork: FourCC type + numeric ID,
 * pointing directly into the buffer passed to rsrc_open_macbinary(). */
typedef struct {
    uint32_t       type;  /* FourCC, e.g. FOURCC('I','C','O','N') -- see stack.h */
    int16_t        id;
    char           name[256];
    const uint8_t *data;
    uint32_t       size;
} RsrcEntry;

typedef struct {
    /* data fork location within the buffer passed to rsrc_open_macbinary() */
    uint32_t   data_off;
    uint32_t   data_len;

    RsrcEntry *entries;
    uint32_t   entry_count;
} RsrcFork;

/* Parse `file` as a MacBinary II encoded blob (the format `hcopy -m`
 * produces): a 128-byte header, the data fork, then the resource fork.
 * Entries in the returned RsrcFork point directly into `file`, which must
 * stay valid for the RsrcFork's lifetime.
 *
 * Returns NULL if `file` isn't recognizably MacBinary, or its resource
 * fork is empty/absent -- callers should fall back to treating `file` as a
 * bare data fork in that case. */
RsrcFork *rsrc_open_macbinary(const uint8_t *file, size_t file_size);
void      rsrc_free(RsrcFork *rf);

/* NULL if no resource with this type+id exists. */
const RsrcEntry *rsrc_find(const RsrcFork *rf, uint32_t type, int16_t id);

#ifdef __cplusplus
}
#endif

#endif /* RSRC_H */
