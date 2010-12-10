#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define ERR(args...) fprintf(stderr, args)

struct datentry {
    uint32_t offset;
    uint32_t len;
    uint32_t dir;
    uint8_t *name;
};

struct {
    uint32_t folderentries;
    uint32_t fileentries;

    uint8_t *folderstrings;
    uint32_t folderstringslen;
    uint8_t *filestrings;
    uint32_t filestringslen;

    struct datentry *files;
} state;

struct tokenizer {
    uint8_t *data;
    uint32_t len;
};

int eat_int(FILE *fp, uint32_t *out) {
    if (out == NULL) {
        return 0;
    }

    uint32_t temp;
    size_t bytes;

    if ((bytes = fread(&temp, sizeof(uint32_t), 1, fp)) != 1) {
        ERR("Got %u ints when expecting 1 in eat_int\n", (uint32_t)bytes);
        return 0;
    }

    *out = temp;

    return 1;
}

uint8_t *get_next_name(struct tokenizer *t) {
    if (t->data == NULL || t->len == 0) {
        return NULL;
    }

    uint8_t *c = memchr(t->data, '\0', t->len);
    if (c == NULL) {
        ERR("Data wasn't null-terminated!\n");
        return NULL;
    }

    uint8_t *ret = t->data;

    uint32_t len = (c - t->data) + 1;
    t->data += len;
    t->len += len;

    return ret;
}

int eat_string_table(FILE *fp, uint32_t *len, uint8_t **data) {
    size_t bytes;
    uint32_t temp;

    if (len == NULL || data == NULL) {
        return 0;
    }

    if (!eat_int(fp, &temp)) {
        ERR("Unable to get string table length\n");
        return 0;
    }

    uint8_t *stringtable = malloc(sizeof(uint8_t) * temp);
    if ((bytes = fread(stringtable, sizeof(uint8_t), temp, fp)) != temp) {
        ERR("Got %u ints when expecting %u in eat_string_table\n", (uint32_t)bytes, (uint32_t)temp);
        return 0;
    }

    *len = temp;
    *data = stringtable;
    return 1;
}

int eat_file_list(FILE *fp) {
    uint32_t temp, i;
    size_t folderentriespos, fileentriespos;

    if (!eat_int(fp, &temp)) {
        goto chomp_headers_error;
    }
    // folder stuff is ignored because filenames have the folder info
    state.folderentries = temp;
    folderentriespos = ftell(fp);
    fseek(fp, state.folderentries * 2 * sizeof(uint32_t), SEEK_CUR);
    // file info is useful, though
    if (!eat_int(fp, &temp)) {
        goto chomp_headers_error;
    }
    state.fileentries = temp;
    fileentriespos = ftell(fp);
    fseek(fp, state.fileentries * 3 * sizeof(uint32_t), SEEK_CUR);

    if (!eat_string_table(fp, &state.folderstringslen, &state.folderstrings)) {
        goto chomp_headers_error;
    }
    if (!eat_string_table(fp, &state.filestringslen, &state.filestrings)) {
        goto chomp_headers_error;
    }

    state.files = malloc(sizeof(struct datentry) * state.fileentries);

    struct tokenizer t;
    t.data = state.filestrings;
    t.len = state.filestringslen;

    fseek(fp, fileentriespos, SEEK_SET);
    for (i = 0; i < state.fileentries; i++) {
        size_t bytes;
        uint32_t file[3];
        if ((bytes = fread(file, sizeof(uint32_t), 3, fp)) != 3) {
            ERR("Got %u ints when expecting 3 while eating file entries\n", (uint32_t)bytes);
            exit(1);
        }

        struct datentry *entry = &state.files[i];
        entry->offset = file[0];
        entry->len = file[1];
        entry->dir = file[2];
        entry->name = get_next_name(&t);
        if (entry->name == NULL) {
            ERR("File %d has null name\n", i);
        }
    }

    return 1;

chomp_headers_error:
    ERR("chomp_headers failed\n");
    return 0;
}

int do_copy(FILE *a, FILE *b, uint32_t len) {
    uint8_t buf[len];
    size_t bytes = fread(buf, sizeof(uint8_t), len, a);
    if (bytes != len) {
        ERR("Got %u bytes when expecting %u\n", (uint32_t)bytes, len);
        return 0;
    }
    bytes = fwrite(buf, sizeof(uint8_t), len, b);
    if (bytes != len) {
        ERR("Only wrote %u bytes instead of %u\n", (uint32_t)bytes, len);
        return 0;
    }

    return 1;

}

int copy_to_file(FILE *fp, uint32_t offset, uint32_t len, char *newname) {
    const uint32_t blocksize = 65536;

    FILE *newfile = fopen(newname, "w");
    if (newfile == NULL) {
        ERR("Unable to open %s\n", newname);
        return 0;
    }
    fseek(fp, offset, SEEK_SET);

    uint32_t remain = len;
    while (remain > blocksize) {
        if (!do_copy(fp, newfile, blocksize)) {
            fclose(newfile);
            return 0;
        }
        remain -= blocksize;
    }
    do_copy(fp, newfile, remain);

    fclose(newfile);

    return 1;
}

int main(int argc, char *argv[]) {
    FILE *fp;
    char *folder = NULL;

    if (argc < 1) {
        ERR("usage: %s filename [folder]\n", argv[0]);
        exit(1);
    }

    if (!(fp = fopen(argv[1], "r"))) {
        ERR("Unable to open file %s\n", argv[1]);
        exit(1);
    }

    if (!eat_file_list(fp)) {
        ERR("Unable to parse file list\n");
        exit(1);
    }

    if (argc > 2) {
        folder = argv[2];
        printf("Extracting contents to \"%s\"\n", folder);
        uint32_t i;
        for (i = 0; i < state.fileentries; i++) {
            struct datentry *file = &state.files[i];

            char filename[256];
            snprintf(filename, 256, "%s/%s", folder, file->name);
            if (!copy_to_file(fp, file->offset, file->len, filename)) {
                ERR("Error extracting \"%s\"\n", file->name);
                exit(1);
            }
        }
    } else {
        printf("Listing contents of \"%s\":\n", argv[1]);
        uint32_t i;
        for (i = 0; i < state.fileentries; i++) {
            struct datentry *file = &state.files[i];
            printf("%s %d\n", file->name, file->len);
        }
    }

    fclose(fp);

    return 0;
}
