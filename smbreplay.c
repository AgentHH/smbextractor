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

struct state {
    uint8_t *filename;
    uint8_t *levelname;

    uint8_t ghosts;
    uint8_t character;
    uint32_t *runlen;
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

int eat_replay_header(FILE *fp, struct state *st) {
    struct {
        uint8_t levelname[16];
        uint8_t ghosts;
        uint8_t character;
        uint16_t padding;
    } replayheader;

    size_t count;

    if ((count = fread(&replayheader, sizeof(replayheader), 1, fp)) != 1) {
        ERR("Got %u count when expecting 1 in eat_replay_header\n", (uint32_t)count);
        return 0;
    }

    if (replayheader.padding != 0xffff) {
        ERR("Padding should be 0xffff in eat_replay_header; got 0x%04x\n", replayheader.padding);
        return 0;
    }

    replayheader.levelname[15] = 0;
    st->levelname = (uint8_t*)strdup((char*)replayheader.levelname);
    st->ghosts = replayheader.ghosts;
    st->character = replayheader.character;

    printf("lh  : [32mc %02x[0m tr %02x [34mn \"%s\"[0m\n", st->character, st->ghosts, st->levelname);

    return 1;
}

int eat_run_headers(FILE *fp, struct state *st) {
    uint32_t *ghostheader = malloc(sizeof(uint32_t) * st->ghosts);

    size_t count;

    if ((count = fread(ghostheader, sizeof(uint32_t), st->ghosts, fp)) != st->ghosts) {
        ERR("Got %u ints when expecting %u in eat_run_headers\n", (uint32_t)count, st->ghosts);
        return 0;
    }

    st->runlen = ghostheader;

    int i;
    for (i = 0; i < st->ghosts; i++) {
        printf("gh  : r %02x [33m%08x[0m\n", i, ghostheader[i]);
    }

    return 1;
}

int eat_replay(FILE *fp, struct state *st) {
    if (!eat_replay_header(fp, st)) {
        return 0;
    }

    uint32_t garbage;
    if (!eat_int(fp, &garbage)) {
        return 0;
    }
    if (garbage != st->ghosts - 1) {
        printf("[31mWARNING: garbage is %u, not %u\n", garbage, st->ghosts - 1);
    }

    if (!eat_run_headers(fp, st)) {
        return 0;
    }

    printf("runs start at %08x\n", ftell(fp));

    int i, j;
    for (i = 0; i < st->ghosts; i++) {
        printf("g   :%5u ------------------------------\n", i);
        for (j = 0; j < st->runlen[i]; j++) {
            uint32_t rec[3];
            size_t count;
            if ((count = fread(rec, sizeof(uint32_t), 3, fp)) != 3) {
                ERR("Got %u ints when expecting %u in eat_\n", (uint32_t)count, 3);
                return 0;
            }
            printf("rec :%5u %08x %08x %08x\n", j, rec[0], rec[1], rec[2]);
        }
    }

    return 1;
}

int main(int argc, char *argv[]) {
    FILE *fp;
    struct state st;

    if (argc < 1) {
        ERR("usage: %s filename\n", argv[0]);
        exit(1);
    }

    st.filename = (uint8_t*)argv[1];

    if (!(fp = fopen(argv[1], "r"))) {
        ERR("Unable to open file %s\n", argv[1]);
        exit(1);
    }

    if (!eat_replay(fp, &st)) {
        ERR("Unable to parse replay\n");
        exit(1);
    }

    fclose(fp);

    return 0;
}
