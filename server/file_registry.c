#include "file_registry.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

typedef struct {
    char *filename;
    int peer_count;
    RegistryPeer *peers;
} FileEntry;

struct FileRegistry {
    FileEntry *entries;
    int entry_count;
    pthread_mutex_t mutex;
};

/// checks if the peer is part of the owners of a file by returning its index in the array or -1 if it doesn't own the file
/// @param entry the file's entry in the RegistryFiles
/// @param peer_socket the socket of the peer you wish to verify
int peer_owns_file(const FileEntry* entry, const int peer_socket) {
    for (int i = 0; i < entry->peer_count; i++) {
        if (entry->peers[i].owner_socket == peer_socket) {
            return i;
        }
    }
    return -1;
}

/// searches the filename in the FileRegistry and returns its index
/// @return -1 or -2 if the filename doesn't appear; a natural number representing the index if the file exists
int search_filename_in_FR(const FileEntry *entries, const int entry_count, const char *file_name) {
    if (entries == NULL) {
        return -1;
    }
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].filename, file_name) == 0) {
            return i;
        }
    }
    return -2;
}

FileRegistry *file_registry_create() {
    FileRegistry *temp = malloc(sizeof(FileRegistry));
    if (!temp) {
        return NULL;
    }

    temp->entries = NULL;
    temp->entry_count = 0;

    pthread_mutex_init(&temp->mutex, NULL);

    return temp;
}

void file_registry_destroy(FileRegistry *file_registry) {
    const FileEntry *entries = file_registry->entries;
    for (int i = 0; i < file_registry->entry_count; i++) {
        free(entries[i].filename);
        free(entries[i].peers);
    }
    free(file_registry->entries);
    free(file_registry);

    pthread_mutex_destroy(&file_registry->mutex);
}

int file_registry_add_file_ownership(FileRegistry *file_registry, const char *file_name, const RegistryPeer owner) {
    pthread_mutex_lock(&file_registry->mutex);
    int result = 0;

    const int entry_count = file_registry->entry_count;
    const int index = search_filename_in_FR(file_registry->entries, file_registry->entry_count, file_name);

    if (index == -1 || index == -2) {
        // file doesn't exist, create a new entry for it
        FileEntry *temp = realloc(file_registry->entries, sizeof(FileEntry) * (file_registry->entry_count + 1));
        if (!temp) {
            perror("Realloc failed - low on memory. Cannot store client as adding a new file to the system");
            result = 2;
            goto end;
        }

        temp[entry_count].filename = strdup(file_name);
        temp[entry_count].peers = malloc(sizeof(RegistryPeer));
        temp[entry_count].peers[0] = owner;
        temp[entry_count].peer_count = 1;

        file_registry->entry_count += 1;
        file_registry->entries = temp;

        result = 0;
        goto end;
    } else {
        // index > 0, file already exists
        if (peer_owns_file(&file_registry->entries[index], owner.owner_socket) != -1) {
            // the peer already has the file, don't add it again as an owner
            result = 0;
            goto end;
        }

        // the peer doesn't own the file, add it
        const int size = file_registry->entries[index].peer_count;

        RegistryPeer* temp = realloc(file_registry->entries[index].peers, sizeof(RegistryPeer) * (size + 1));
        if (!temp) {
            perror("Realloc failed - low on memory. Cannot store client as adding a new file to the system");
            result = 2;
            goto end;
        }
        temp[size] = owner;

        file_registry->entries[index].peers = temp;
        file_registry->entries[index].peer_count += 1;

        result = 0;
        goto end;
    }

    end:
    pthread_mutex_unlock(&file_registry->mutex);
    return result;
}

