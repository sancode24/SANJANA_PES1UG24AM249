#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ... [PROVIDED functions stay the same] ...

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    // 1. Prepare the Header
    char header[64];
    const char *type_str = (type == OBJ_BLOB) ? "blob" : (type == OBJ_TREE) ? "tree" : "commit";
    int header_len = sprintf(header, "%s %zu", type_str, len) + 1; // +1 for the '\0'

    // 2. Build the Full Object (Header + Data)
    size_t full_len = header_len + len;
    uint8_t *full_obj = malloc(full_len);
    if (!full_obj) return -1;
    memcpy(full_obj, header, header_len);
    memcpy(full_obj + header_len, data, len);

    // 3. Compute SHA-256 and check Deduplication
    compute_hash(full_obj, full_len, id_out);
    if (object_exists(id_out)) {
        free(full_obj);
        return 0; 
    }

    // 4. Create Shard Directory
    char path[512], temp_path[512], hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);
    object_path(id_out, path, sizeof(path));
    
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(dir_path, 0755); 

    // 5. Write to Temp File
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    int fd = open(temp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { free(full_obj); return -1; }
    write(fd, full_obj, full_len);
    
    // 6. OS Persistence
    fsync(fd);
    int fd = open(temp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { 
    perror("Failed to open file"); // This will tell you WHY it failed (e.g., "No such file or directory")
    free(full_obj); 
    return -1; 
}
    close(fd);
    rename(temp_path, path);

    free(full_obj);
    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    // 1. Open and get file size
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    size_t total_file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // 2. Read entire file into buffer
    uint8_t *full_buffer = malloc(total_file_size);
    if (!full_buffer) { fclose(fp); return -1; }
    fread(full_buffer, 1, total_file_size, fp);
    fclose(fp);

    // 3. Integrity Check: Verify hash matches
    ObjectID actual_id;
    compute_hash(full_buffer, total_file_size, &actual_id);
    if (memcmp(id->hash, actual_id.hash, HASH_SIZE) != 0) {
        free(full_buffer);
        return -1; // Corruption detected!
    }

    // 4. Parse Header
    void *header_end = memchr(full_buffer, '\0', total_file_size);
    if (!header_end) { free(full_buffer); return -1; }

    size_t header_len = (uint8_t *)header_end - (uint8_t *)full_buffer + 1;
    char type_str[16];
    sscanf((char *)full_buffer, "%15s", type_str);

    if (strcmp(type_str, "blob") == 0) *type_out = OBJ_BLOB;
    else if (strcmp(type_str, "tree") == 0) *type_out = OBJ_TREE;
    else if (strcmp(type_str, "commit") == 0) *type_out = OBJ_COMMIT;

    // 5. Extract data
    *len_out = total_file_size - header_len;
    *data_out = malloc(*len_out);
    memcpy(*data_out, (uint8_t *)full_buffer + header_len, *len_out);

    free(full_buffer);
    return 0;
}
