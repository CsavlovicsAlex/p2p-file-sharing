#pragma once
#include "file_scan.h"

/// creates a new socket which is prepared for listening, bound to a random port with a queue length of 5
/// @param listening_port_out the port which the listening socket uses. Output parameter
/// @return an int, representing the listening socket
int create_listening_socket(uint16_t *listening_port_out);

/// accepts a connection from a peer
/// @return the socket with the connection to the peer that requests a file
int accept_connection(int listening_sock);

/**
 * Sends all available files in ./client-data to the server.
 * @param server_sock the socket used to connect to the server
 * @param listen_port the port on which the client is listening for p2p connections
 * @param cf_out a pointer to a ClientFiles struct containing the filenames in the ./client-data folder. Output parameter
 *
 * Return codes:
 *   0  - success
 *   1  - error scanning the folder
 *   2  - error serializing the files
 *   3  - error sending data over socket
 */
int send_available_files(int server_sock, uint16_t listen_port, ClientFiles **cf_out);

// Clears the input buffer by consuming all characters left
void clear_input_buffer (void);

/**
 * Downloads the file from one of the peers by asking the server which peers have the file and then
 *  fetching it from one of the peers
 * @param server_sock the socket used to connect to the server
 * @param cf a pointer to the ClientFiles struct
 * @param filename the name of the file you want to download
 *
 * Return codes:
 *    0 - success
 *    1 - socket creation failed
 *    2 - connect error
 *    3 - send error
 *    4 - recv error
 *    5 - fopen error
 *    6 - file already exists
 *    7 - no peers have that file
 *    8 - request rejected
 *    9 - realloc error
 */
int download_file(int server_sock, ClientFiles *cf, const char *filename);

/**
 * Return codes:
 *    0 - success
 *    1 - operation rejected, filename may be dangerous or is too big
 *    3 - send error
 *    4 - recv error
 *    5 - fopen / stat/ fread error
 */
int serve_peer(int peer_sock, ClientFiles *cf);