int ips_with_filename(FileRegistry *file_registry, const char *file_name) {
    if (file_registry == NULL) {
        return -1;
    }

    pthread_mutex_lock(&file_registry->mutex);
    int parsing_filenames_index = 0;
    while (strcmp(file_registry->entries[parsing_filenames_index].filename, file_name) != 0) {
        parsing_filenames_index += 1;
        if (parsing_filenames_index == file_registry->entry_count) {
            // couldn't find the filename in the FileRegistry
            pthread_mutex_unlock(&file_registry->mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&file_registry->mutex);
    return file_registry->entries[parsing_filenames_index].peer_count;
}

int loop_peers_for_file(FileRegistry *file_registry, const char *file_name, const int index, RegistryPeer* output) {
    if (file_registry == NULL) {
        return 1;
    }

    pthread_mutex_lock(&file_registry->mutex);
    int parsing_filenames_index = 0;
    while (strcmp(file_registry->entries[parsing_filenames_index].filename, file_name) != 0) {
        parsing_filenames_index += 1;
        if (parsing_filenames_index == file_registry->entry_count) {
            // couldn't find the filename in the FileRegistry

            pthread_mutex_unlock(&file_registry->mutex);
            return 2;
        }
    }

    if (index < 0 || index >= file_registry->entries[parsing_filenames_index].peer_count) {
        // the index the user is trying to access is out of bounds

        pthread_mutex_unlock(&file_registry->mutex);
        return 3;
    }

    *output = file_registry->entries[parsing_filenames_index].peers[index];
    pthread_mutex_unlock(&file_registry->mutex);
    return 0;
}

int file_registry_remove_all_file_ownership(FileRegistry *file_registry, const int peer_socket) {
    if (file_registry == NULL) {
        return 1;
    }


    pthread_mutex_lock(&file_registry->mutex);
    for (int i = 0; i < file_registry->entry_count; i++) {
        const int index = peer_owns_file(file_registry->entries + i, peer_socket);

        // if our peer doesn't own the file skip the file
        if (index  == -1) {
            continue;
        }

        const int owners_of_file = file_registry->entries[i].peer_count;

        // check if our peer isn't the only possessor of the file
        if (owners_of_file> 1) {
            // replace the peer with the last one
            file_registry->entries[i].peers[index] = file_registry->entries[i].peers[owners_of_file-1];

            RegistryPeer *temp = realloc(file_registry->entries[i].peers, sizeof(RegistryPeer) * (owners_of_file-1));
            if (!temp) {
                perror("Realloc failed");
                pthread_mutex_unlock(&file_registry->mutex);
                return 2;
            }
            file_registry->entries[i].peers = temp;

            // change the ownership number
            file_registry->entries[i].peer_count -= 1;

            // go to the next file
            continue;
        }

        // the peer does own the file and is the only possessor

        // replace the file entry with the last one
        file_registry->entries[i] = file_registry->entries[file_registry->entry_count - 1];

        // delete the extra space
        FileEntry* temp = realloc(file_registry->entries, sizeof(FileEntry) * (file_registry->entry_count - 1));
        if (!temp) {
            perror("Realloc failed");
            pthread_mutex_unlock(&file_registry->mutex);
            return 2;
        }

        file_registry->entries = temp;

        // change the ownership number
        file_registry->entry_count -= 1;

        // don't go to the next index
        i = i-1;
    }

    pthread_mutex_unlock(&file_registry->mutex);
    return 0;
}

void file_registry_debug_print(FileRegistry *file_registry) {
    pthread_mutex_lock(&file_registry->mutex);
    for (int i = 0; i < file_registry->entry_count; i++) {
        printf("We have: %s owned by: ", file_registry->entries[i].filename);
        for (int j = 0; j < file_registry->entries[i].peer_count; j++) {
            printf(" %s:%d; socket: %d ",
                   file_registry->entries[i].peers[j].peer.ip,
                   file_registry->entries[i].peers[j].peer.port,
                   file_registry->entries[i].peers[j].owner_socket);
        }
        printf("\n");
    }
    fflush(stdout);
    pthread_mutex_unlock(&file_registry->mutex);
}