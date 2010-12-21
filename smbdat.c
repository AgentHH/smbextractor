#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define ERR(args...) fprintf(stderr, args)

struct datentry {
    uint32_t offset;
    uint32_t len;
    uint32_t dir;
    uint8_t *name;
};

struct state {
    uint32_t folderentries;
    uint32_t fileentries;

    uint8_t *folderstrings;
    uint32_t folderstringslen;
    uint8_t *filestrings;
    uint32_t filestringslen;

    uint8_t **folders;
    struct datentry *files;
};

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

    uint8_t *c = (uint8_t*)memchr(t->data, '\0', t->len);
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

int eat_string_tables(FILE *fp, struct state *st) {
    size_t bytes;
    uint32_t len;

    if (st == NULL) {
        return 0;
    }

    if (!eat_int(fp, &st->folderstringslen) ||
        !eat_int(fp, &st->filestringslen)) {
        ERR("Unable to get string table lengths\n");
        return 0;
    }

    len = st->folderstringslen;
    st->folderstrings = (uint8_t*)malloc(sizeof(uint8_t) * len);
    if ((bytes = fread(st->folderstrings, sizeof(uint8_t), len, fp)) != len) {
        ERR("Got %u ints (expecting %u) in eat_string_tables\n", (uint32_t)bytes, (uint32_t)len);
        return 0;
    }

    len = st->filestringslen;
    st->filestrings = (uint8_t*)malloc(sizeof(uint8_t) * len);
    if ((bytes = fread(st->filestrings, sizeof(uint8_t), len, fp)) != len) {
        ERR("Got %u ints (expecting %u) in eat_string_tables\n", (uint32_t)bytes, (uint32_t)len);
        return 0;
    }

    return 1;
}

int eat_file_list(FILE *fp, struct state *st) {
    uint32_t temp, i;
    size_t folderentriespos, fileentriespos;

    if (!eat_int(fp, &temp)) {
        goto chomp_headers_error;
    }
    st->folderentries = temp;
    folderentriespos = ftell(fp);
    fseek(fp, st->folderentries * 2 * sizeof(uint32_t), SEEK_CUR);
    if (!eat_int(fp, &temp)) {
        goto chomp_headers_error;
    }
    st->fileentries = temp;
    fileentriespos = ftell(fp);
    fseek(fp, st->fileentries * 3 * sizeof(uint32_t), SEEK_CUR);

    if (!eat_string_tables(fp, st)) {
        goto chomp_headers_error;
    }

    struct tokenizer t_;
    t_.data = st->folderstrings;
    t_.len = st->folderstringslen;

    st->folders = (uint8_t**)malloc(sizeof(uint8_t*) * st->folderentries);

    fseek(fp, folderentriespos, SEEK_SET);
    for (i = 0; i < st->folderentries; i++) {
        st->folders[i] = get_next_name(&t_);
    }

    struct tokenizer t;
    t.data = st->filestrings;
    t.len = st->filestringslen;

    st->files = (struct datentry*)malloc(sizeof(struct datentry) * st->fileentries);

    fseek(fp, fileentriespos, SEEK_SET);
    for (i = 0; i < st->fileentries; i++) {
        size_t bytes;
        uint32_t file[3];
        if ((bytes = fread(file, sizeof(uint32_t), 3, fp)) != 3) {
            ERR("Got %u ints when expecting 3 while eating file entries\n", (uint32_t)bytes);
            exit(1);
        }

        struct datentry *entry = &st->files[i];
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

int _mkdir(char *dir) {
    int ret = mkdir(dir, S_IRWXU);
    if (ret) {
        ret = errno;
        switch (ret) {
            case EEXIST: {
                struct stat st;
                ret = stat(dir, &st);
                if (ret != 0) {
                    ret = errno;
                    printf("Error stat()ing %s: %s", dir, strerror(ret));
                    return 0;
                }
                if (!S_ISDIR(st.st_mode)) {
                    printf("Error: %s is not a directory\n", dir);
                    return 0;
                }
                return 1;
            }
            default:
                printf("Unable to make directory %s: %s\n", dir, strerror(ret));
                break;
        }
        return 0;
    }

    return 1;
}

// this is a terrible hack
// I just wanted something that worked
int mkdir_recursive(const char *dir) {
    uint32_t i, len = strlen(dir);
    len = len > 255 ? 255 : len;

    if (len < 1) {
        return 1;
    }

    char temp[len + 1];
    memcpy(temp, dir, len);
    temp[len] = 0;

    if (temp[len - 1] == '/') {
        temp[len - 1] = 0;
    }

    for (i = 1; i < len; i++) {
        if (temp[i] == '/') {
            temp[i] = 0;
            if (!_mkdir(temp)) {
                return 0;
            }
            temp[i] = '/';
        }
    }
    return _mkdir(temp);
}

int main(int argc, char *argv[]) {
    FILE *fp;
    const char *folder = NULL;
    struct state st;

    if (argc < 1) {
        ERR("usage: %s filename [folder]\n", argv[0]);
        exit(1);
    }

    if (!(fp = fopen(argv[1], "r"))) {
        ERR("Unable to open file %s\n", argv[1]);
        exit(1);
    }

    if (!eat_file_list(fp, &st)) {
        ERR("Unable to parse file list\n");
        exit(1);
    }

    if (argc > 2) {
        uint32_t i;

        folder = argv[2];
        printf("Extracting contents to \"%s\"\n", folder);

        if (!mkdir_recursive(folder)) {
            exit(1);
        }
        if (chdir((const char*)folder) != 0) {
            int ret = errno;
            printf("Unable to change directory to %s: %s\n", folder, strerror(ret));
            exit(1);
        }

        for (i = 0; i < st.folderentries; i++) {
            const char *name = (const char*)st.folders[i];

            if (!mkdir_recursive(name)) {
                exit(1);
            }
        }
        for (i = 0; i < st.fileentries; i++) {
            struct datentry *file = &st.files[i];

            if (!copy_to_file(fp, file->offset, file->len, (char*)file->name)) {
                ERR("Error extracting \"%s\"\n", file->name);
                exit(1);
            }
        }
    } else {
        printf("Listing contents of \"%s\":\n", argv[1]);
        uint32_t i;
        for (i = 0; i < st.fileentries; i++) {
            struct datentry *file = &st.files[i];
            printf("%s %d\n", file->name, file->len);
        }
    }

    fclose(fp);

    return 0;
}
