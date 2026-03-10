#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/stat.h>

#include "communication.h"

#include <unistd.h>

#include "message_types.h"
#include "peer.h"

int create_listening_socket(uint16_t *listening_port_out) {
    const int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("socket creation failed");
        exit(1);
    }

    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY;
    client_addr.sin_port = htons(0); // 0 = let OS choose free port

    if (bind(listen_sock, (struct sockaddr *) &client_addr, sizeof(client_addr)) < 0) {
        perror("bind failed");
        exit(1);
    }

    if (listen(listen_sock, 5) < 0) {
        perror("listen failed");
        exit(1);
    }

    // retrieve the chosen port
    socklen_t addr_len = sizeof(client_addr);
    if (getsockname(listen_sock, (struct sockaddr *) &client_addr, &addr_len) < 0) {
        perror("getsockname failed");
        exit(1);
    }

    *listening_port_out = ntohs(client_addr.sin_port);

    return listen_sock;
}

int accept_connection(const int listening_sock) {
    struct sockaddr_in peer;
    socklen_t peer_size;
    memset(&peer, 0, sizeof(peer));
    peer_size = sizeof(peer);
    const int peer_socket = accept(listening_sock, (struct sockaddr *) &peer, &peer_size);

    if (peer_socket < 0) {
        perror("accept failed");
        exit(1);
    }

    printf("Incoming peer connection from %s:%d\n", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
    return peer_socket;
}

int send_available_files(const int server_sock, const uint16_t listen_port, ClientFiles **cf_out) {
    // send the listen port
    const uint16_t net_listen_port = htons(listen_port);
    if (send(server_sock, &net_listen_port, sizeof(uint16_t), 0) < 0) {
        perror("Error sending the listen_port - couldn't send the port\n");
        return 3;
    }

    // get all the files we have
    ClientFiles *cf = client_files_create("./client-data");
    *cf_out = cf;

    if (cf == NULL) {
        perror("Error scanning the folder ./client-data");
        return 1;
    }

    #ifdef DEBUG
    printf("Found %d files:\n", client_files_files_count(cf));
    for (int i = 0; i < client_files_files_count(cf); i++) {
        char *file = client_files_get_at_index(cf, i);
        char *abs_path = client_files_get_absolute_path(cf, i);
        printf("  %s -> %s\n", abs_path, file);
        free(file);
        free(abs_path);
    }
    #endif


    // transform the files matrix into a contiguous char array
    uint32_t cont_files_len = 0;
    char *contiguous_files = client_files_serialize(cf, &cont_files_len);

    if (!contiguous_files) {
        perror("Error serializing files");
        return 2;
    }

    // send the information to the server; step 1: send the size
    const uint32_t net_cont_files_len = htonl(cont_files_len);
    if (send(server_sock, &net_cont_files_len, sizeof(uint32_t), 0) < 0) {
        perror("Error sending the contiguous files - couldn't send the array length");

        // free the arrays
        free(contiguous_files);

        return 3;
    }

    // send the information to the server; step 2: send the char array
    if (send(server_sock, contiguous_files, cont_files_len, 0) < 0) {
        perror("Error sending the contiguous files - couldn't send the char array");

        // free the arrays
        free(contiguous_files);

        return 3;
    }

    // free the arrays
    free(contiguous_files);
    return 0;
}

void clear_input_buffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/**
 * Asks the user for a filename to download and returns the IP for the corresponding client
 * @param sock the socket of the server
 * @param cf a pointer to the ClientFiles
 * @param filename the name of the file you want
 * @param peers_out a pointer to an array of Peer structs. Output parameter. The caller is responsible for freeing it
 * @param length_out the number of peers in the peers_out array
 * Return codes:
 *    0 - success
 *    3 - send error
 *    4 - recv error
 *    6 - file already exists
 *    7 - no peers have that file
 */
int request_owners_of_file(const int sock, ClientFiles *cf, const char *filename, Peer **peers_out,
                           uint32_t *length_out) {
    // PART 1: check if we have the filename in our system
    if (client_files_contains(cf, filename)) {
        #ifdef DEBUG
        printf("Because you are in test mode, we will continue with the request as usual\n");
        #endif

        #ifndef DEBUG
        return 6;
        #endif
    }

    // PART 2: send the filename to the server
    const uint16_t message_type = htons(MSG_REQUEST_FILE);
    if (send(sock, &message_type, sizeof(uint16_t), 0) < 0) {
        perror("Error sending the filename - couldn't send the message type\n");
        return 3;
    }

    uint32_t filename_len = strlen(filename) + 1;
    filename_len = htonl(filename_len);
    if (send(sock, &filename_len, sizeof(uint32_t), 0) < 0) {
        perror("Error sending the filename - couldn't send the length\n");
        return 3;
    }

    if (send(sock, filename, strlen(filename) + 1, 0) < 0) {
        perror("Error sending the filename - couldn't send the file\n");
        return 3;
    }

    // PART 3: receive the list of IPs
    uint32_t list_len;
    if (recv(sock, &list_len, sizeof(uint32_t), 0) < 0) {
        perror("Couldn't receive the list of IPs - couldn't get the length\n");
        return 4;
    }
    list_len = ntohl(list_len);

    // check if there are computers that have the file
    if (list_len == 0) {
        return 7;
    }

    uint8_t *buffer = malloc(list_len);
    uint8_t *p = buffer;

    if (recv(sock, buffer, list_len, 0) < 0) {
        perror("Couldn't receive the list of IPs - couldn't get the list\n");
        free(buffer);
        return 4;
    }

    // split the resulted buffer to get the information; firstly get the number of peers
    uint32_t peer_count;
    memcpy(&peer_count, buffer, sizeof(peer_count));
    peer_count = ntohl(peer_count);
    p += sizeof(peer_count);

    Peer *peers = malloc(peer_count * sizeof(Peer));

    // loop through the buffer to get the information about each peer
    for (int i = 0; i < peer_count; i++) {
        // get the IP length
        uint32_t ip_len;
        memcpy(&ip_len, p, sizeof(ip_len));
        ip_len = ntohl(ip_len);
        p += sizeof(ip_len);

        // get the IP address
        char ip[INET_ADDRSTRLEN];
        memcpy(ip, p, ip_len);
        ip[ip_len] = '\0';
        p += ip_len;

        // get the port
        uint16_t port;
        memcpy(&port, p, sizeof(port));
        port = ntohs(port);
        p += sizeof(port);

        // store the result in the list of Peers
        memcpy(peers[i].ip, ip, INET_ADDRSTRLEN);
        peers[i].port = port;
    }

    free(buffer);
    *peers_out = peers;
    *length_out = peer_count;

    return 0;
}

// @return the index of the best peer to be contacted for the file
uint32_t choose_best_peer(const Peer *peers, const uint32_t peer_count) {
    // this function can be modified to ping each peer and select the closest one
    // right now it returns the first peer, its implementation will be provided in the future
    return 0;
}

/**
 * Opens a communication channel (TCP) with the peer whose IP we've been given, requests the file and downloads it
 * @param filename the filename that we want to download
 * @param peer_to_contact the Peer from which the file will be downloaded
 * Return codes:
 *    0 - success
 *    1 - socket creation failed
 *    2 - connect error
 *    3 - send error
 *    4 - recv error
 *    5 - fopen error
 */
int get_file_from_peer(const char *filename, const Peer peer_to_contact) {
    int return_code = 0;
    FILE *fp = NULL;

    // STEP 1: START THE CONNECTION TO THE PEER
    // create the socket
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket creation failed while trying to get the file from peer");
        return_code = 1;
        goto cleanup;
    }

    // connect
    struct sockaddr_in peer;
    memset(&peer, 0, sizeof(peer));
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = inet_addr(peer_to_contact.ip);
    peer.sin_port = htons(peer_to_contact.port);
    if (connect(sock, (struct sockaddr *) &peer, sizeof(peer)) < 0) {
        perror("connect failed while trying to get the file from peer");
        return_code = 2;
        goto cleanup;
    }

    // STEP 2: REQUEST THE FILE FROM THE PEER
    uint32_t filename_len = strlen(filename) + 1;
    filename_len = htonl(filename_len);
    if (send(sock, &filename_len, sizeof(uint32_t), 0) < 0) {
        perror("Error sending the filename - couldn't send the length\n");
        return_code = 3;
        goto cleanup;
    }

    if (send(sock, filename, strlen(filename) + 1, 0) < 0) {
        perror("Error sending the filename - couldn't send the file\n");
        return_code = 3;
        goto cleanup;
    }

    // STEP 3: DOWNLOAD THE FILE
    // check if the "received" folder exists
    struct stat st = {0};
    if (stat("./client-data/received", &st) == -1) {
        mkdir("./client-data/received", 0755);
    }

    // get the file size
    uint32_t file_size_net;
    if (recv(sock, &file_size_net, sizeof(file_size_net), 0) < 0) {
        perror("Error receiving the size of the file\n");
        return_code = 4;
        goto cleanup;
    }
    const uint32_t file_size = ntohl(file_size_net);

    // check if our request was rejected
    if (file_size == 0) {
        perror("Request rejected by peer\n");
        return_code = 8;
        goto cleanup;
    }

    // open and write in the file
    char path[1024];
    snprintf(path, sizeof(path), "./client-data/received/%s", filename);
    fp = fopen(path, "wb");
    if (!fp) {
        perror("Couldn't open file for writing");
        return_code = 5;
        goto cleanup;
    }

    uint32_t remaining = file_size;
    char buffer[4096];
    while (remaining > 0) {
        const uint32_t to_read = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const ssize_t result = recv(sock, buffer, to_read, 0);
        if (result < 0) {
            perror("Couldn't read the whole file from the peer");
            return_code = 4;
            goto cleanup;
        }

        fwrite(buffer, sizeof(char), result, fp);
        remaining -= result;
    }

    cleanup:
    if (fp)
        fclose(fp);
    close(sock);
    return return_code;
}

