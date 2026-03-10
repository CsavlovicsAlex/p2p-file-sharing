#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_scan.h"

typedef struct {
    char *filename;
    char *relative_path;
} RelativeFilePath;

struct ClientFiles {
    RelativeFilePath *files;
    int file_count;
    pthread_mutex_t mutex;
};

static void scan_recursive(const char *base_path, const char *current_path, RelativeFilePath **files, int *count) {
    DIR *dir = opendir(current_path);
    if (!dir) return;

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        // Build full path
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", current_path, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) != 0) continue;

        if (S_ISREG(st.st_mode)) {
            // It's a file -> save the name and relative path
            *files = realloc(*files, (*count + 1) * sizeof(RelativeFilePath));
            (*files)[*count].filename = strdup(entry->d_name);

            // Compute relative path
            size_t base_len = strlen(base_path);
            if (strncmp(fullpath, base_path, base_len) == 0) {
                // +1 to skip the '/' after base_path if present
                const char *rel_path_start = fullpath + base_len;
                if (*rel_path_start == '/')
                    rel_path_start++;
                (*files)[*count].relative_path = strdup(rel_path_start);
            } else {
                // fallback: store fullpath as relative path if base_path is not prefix
                (*files)[*count].relative_path = strdup(fullpath);
            }
            (*count)++;
        } else if (S_ISDIR(st.st_mode)) {
            // It's a folder -> go deeper
            scan_recursive(base_path, fullpath, files, count);
        }
    }

    closedir(dir);
}

ClientFiles *client_files_create(const char *path) {
    RelativeFilePath *files = NULL;
    int count = 0;

    scan_recursive(path, path, &files, &count);

    ClientFiles *client_files = malloc(sizeof(ClientFiles));
    client_files->files = files;
    client_files->file_count = count;

    pthread_mutex_init(&client_files->mutex, NULL);

    return client_files;
}

void client_files_destroy(ClientFiles *files) {
    pthread_mutex_lock(&files->mutex);
    for (int i = 0; i < files->file_count; i++) {
        free(files->files[i].filename);
        free(files->files[i].relative_path);
    }
    pthread_mutex_unlock(&files->mutex);
    pthread_mutex_destroy(&files->mutex);

    free(files->files);
    free(files);
}

bool client_files_contains(ClientFiles *files, const char *filename) {
    pthread_mutex_lock(&files->mutex);

    for (int i = 0; i < files->file_count; i++) {
        if (strcmp(files->files[i].filename, filename) == 0) {
            pthread_mutex_unlock(&files->mutex);
            return true;
        }
    }

    pthread_mutex_unlock(&files->mutex);
    return false;
}

char *client_files_serialize(ClientFiles *files, uint32_t *out_len) {
    pthread_mutex_lock(&files->mutex);

    // First compute total size
    size_t total = 0;
    for (int i = 0; i < files->file_count; i++) {
        total += strlen(files->files[i].filename) + 1; // +1 for \0 separator
    }
    total += 1; // final \0 to terminate list

    char *buffer = malloc(total);
    size_t pos = 0;

    for (int i = 0; i < files->file_count; i++) {
        size_t len = strlen(files->files[i].filename);
        memcpy(buffer + pos, files->files[i].filename, len);
        pos += len;
        buffer[pos++] = '\0';
    }

    buffer[pos++] = '\0'; // end of list

    *out_len = (uint32_t)total;

    pthread_mutex_unlock(&files->mutex);

    return buffer;
}

int client_files_files_count(ClientFiles *files) {
    pthread_mutex_lock(&files->mutex);
    const int count = files->file_count;
    pthread_mutex_unlock(&files->mutex);

    return count;
}

char *client_files_get_at_index(ClientFiles *files, const int index) {
    pthread_mutex_lock(&files->mutex);

    if (index < 0 || index >= files->file_count) {
        pthread_mutex_unlock(&files->mutex);
        return NULL;
    }

    char *result = strdup(files->files[index].filename);
    pthread_mutex_unlock(&files->mutex);

    return result;
}

char *client_files_get_relative_path_for_filename(ClientFiles *files, const char *filename) {
    pthread_mutex_lock(&files->mutex);

    for (int i = 0; i < files->file_count; i++) {
        if (strcmp(files->files[i].filename, filename) == 0) {
            char *result = strdup(files->files[i].relative_path);
            pthread_mutex_unlock(&files->mutex);

            return result;
        }
    }

    pthread_mutex_unlock(&files->mutex);
    return NULL;
}

char *client_files_get_absolute_path(ClientFiles *files, const int index) {
    pthread_mutex_lock(&files->mutex);

    if (index < 0 || index >= files->file_count) {
        pthread_mutex_unlock(&files->mutex);
        return NULL;
    }
    char *result = strdup(files->files[index].relative_path);
    pthread_mutex_unlock(&files->mutex);

    return result;
}

int client_files_add_new_file(ClientFiles *files, const char *filename) {
    pthread_mutex_lock(&files->mutex);

    RelativeFilePath *temp = realloc(files->files, (files->file_count + 1) * sizeof(RelativeFilePath));
    if (!temp) {
        printf("realloc failed\n");
        pthread_mutex_unlock(&files->mutex);

        return 1;
    }

    char relative_path[512];
    strcpy(relative_path, "./client-data/received/");
    strcpy(relative_path + strlen(relative_path), filename);

    temp[files->file_count].filename = strdup(filename);
    temp[files->file_count].relative_path = strdup(relative_path);

    files->files = temp;
    files->file_count++;

    pthread_mutex_unlock(&files->mutex);
    return 0;
}