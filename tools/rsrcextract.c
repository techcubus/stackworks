/*
 * rsrcextract — extract resources from a MacBinary II file
 *
 * Usage: rsrcextract <file.bin> <outdir>
 *
 * Creates: <outdir>/<FOURCC>/<id>[_<name>]
 *
 * Resource fork layout (Inside Macintosh vol I):
 *   fork header   (256 bytes)
 *   resource data (variable)
 *   resource map  (variable)
 *
 * Each resource data block is preceded by a 4-byte length.
 * Map contains: header copy, type list, reference lists, name list.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stddef.h>

/* ---- big-endian reads ---- */

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

/* ---- path helpers ---- */

/*
 * fourcc_dirname: map 4 raw bytes to a filesystem-safe 4-char directory name.
 * Printable ASCII other than / stays as-is; everything else becomes _.
 * Trailing spaces become _ (handles 'STR ', 'snd ').
 */
static void fourcc_dirname(const uint8_t cc[4], char out[5]) {
    for (int i = 0; i < 4; i++) {
        unsigned char c = cc[i];
        if (c >= 33 && c < 127 && c != '/')
            out[i] = (char)c;
        else
            out[i] = '_';
    }
    out[4] = '\0';
}

/* Sanitize a Pascal string (length byte + bytes) into a C string for filenames. */
static void pascal_to_safe(const uint8_t *pascal, char *out, size_t out_size) {
    uint8_t len = pascal[0];
    size_t j = 0;
    for (uint8_t i = 0; i < len && j < out_size - 1; i++) {
        unsigned char c = pascal[1 + i];
        if (c >= 32 && c < 127 && c != '/' && c != '\\' && c != ':')
            out[j++] = (char)c;
        else
            out[j++] = '_';
    }
    out[j] = '\0';
}

static void mkdir_safe(const char *path) {
    mkdir(path, 0755);  /* ignore EEXIST */
}