/// Adds a new downloaded file from the ./client-data/received folder in the ClientFiles and notifies the server
///     about it.
/// @param cf a pointer to the ClientFiles
/// @param server_sock the socket used to talk to the server
/// @param filename the new downloaded file which we want registered
/// @return 0 on success; 3 on send error; 9 on malloc error
int update_CF_and_server_on_new_file(ClientFiles *cf, const int server_sock, const char *filename) {
    // add the file in the client files
    if (client_files_add_new_file(cf, filename) != 0) {
        perror("Couldn't add new file to ClientFiles");
        return 9;
    }

    // notify the server about the new file
    const uint16_t message_type = htons(MSG_ANNOUNCE_FILE);
    if (send(server_sock, &message_type, sizeof(message_type), 0) < 0) {
        perror("Error sending the new file to the server - couldn't send the message type");
        return 3;
    }

    uint32_t filename_len = strlen(filename) + 1;
    filename_len = htonl(filename_len);
    if (send(server_sock, &filename_len, sizeof(uint32_t), 0) < 0) {
        perror("Error sending the filename - couldn't send the length\n");
        return 3;
    }

    if (send(server_sock, filename, strlen(filename) + 1, 0) < 0) {
        perror("Error sending the filename - couldn't send the file\n");
        return 3;
    }

    return 0;
}

