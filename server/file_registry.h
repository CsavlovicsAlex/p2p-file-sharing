#pragma once
#include "peer.h"

typedef struct FileRegistry FileRegistry;

typedef struct {
    Peer peer;
    int owner_socket;
} RegistryPeer;

/// Creates an ADT of type FileRegistry
/// @return a pointer to a FileRegistry struct
FileRegistry *file_registry_create();

/// Frees all the memory allocated for the FileRegistry
/// @param file_registry a pointer to the FileRegistry
void file_registry_destroy(FileRegistry *file_registry);

/// Adds the client's IP and port as owner of the given filename
/// @param file_registry a pointer to the FileRegistry
/// @param file_name a pointer to the name of the file to be marked
/// @param owner a struct with information regarding the owner of the file: ip, port and socket
/// @return an int, marking the result of the operation
/* RETURN CODES
 * 0 - success
 * 2 - memory allocation / reallocation failed
 */
int file_registry_add_file_ownership(FileRegistry *file_registry, const char *file_name, RegistryPeer owner);

/// Returns the number of IPs that have that file_name
/// @param file_registry a pointer to the FileRegistry
/// @param file_name a pointer to the name of the file that is being searched
/// @return -1 if file_registry is null, the number of IPs otherwise
int ips_with_filename(FileRegistry *file_registry, const char *file_name);

/// Gets the RegistryPeer that has the requested file (file_name) at the current index. Used for looping through all the RegistryPeers
/// @param file_registry a pointer to the FileRegistry
/// @param file_name a pointer to the name of the file that is being searched
/// @param index the index in the RegistryPeers array
/// @param output a pointer to a RegistryPeer struct where the result will be returned. Null if there are no more peers
/* RETURN CODES
 * 0 - success
 * 1 - file_registry is NULL
 * 2 - could not find the filename in the registry
 * 3 - index is out of bounds
 */
int loop_peers_for_file(FileRegistry *file_registry, const char *file_name, int index, RegistryPeer* output);

/// Removes all the files owned by a peer by identifying it by its socket. If a file remains with 0 peers it will be deleted
/// @param file_registry a pointer to the FileRegistry
/// @param peer_socket the socket of the client whose connection has been terminated
/* RETURN CODES
 * 0 - success
 * 1 - file_registry is NULL
 * 2 - realloc fail
 */
int file_registry_remove_all_file_ownership(FileRegistry *file_registry, int peer_socket);

void file_registry_debug_print(FileRegistry *file_registry);