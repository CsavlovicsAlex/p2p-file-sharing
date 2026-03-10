#pragma once
#include "file_registry.h"

/// receives and maps the client's files
/// @param file_registry a pointer to the file registry
/// @param client_socket the client's internet socket
/// @param connected_client - the client's IP address information through a struct sockaddr_in
/// @param out_peer the information regarding the client (stored as a RegistryPeer) will be put through this pointer. Output variable
/* RETURN CODES
 * 0 - success
 * 1 - recv error
 * 2 - realloc error - some files from the client might still be kept
 * 9 - connection terminated
 */
int get_and_store_client_files (FileRegistry *file_registry, int client_socket, const struct sockaddr_in * connected_client, RegistryPeer *out_peer);

/// receives a filename from the client and sends the IPs that have that file or registers that new file as being owned by the
///     client, depending on the message type
/// @param file_registry a pointer to the file registry
/// @param client_data a pointer to a RegistryPeer which contains the client's data
/* RETURN CODES
 * 0 - success
 * 1 - recv error
 * 2 - file registry operation error
 * 3 - send error
 * 4 - unknown message type
 * 9 - connection terminated
 */
int serve_client (FileRegistry *file_registry, const RegistryPeer *client_data);