int download_file(const int server_sock, ClientFiles *cf, const char *filename) {
    // Firstly, ask the server for which peers have the file
    Peer *peers;
    uint32_t peer_count = 0;
    int result = request_owners_of_file(server_sock, cf, filename, &peers, &peer_count);
    if (result != 0) {
        return result;
    }

    // Then, find the best peer to contact
    const uint32_t peer_to_call = choose_best_peer(peers, peer_count);

    // Contact the peer and download the file
    result = get_file_from_peer(filename, peers[peer_to_call]);
    if (result != 0) {
        return result;
    }

    // Finally, announce the server about our new file and add it in our ClientFiles registry
    return update_CF_and_server_on_new_file(cf, server_sock, filename);
}

/// waits at the reading end of the communication channel with the peer and reads the filename that the peer requests and validates it
/// @param peer_sock the peer's socket
/// @param cf a pointer to the ClientFiles
/// @param filename_out a pointer to the beginning of a char array. Output parameter, representing the filename the peer requests
/// @return 0 if successfully; 1 if the filename is rejected (potentially dangerous); 4 in case of a recv error
int get_file_request(const int peer_sock, ClientFiles *cf, char **filename_out) {
    // get the filename length and validate it
    uint32_t net_filename_size;
    if (recv(peer_sock, &net_filename_size, sizeof(net_filename_size), MSG_WAITALL) < 0) {
        perror("Couldn't receive the size of the file when trying to serve another peer");
        return 4;
    }
    const uint32_t filename_size = ntohl(net_filename_size);
    if (filename_size > 1024) {
        return 1;
    }

    // get the filename
    char *filename = (char *) malloc(sizeof(char) * filename_size);
    if (recv(peer_sock, filename, filename_size, MSG_WAITALL) < 0) {
        perror("Couldn't receive the filename when trying to serve another peer");
        return 4;
    }

    // validate the filename
    if (strstr(filename, "..") || strchr(filename, '/') || strchr(filename, '\\') || filename[0] == '\0') {
        return 1;
    }

    if (!client_files_contains(cf, filename)) {
        printf("We don't actually have that file\n");
        return 1;
    }

    *filename_out = filename;
    return 0;
}

