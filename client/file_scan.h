#pragma once
#include <stdint.h>

typedef struct ClientFiles ClientFiles;

/// creates the ClientFiles struct by recursively searching the given folder and storing the name of all the files inside
/// @param path the path to the folder to be searched. Absolute or relative
/// @return a pointer to a new ClientFiles struct. It is the caller's duty to destroy it
ClientFiles *client_files_create(const char *path);

/// frees all the memory allocated for the ClientFiles
/// @param files a pointer to the ClientFiles struct
void client_files_destroy(ClientFiles *files);

/// checks if a filename exists in the provided ClientFiles
/// @param files a pointer to the ClientFiles
/// @param filename the name of the file to search
/// @return true if the filename exists in the ClientFiles. False otherwise
bool client_files_contains(ClientFiles *files, const char *filename);

/// transforms the filenames from the ClientFiles into a contiguous string array where each file path is separated by \0.
///     The array itself ends with another \0
/// the caller must free the returned buffer
/// @param files a pointer to the ClientFiles
/// @param out_len the length of the resulting contiguous array in bytes
/// @return a pointer to a char array
char *client_files_serialize(ClientFiles *files, uint32_t *out_len);

/// @param files a pointer to the ClientFiles
/// @return the number of files stored in ClientFiles
int client_files_files_count(ClientFiles *files);

/// returns a copy of the index-th filename in the array or NULL If the index is out of bounds
/// the caller must free the returned filename
/// @param files a pointer to the ClientFiles
/// @param index the position (between 0 and size-1) at which you want to get the filename
/// @return a pointer to a heap-allocated memory zone containing the filename
char *client_files_get_at_index(ClientFiles *files, int index);

/// returns a pointer to the copy of the relative path of the filename or null if the filename does not exist
/// the caller must free the returned pointer
/// @param files a pointer to the ClientFiles
/// @param filename the name of the file whose relative path you want to know
/// @return a pointer to a heap-allocated memory zone containing the relative path
char *client_files_get_relative_path_for_filename(ClientFiles *files, const char *filename);

/// adds a new downloaded file into the ClientFiles registry, marking it as being owned. Works only for downloaded
///     files that get stored in the ./client-data/received folder
/// @param files a pointer to the ClientFiles
/// @param filename the name of the file which was downloaded
/// @return 0 on success; 1 on realloc error
int client_files_add_new_file(ClientFiles *files, const char *filename);

/// returns a pointer to the copy of the absolute path of the filename or null if the filename does not exist
/// the caller must free the returned pointer
/// @param files a pointer to the ClientFiles
/// @param index the position (between 0 and size-1) at which you want to get the absolute path
/// @return a pointer to a heap-allocated memory zone containing the absolute path
char *client_files_get_absolute_path(ClientFiles *files, int index);