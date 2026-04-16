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

// Helper to sort the index entries by path
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
        if (sscanf(line, "%o %64s %ld %u %255s", 
                   &e->mode, hash_hex, &e->mtime_sec, &e->size, e->path) == 5) {
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

    char temp_path[] = ".pes/index.tmp";
    FILE *f = fopen(temp_path, "w");
    if (!f) return -1;

    for (int i = 0; i < sorted_idx.count; i++) {
        char hash_hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&sorted_idx.entries[i].hash, hash_hex);
        fprintf(f, "%o %s %ld %u %s\n", 
                sorted_idx.entries[i].mode, hash_hex, 
                sorted_idx.entries[i].mtime_sec, sorted_idx.entries[i].size, 
                sorted_idx.entries[i].path);
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return rename(temp_path, ".pes/index");
}

int index_add(Index *index, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    
    void *data = malloc(st.st_size);
    if (!data) { fclose(f); return -1; }

    // FIXED: This handles the warning and ensures data is actually read
    size_t read_bytes = fread(data, 1, st.st_size, f);
    fclose(f);
    if (read_bytes != (size_t)st.st_size) {
        free(data);
        return -1;
    }

    ObjectID blob_id;
    if (object_write(OBJ_BLOB, data, st.st_size, &blob_id) != 0) {
        free(data);
        return -1;
    }
    free(data);

    IndexEntry *e = index_find(index, path);
    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        e = &index->entries[index->count++];
        memset(e, 0, sizeof(IndexEntry));
        strncpy(e->path, path, sizeof(e->path) - 1);
    }

    e->mode = get_file_mode(path);
    e->hash = blob_id;
    e->mtime_sec = (uint64_t)st.st_mtime;
    e->size = (uint32_t)st.st_size;

    return index_save(index);
}