/// sends the value 0 to the connected computer
/// @return 0 if successfully; 3 if case of a send error
int send_zero(const int peer_sock) {
    const uint32_t zero = 0;
    if (send(peer_sock, &zero, sizeof(zero), 0) < 0) {
        perror("Couldn't send the rejection to peer while trying to serve peer\n");
        return 3;
    }
    return 0;
}

/// opens and reads from the file with the given filename and sends it over the network through the provided socket
/// @param peer_sock the peer's socket
/// @param cf a pointer to the ClientFiles
/// @param filename the name of the file to be sent (not the relative path)
/// @return 0 if successfully; 1 - operation rejected; 3 - send error; 5 - fopen / stat / fread error
int upload_file(const int peer_sock, const ClientFiles *cf, const char *filename) {
    // open the file
    char path[1024];
    snprintf(path, sizeof(path), "./client-data/%s", filename);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror("Couldn't not open the requested file for reading\n");
        return 5;
    }

    // send the length of the file
    struct stat st;
    if (stat(path, &st) < 0) {
        perror("Stat failed\n");
        fclose(fp);
        return 5;
    }

    const uint32_t file_size = st.st_size;
    if (file_size > UINT32_MAX) {
        perror("Requested file size is too big\n");
        fclose(fp);
        if (send_zero(peer_sock) == 3) {
            return 3;
        }
        return 1;
    }

    const uint32_t net_file_size = htonl(st.st_size);
    if (send(peer_sock, &net_file_size, sizeof(net_file_size), 0) < 0) {
        perror("Couldn't send the size of the file\n");
        fclose(fp);
        return 3;
    }

    // send the file
    char buffer[4096];
    uint32_t remaining = file_size;
    while (remaining > 0) {
        // decide how much to read
        const uint32_t to_read = remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        // read from the file
        const size_t managed_to_read = fread(buffer, sizeof(char), to_read, fp);
        if (managed_to_read == 0) {
            if (ferror(fp)) {
                perror("Couldn't read the file requested by the peer\n");
                fclose(fp);
                return 5;
            }
            break; // EOF
        }

        // loop until you send everything that you read from the file
        uint32_t total_sent = 0;
        while (total_sent < managed_to_read) {
            const uint32_t sent_now = send(peer_sock, buffer + total_sent, managed_to_read - total_sent, 0);
            if (sent_now <= 0) {
                perror("Couldn't send the file requested by the peer\n");
                fclose(fp);
                return 3;
            }

            total_sent += sent_now;
        }

        remaining -= managed_to_read;
    }

    fclose(fp);
    return 0;
}

int serve_peer(const int peer_sock, ClientFiles *cf) {
    // STEP 1: LISTEN FOR A PEER'S REQUEST
    char *filename;
    int result = get_file_request(peer_sock, cf, &filename);

    if (result != 0) {
        if (result == 1) {
            // filename may be dangerous => reject operation (send the value 0)
            if (send_zero(peer_sock) == 3) {
                free(filename);
                return 3;
            }
        }

        free(filename);
        return result;
    }

    #ifdef DEBUG
    printf("\nA peer wants to download the file %s, serving...\n", filename);
    #endif


    // STEP 2: SEND THE FILE
    char *path = client_files_get_relative_path_for_filename(cf, filename);
    result = upload_file(peer_sock, cf, path);
    free(path);

    free(filename);
    return result;
}
