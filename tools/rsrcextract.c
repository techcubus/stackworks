/*
 * rsrcextract — extract resources from a MacBinary II file
 *
 * Usage: rsrcextract <file.bin> <outdir>
 *
 * Creates: <outdir>/<FOURCC>/<id>[_<name>]
 *
 * The actual MacBinary/resource-fork parsing lives in src/rsrc.c (shared
 * with the stackworks app, which uses it to look up button ICON
 * resources); this tool is a thin CLI wrapper that writes each entry to a
 * file instead of decoding it in-process.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#include "../src/rsrc.h"

/* ---- path helpers ---- */

/*
 * fourcc_dirname: map a packed FourCC to a filesystem-safe 4-char
 * directory name. Printable ASCII other than / stays as-is; everything
 * else becomes _.
 */
static void fourcc_dirname(uint32_t cc, char out[5]) {
    uint8_t bytes[4] = {
        (uint8_t)(cc >> 24), (uint8_t)(cc >> 16), (uint8_t)(cc >> 8), (uint8_t)cc
    };
    for (int i = 0; i < 4; i++) {
        unsigned char c = bytes[i];
        if (c >= 33 && c < 127 && c != '/')
            out[i] = (char)c;
        else
            out[i] = '_';
    }
    out[4] = '\0';
}

/* Sanitize a resource name (already a plain C string) into a filename. */
static void name_to_safe(const char *name, char *out, size_t out_size) {
    size_t j = 0;
    for (size_t i = 0; name[i] && j < out_size - 1; i++) {
        unsigned char c = (unsigned char)name[i];
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

    if (file_size >= 128) {
        uint8_t fname_len = file[1];
        char mac_fname[64];
        size_t n = fname_len < 63 ? fname_len : 63;
        memcpy(mac_fname, file + 2, n);
        mac_fname[n] = '\0';
        printf("file:      %s\n", in_path);
        printf("mac name:  %s\n", mac_fname);
    }

    RsrcFork *rf = rsrc_open_macbinary(file, (size_t)file_size);
    if (!rf) {
        fprintf(stderr, "rsrcextract: not a MacBinary file, or its resource fork is empty\n");
        free(file);
        return 1;
    }

    printf("data fork: %u bytes\n", rf->data_len);
    printf("resources: %u\n\n", rf->entry_count);

    mkdir_safe(out_base);

    uint32_t extracted = 0;
    uint32_t cur_type = 0;
    int have_type = 0;

    for (uint32_t i = 0; i < rf->entry_count; i++) {
        const RsrcEntry *e = &rf->entries[i];

        if (!have_type || e->type != cur_type) {
            char tc[5];
            fourcc_dirname(e->type, tc);
            printf("'%.4s' (%s):\n", tc, tc);
            cur_type = e->type;
            have_type = 1;
        }

        char tc[5];
        fourcc_dirname(e->type, tc);
        char dir_path[4096];
        snprintf(dir_path, sizeof dir_path, "%s/%s", out_base, tc);
        mkdir_safe(dir_path);

        char safe_name[256];
        name_to_safe(e->name, safe_name, sizeof safe_name);

        char fname[512];
        if (safe_name[0])
            snprintf(fname, sizeof fname, "%d_%s", (int)e->id, safe_name);
        else
            snprintf(fname, sizeof fname, "%d", (int)e->id);

        char out_path[4096];
        snprintf(out_path, sizeof out_path, "%s/%s", dir_path, fname);

        FILE *out = fopen(out_path, "wb");
        if (!out) { perror(out_path); continue; }
        if (e->size > 0 && fwrite(e->data, 1, e->size, out) != e->size)
            perror(out_path);
        fclose(out);

        printf("  %6d  %5u bytes  %s\n", (int)e->id, e->size, e->name);
        extracted++;
    }

    printf("\nextracted %u resource%s to %s/\n",
           extracted, extracted == 1 ? "" : "s", out_base);
    rsrc_free(rf);
    free(file);
    return 0;
}
