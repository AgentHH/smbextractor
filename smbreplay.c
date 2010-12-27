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

struct replayrecord {
    uint8_t unknown[12];
} __attribute__((__packed__));

struct replay {
    uint8_t *filename;
    uint8_t *levelname;

    uint8_t ghosts;
    uint8_t character;
    uint32_t whichghost;
    uint32_t *runlen;
    struct replayrecord **records;
};

struct replayheader {
    uint8_t levelname[16];
    uint8_t ghosts;
    uint8_t character;
    uint16_t padding;
    uint32_t whichghost;
} __attribute__((__packed__));

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

void get_character(uint8_t character, char *buf, size_t len) {
    const char *string;

    switch (character) {
        case 0:
            string = "Super Meat Boy";
            break;
        default:
            snprintf(buf, len, "unknown (%d)", character);
            return;
    }

    snprintf(buf, len, "%s", string);
}

int eat_replay_header(FILE *fp, struct replay *rp) {
    struct replayheader rph;
    size_t count;

    if ((count = fread(&rph, sizeof(struct replayheader), 1, fp)) != 1) {
        ERR("Got %u count when expecting 1 in eat_replay_header\n", (uint32_t)count);
        return 0;
    }

    if (rph.padding != 0xffff) {
        ERR("Padding should be 0xffff in eat_replay_header; got 0x%04x\n", rph.padding);
        return 0;
    }

    if (rph.whichghost >= rph.ghosts) {
        ERR("Primary ghost is out of range (got %u, needed to be < %u\n", rph.whichghost, rph.ghosts);
        return 0;
    }

    rph.levelname[15] = 0;
    rp->levelname = (uint8_t*)strdup((char*)rph.levelname);
    rp->ghosts = rph.ghosts;
    rp->character = rph.character;
    rp->whichghost = rph.whichghost;

#ifdef DEBUG
    char charname[32];
    get_character(rp->character, charname, 32);

    printf("lh  : [31mtr %02x[0m [32ms %02x[0m\n", rp->ghosts, rp->whichghost);
    printf("lhn : [36mchar: %s[0m [35mlevel: \"%s\"[0m\n", charname, rp->levelname);
#endif

    return 1;
}

int eat_run_lengths(FILE *fp, struct replay *rp) {
    uint32_t *ghostheader = malloc(sizeof(uint32_t) * rp->ghosts);

    size_t count;

    if ((count = fread(ghostheader, sizeof(uint32_t), rp->ghosts, fp)) != rp->ghosts) {
        ERR("Got %u ints when expecting %u in eat_run_lengths\n", (uint32_t)count, rp->ghosts);
        return 0;
    }

    rp->runlen = ghostheader;

#ifdef DEBUG
    int i;
    for (i = 0; i < rp->ghosts; i++) {
        printf("gh  : [31mr %02x[0m [33ml %08x[0m\n", i, ghostheader[i]);
    }
#endif

    return 1;
}

int eat_runs(FILE *fp, struct replay *rp) {
    uint32_t i;

    rp->records = malloc(sizeof(struct replayrecord*) * rp->ghosts);

    for (i = 0; i < rp->ghosts; i++) {
        size_t count;
        uint32_t len = rp->runlen[i];
        struct replayrecord *data = malloc(sizeof(struct replayrecord) * rp->runlen[i]);
        rp->records[i] = data;
#ifdef DEBUG
        //printf("g   :%5u %5u\n", i, rp->runlen[i]);
#endif
        if ((count = fread(data, sizeof(struct replayrecord), len, fp)) != len) {
            ERR("Got %u records when expecting %u in eat_runs\n", (uint32_t)count, len);
            return 0;
        }
    }

    return 1;
}

int eat_replay(FILE *fp, struct replay *rp) {
    if (!eat_replay_header(fp, rp)) {
        return 0;
    }

    if (!eat_run_lengths(fp, rp)) {
        return 0;
    }

    if (!eat_runs(fp, rp)) {
        return 0;
    }

    return 1;
}

int main(int argc, char *argv[]) {
    FILE *fp;
    struct replay rp;

    if (argc < 1) {
        ERR("usage: %s filename\n", argv[0]);
        exit(1);
    }

    rp.filename = (uint8_t*)argv[1];

    if (!(fp = fopen(argv[1], "r"))) {
        ERR("Unable to open file %s\n", argv[1]);
        exit(1);
    }

    if (!eat_replay(fp, &rp)) {
        ERR("Unable to parse replay\n");
        exit(1);
    }

    fclose(fp);

    return 0;
}
