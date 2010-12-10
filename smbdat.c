#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define ERR(args...) fprintf(stderr, args)

struct {
    uint32_t folderentries;

    uint8_t *folderstrings;
    uint8_t *filestrings;

} header;

void chomp_header(FILE *fp) {
    uint32_t folderentries;
    size_t bytes;

    printf("head --");
    fseek(fp, 0, SEEK_END);
    printf(" [33ms %08x[0m", (uint32_t)ftell(fp));
    fseek(fp, 0, SEEK_SET);

    if ((bytes = fread(&folderentries, sizeof(uint32_t), 1, fp)) != 1) {
        ERR("Got %u ints when expecting 1 in chomp_header\n", (uint32_t)bytes);
        exit(1);
    }

    printf(" [31mf %08x[0m", folderentries);
    printf("\n");

    header.folderentries = folderentries;
}

void eat_folder_entries(FILE *fp) {
    uint32_t folder[2];
    size_t bytes, i;

    for (i = 0; i < header.folderentries; i++) {
        if ((bytes = fread(folder, sizeof(uint32_t), 2, fp)) != 2) {
            ERR("Got %u ints when expecting 2 in eat_folder_entries\n", (uint32_t)bytes);
            exit(1);
        }

        printf("fold --");
        printf(" [31mf %08x[0m", folder[0]);
        printf(" [32ms %08x[0m", folder[1]);
        printf("\n");
    }
}

void eat_file_headers(FILE *fp) {
    uint32_t fileheader[3], files;
    size_t bytes, i;

    if ((bytes = fread(&files, sizeof(uint32_t), 1, fp)) != 1) {
        ERR("Got %u ints when expecting 1 in eat_file_headers\n", (uint32_t)bytes);
        exit(1);
    }

    printf("flhd --");
    printf(" [36mt %08x[0m", files);
    printf("\n");

    for (i = 0; i < files; i++) {
       if ((bytes = fread(fileheader, sizeof(uint32_t), 3, fp)) != 3) {
           ERR("Got %u ints when expecting 3 in eat_file_headers\n", (uint32_t)bytes);
           exit(1);
       }

       printf("file --");
       printf(" [37mn %08x[0m", i);
       printf(" [34mo %08x[0m", fileheader[0]);
       printf(" [33ml %08x[0m", fileheader[1]);
       printf(" [31mf %08x[0m", fileheader[2]);
       printf("\n");
    }
}

void eat_string_tables(FILE *fp) {
    uint32_t stringheader[2];
    size_t bytes, i;

    printf("strh --");
    printf(" [34mo %08x[0m", (uint32_t)ftell(fp));

    if ((bytes = fread(stringheader, sizeof(uint32_t), 2, fp)) != 2) {
        ERR("Got %u ints when expecting 2 in eat_string_tables\n", (uint32_t)bytes);
        exit(1);
    }

    printf(" [33mfold %08x[0m", stringheader[0]); // folder table
    printf(" [36mfile %08x[0m", stringheader[1]); // file table
    printf("\n");

    for (i = 0; i < 2; i++) {
        size_t len = stringheader[i];
        uint8_t *stringtable = malloc(sizeof(uint8_t) * len);
        if ((bytes = fread(stringtable, sizeof(uint8_t), len, fp)) != len) {
            ERR("Got %u ints when expecting %u in eat_string_tables\n", (uint32_t)bytes, (uint32_t)len);
            exit(1);
        }
        switch (i) {
            case 0:
                header.folderstrings = stringtable;
                break;
            case 1:
                header.filestrings = stringtable;
                break;
            default:
                ERR("i was not 0 or 1 in eat_string_tables\n");
                exit(1);
        }
    }
}

int main(int argc, char *argv[]) {
    FILE *fp;

    if (argc < 1) {
        ERR("usage: %s filename\n", argv[0]);
        exit(1);
    }

    if (!(fp = fopen(argv[1], "r"))) {
        ERR("Unable to open file %s\n", argv[1]);
        exit(1);
    }

    printf("%s:\n", argv[1]);
    chomp_header(fp);
    eat_folder_entries(fp);
    eat_file_headers(fp);
    eat_string_tables(fp);

    fclose(fp);

    return 0;
}
