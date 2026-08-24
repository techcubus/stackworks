#include "rsrc.h"
#include <stdlib.h>
#include <string.h>

/* ---- big-endian reads (see tools/rsrcextract.c for the format this
 * mirrors -- kept in sync by hand, not shared, since that tool stays a
 * standalone single-file build) ---- */

static uint16_t r16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
static uint32_t r24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}
static uint32_t r32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static uint32_t pad128(uint32_t n) {
    return n ? ((n + 127u) & ~127u) : 0u;
}

static void pascal_to_cstr(const uint8_t *pascal, char *out, size_t out_size) {
    uint8_t len = pascal[0];
    size_t j = 0;
    for (uint8_t i = 0; i < len && j < out_size - 1; i++)
        out[j++] = (char)pascal[1 + i];
    out[j] = '\0';
}

static int add_entry(RsrcFork *rf, uint32_t *cap, RsrcEntry e) {
    if (rf->entry_count == *cap) {
        uint32_t newcap = *cap ? *cap * 2 : 32;
        RsrcEntry *grown = realloc(rf->entries, newcap * sizeof *grown);
        if (!grown) return -1;
        rf->entries = grown;
        *cap = newcap;
    }
    rf->entries[rf->entry_count++] = e;
    return 0;
}

RsrcFork *rsrc_open_macbinary(const uint8_t *file, size_t file_size) {
    if (file_size < 128) return NULL;

    /* MacBinary header sanity: byte 0 (old version) and the two reserved
     * bytes must be zero, and the Mac filename length must be plausible. */
    uint8_t fname_len = file[1];
    if (file[0] != 0 || file[74] != 0 || file[82] != 0 ||
        fname_len == 0 || fname_len > 63)
        return NULL;

    uint8_t  mb_version  = file[124];
    uint32_t data_len    = r32(file + 83);
    uint32_t rsrc_len    = r32(file + 87);
    uint32_t sec_hdr_len = (mb_version >= 130) ? r32(file + 120) : 0u;
    uint32_t rsrc_start  = 128u + pad128(sec_hdr_len) + pad128(data_len);

    if (rsrc_len == 0) return NULL;
    if ((uint64_t)rsrc_start + rsrc_len > file_size) return NULL;

    const uint8_t *rsrc = file + rsrc_start;

    uint32_t rdata_off = r32(rsrc + 0);
    uint32_t map_off   = r32(rsrc + 4);
    uint32_t rdata_len = r32(rsrc + 8);
    uint32_t map_len   = r32(rsrc + 12);

    if ((uint64_t)map_off + map_len > rsrc_len ||
        (uint64_t)rdata_off + rdata_len > rsrc_len)
        return NULL;

    const uint8_t *map = rsrc + map_off;

    uint16_t type_list_off = r16(map + 24);
    uint16_t name_list_off = r16(map + 26);
    if (type_list_off >= map_len) return NULL;

    const uint8_t *type_list = map + type_list_off;
    const uint8_t *name_list = (name_list_off < map_len) ? map + name_list_off : NULL;

    uint16_t type_count = r16(type_list) + 1u;

    RsrcFork *rf = calloc(1, sizeof *rf);
    if (!rf) return NULL;
    rf->data_off = 128u + pad128(sec_hdr_len);
    rf->data_len = data_len;

    uint32_t cap = 0;
    for (uint16_t ti = 0; ti < type_count; ti++) {
        const uint8_t *te = type_list + 2 + (size_t)ti * 8;
        if ((size_t)(te - map) + 8 > map_len) break;

        uint32_t       cc        = r32(te);
        uint16_t       res_count = r16(te + 4) + 1u;
        uint16_t       ref_off   = r16(te + 6);
        const uint8_t *ref_list  = type_list + ref_off;

        for (uint16_t ri = 0; ri < res_count; ri++) {
            const uint8_t *re = ref_list + (size_t)ri * 12;
            if ((size_t)(re - map) + 12 > map_len) break;

            int16_t  res_id       = (int16_t)r16(re);
            uint16_t name_off     = r16(re + 2);
            uint32_t res_data_off = r24(re + 5);

            RsrcEntry entry;
            memset(&entry, 0, sizeof entry);
            entry.type = cc;
            entry.id   = res_id;

            if (name_off != 0xFFFF && name_list) {
                const uint8_t *np = name_list + name_off;
                if ((size_t)(np - map) + 1 + np[0] <= map_len)
                    pascal_to_cstr(np, entry.name, sizeof entry.name);
            }

            uint32_t data_blk_off = rdata_off + res_data_off;
            if ((uint64_t)data_blk_off + 4 > rsrc_len) continue;
            uint32_t rlen = r32(rsrc + data_blk_off);
            if ((uint64_t)data_blk_off + 4 + rlen > rsrc_len) continue;

            entry.data = rsrc + data_blk_off + 4;
            entry.size = rlen;

            if (add_entry(rf, &cap, entry) != 0) {
                rsrc_free(rf);
                return NULL;
            }
        }
    }

    return rf;
}

void rsrc_free(RsrcFork *rf) {
    if (!rf) return;
    free(rf->entries);
    free(rf);
}

const RsrcEntry *rsrc_find(const RsrcFork *rf, uint32_t type, int16_t id) {
    if (!rf) return NULL;
    for (uint32_t i = 0; i < rf->entry_count; i++)
        if (rf->entries[i].type == type && rf->entries[i].id == id)
            return &rf->entries[i];
    return NULL;
}