/* ---- main ---- */

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: rsrcextract <macbinary2.bin> <outdir>\n");
        return 1;
    }
    const char *in_path  = argv[1];
    const char *out_base = argv[2];

    /* ---- load whole file ---- */
    FILE *f = fopen(in_path, "rb");
    if (!f) { perror(in_path); return 1; }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);
    uint8_t *file = malloc((size_t)file_size);
    if (!file) { fprintf(stderr, "out of memory\n"); fclose(f); return 1; }
    if (fread(file, 1, (size_t)file_size, f) != (size_t)file_size) {
        perror(in_path); free(file); fclose(f); return 1;
    }
    fclose(f);

    /* ---- MacBinary II header (128 bytes) ---- */
    if (file_size < 128) {
        fprintf(stderr, "rsrcextract: file too small\n");
        free(file); return 1;
    }

    /* Sanity checks — bytes 0, 74, 82 must be 0 in a valid MacBinary header */
    if (file[0] != 0)
        fprintf(stderr, "rsrcextract: warning: byte 0 = 0x%02X (expected 0x00)\n", file[0]);
    if (file[74] != 0 || file[82] != 0)
        fprintf(stderr, "rsrcextract: warning: reserved bytes non-zero, may not be MacBinary\n");

    uint8_t  fname_len    = file[1];
    char     mac_fname[64];
    memcpy(mac_fname, file + 2, fname_len < 63 ? fname_len : 63);
    mac_fname[fname_len < 63 ? fname_len : 63] = '\0';

    uint8_t  mb_version  = file[124];
    uint32_t data_len    = r32(file + 83);
    uint32_t rsrc_len    = r32(file + 87);
    /* Secondary header only exists in MacBinary III (version >= 130).
     * MacBinary I/II files often have garbage at offset 120; ignore it. */
    uint32_t sec_hdr_len = (mb_version >= 130) ? r32(file + 120) : 0u;

    uint32_t rsrc_start  = 128u + pad128(sec_hdr_len) + pad128(data_len);

    printf("file:      %s\n", in_path);
    printf("mac name:  %s\n", mac_fname);
    printf("data fork: %u bytes\n", data_len);
    printf("rsrc fork: %u bytes at offset %u\n", rsrc_len, rsrc_start);

    if (rsrc_len == 0) {
        fprintf(stderr, "rsrcextract: resource fork is empty\n");
        free(file); return 1;
    }
    if ((long)(rsrc_start + rsrc_len) > file_size) {
        fprintf(stderr, "rsrcextract: resource fork extends beyond end of file\n");
        free(file); return 1;
    }

    const uint8_t *rsrc = file + rsrc_start;

    /* ---- resource fork header (first 16 bytes of fork) ---- */
    uint32_t rdata_off = r32(rsrc + 0);    /* offset from fork start to data section */
    uint32_t map_off   = r32(rsrc + 4);    /* offset from fork start to map */
    uint32_t rdata_len = r32(rsrc + 8);
    uint32_t map_len   = r32(rsrc + 12);

    printf("rdata:     offset=%u len=%u\n", rdata_off, rdata_len);
    printf("map:       offset=%u len=%u\n", map_off, map_len);

    if (map_off + map_len > rsrc_len || rdata_off + rdata_len > rsrc_len) {
        fprintf(stderr, "rsrcextract: offsets in resource fork header are out of range\n");
        free(file); return 1;
    }

    const uint8_t *map = rsrc + map_off;

    /* ---- resource map ----
     *   0-15:  copy of fork header
     *  16-19:  handle to next map (reserved)
     *  20-21:  file reference number (reserved)
     *  22-23:  resource fork attributes
     *  24-25:  offset from map start to type list
     *  26-27:  offset from map start to name list
     */
    uint16_t type_list_off = r16(map + 24);
    uint16_t name_list_off = r16(map + 26);

    if (type_list_off >= map_len) {
        fprintf(stderr, "rsrcextract: type list offset out of range\n");
        free(file); return 1;
    }

    const uint8_t *type_list = map + type_list_off;
    const uint8_t *name_list = (name_list_off < map_len) ? map + name_list_off : NULL;

    /* type list: 2-byte (count - 1), then 8-byte entries */
    uint16_t type_count = r16(type_list) + 1u;
    printf("types:     %u\n\n", type_count);

    mkdir_safe(out_base);

    uint32_t extracted = 0;

    for (uint16_t ti = 0; ti < type_count; ti++) {
        const uint8_t *te = type_list + 2 + (size_t)ti * 8;

        /* 8-byte type entry:
         *   0-3: FourCC
         *   4-5: (count - 1)
         *   6-7: offset from type list start to reference list
         */
        const uint8_t *cc        = te;
        uint16_t       res_count = r16(te + 4) + 1u;
        uint16_t       ref_off   = r16(te + 6);   /* from type_list start */

        char type_dir[5];
        fourcc_dirname(cc, type_dir);

        char dir_path[4096];
        snprintf(dir_path, sizeof dir_path, "%s/%s", out_base, type_dir);
        mkdir_safe(dir_path);

        printf("'%.4s' (%s): %u resource%s\n",
               (const char *)cc, type_dir, res_count, res_count == 1 ? "" : "s");

        const uint8_t *ref_list = type_list + ref_off;

        for (uint16_t ri = 0; ri < res_count; ri++) {
            /* 12-byte reference entry:
             *   0-1:  resource ID (int16)
             *   2-3:  offset from name list start to name (0xFFFF = no name)
             *   4:    attributes
             *   5-7:  offset from data section start to this resource's data block (uint24)
             *   8-11: reserved handle
             */
            const uint8_t *re      = ref_list + (size_t)ri * 12;
            int16_t   res_id       = (int16_t)r16(re);
            uint16_t  name_off     = r16(re + 2);
            uint32_t  res_data_off = r24(re + 5);

            /* resolve name */
            char res_name[256] = "";
            if (name_off != 0xFFFF && name_list) {
                const uint8_t *np = name_list + name_off;
                /* bounds check: Pascal string length byte */
                if ((np - map) + 1 + np[0] <= (ptrdiff_t)map_len)
                    pascal_to_safe(np, res_name, sizeof res_name);
            }

            /* build output filename */
            char fname[512];
            if (res_name[0])
                snprintf(fname, sizeof fname, "%d_%s", (int)res_id, res_name);
            else
                snprintf(fname, sizeof fname, "%d", (int)res_id);

            char out_path[4096];
            snprintf(out_path, sizeof out_path, "%s/%s", dir_path, fname);

            /* resource data: 4-byte length prefix followed by raw bytes */
            uint32_t data_blk_off = rdata_off + res_data_off;
            if (data_blk_off + 4 > rsrc_len) {
                fprintf(stderr, "  skip %s/%s: data offset out of range\n",
                        type_dir, fname);
                continue;
            }
            uint32_t rlen = r32(rsrc + data_blk_off);
            if (data_blk_off + 4 + rlen > rsrc_len) {
                fprintf(stderr, "  skip %s/%s: data block truncated (%u bytes claimed)\n",
                        type_dir, fname, rlen);
                continue;
            }
            const uint8_t *rdata = rsrc + data_blk_off + 4;

            FILE *out = fopen(out_path, "wb");
            if (!out) { perror(out_path); continue; }
            if (rlen > 0 && fwrite(rdata, 1, rlen, out) != rlen)
                perror(out_path);
            fclose(out);

            printf("  %6d  %5u bytes  %s\n", (int)res_id, rlen, res_name);
            extracted++;
        }
    }

    printf("\nextracted %u resource%s to %s/\n",
           extracted, extracted == 1 ? "" : "s", out_base);
    free(file);
    return 0;
}
