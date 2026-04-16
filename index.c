#include "index.h"
#include "pes.h"
#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// --- PROVIDED FUNCTIONS (Don't delete these!) ---
IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) return &index->entries[i];
    }
    return NULL;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    for (int i = 0; i < index->count; i++) printf("  staged: %s\n", index->entries[i].path);
    return 0;
}

// --- TODO IMPLEMENTATIONS ---

static int compare_index_entries(const void *a, const void *b) {
    return strcmp(((const IndexEntry *)a)->path, ((const IndexEntry *)b)->path);
}

int index_load(Index *index) {
    memset(index, 0, sizeof(Index));
    FILE *f = fopen(".pes/index", "r");
    if (!f) return 0;
    char line[1024];
    while (fgets(line, sizeof(line), f) && index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];
        char hash_hex[HASH_HEX_SIZE + 1];
        if (sscanf(line, "%o %64s %ld %u %255s", &e->mode, hash_hex, &e->mtime_sec, &e->size, e->path) == 5) {
            hex_to_hash(hash_hex, &e->hash);
            index->count++;
        }
    }
    fclose(f);
    return 0;
}

int index_save(const Index *index) {
    Index sorted_idx = *index;
    qsort(sorted_idx.entries, sorted_idx.count, sizeof(IndexEntry), compare_index_entries);
    FILE *f = fopen(".pes/index.tmp", "w");
    if (!f) return -1;
    for (int i = 0; i < sorted_idx.count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&sorted_idx.entries[i].hash, hex);
        fprintf(f, "%o %s %ld %u %s\n", sorted_idx.entries[i].mode, hex, sorted_idx.entries[i].mtime_sec, sorted_idx.entries[i].size, sorted_idx.entries[i].path);
    }
    fclose(f);
    return rename(".pes/index.tmp", ".pes/index");
}

int index_add(Index *index, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    void *data = malloc(st.st_size);
    fread(data, 1, st.st_size, f);
    fclose(f);
    ObjectID blob_id;
    object_write(OBJ_BLOB, data, st.st_size, &blob_id);
    free(data);
    IndexEntry *e = index_find(index, path);
    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        e = &index->entries[index->count++];
        strncpy(e->path, path, sizeof(e->path)-1);
    }
    e->mode = get_file_mode(path);
    e->hash = blob_id;
    e->mtime_sec = st.st_mtime;
    e->size = st.st_size;
    return index_save(index);
}
