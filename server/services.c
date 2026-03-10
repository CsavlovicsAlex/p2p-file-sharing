#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "file_registry.h"
#include "message_types.h"
#include "services.h"

void log_errno(const char *msg) {
    perror(msg);
}

int connection_closed_event(const int result, const char *msg, FileRegistry *file_registry, const int client_socket) {
    file_registry_remove_all_file_ownership(file_registry, client_socket);
    close(client_socket);

    if (result != 0) {
        log_errno(msg);
        return result;
    }
    return 9;
}

int get_and_store_client_files(FileRegistry *file_registry, const int client_socket, const struct sockaddr_in * connected_client, RegistryPeer *out_peer) {
    ssize_t result;
    // STEP 1: GET THE LISTENING PORT
    // get the listening port of the client
    uint16_t net_listening_port;
    if ((result =recv(client_socket, &net_listening_port, sizeof(uint16_t), MSG_WAITALL)) <= 0) {
        return connection_closed_event((int)result, "recv failed", file_registry, client_socket);
    }
    const uint16_t listening_port = ntohs(net_listening_port);

    // create a new client Peer for storing its files
    RegistryPeer client;
    client.peer.port = listening_port;
    strcpy(client.peer.ip, inet_ntoa(connected_client->sin_addr));
    client.owner_socket = client_socket;
    *out_peer = client;

    // STEP 2: RECEIVE THE FILES
    // receive the files from the client
    uint32_t net_cont_files_len;
    if ((result = recv(client_socket, &net_cont_files_len, sizeof(uint32_t), MSG_WAITALL)) <= 0) {
        return connection_closed_event((int) result, "recv failed", file_registry, client_socket);
    }
    const uint32_t cont_files_len = ntohl(net_cont_files_len);

    char *contiguous_files = malloc(cont_files_len * sizeof(char) + 1);
    if ((result = recv(client_socket, contiguous_files, cont_files_len * sizeof(char), MSG_WAITALL)) <= 0) {
        free(contiguous_files);
        return connection_closed_event((int) result, "recv failed", file_registry, client_socket);
    }
    contiguous_files[cont_files_len] = '\0';

    // STEP 3: STORE THE FILES
    int pos = 0;
    while (contiguous_files[pos] != '\0') {
        // add each individual file in the registry
        const int fr_result = file_registry_add_file_ownership(file_registry, contiguous_files + pos, client);
        if (fr_result == 2) {
            perror("file_registry_add_file_ownership failed");
            free(contiguous_files);
            return 2;
        }

        pos = pos + 1 + (int) strlen(contiguous_files + pos);
    }

    free(contiguous_files);
    return 0;
}

/// Searches the FileRegistry for all the Peers that have the given filename, creates a list out of them and sends it to the client
/// @param file_registry a pointer to the FileRegistry
/// @param client_socket the client's socket
/// @param filename the filename which the client asked for
/// @return 0 on success; 3 on send error; 9 on closed connection
int serve_client_send_owners_of_file (FileRegistry *file_registry, const int client_socket, char filename[]) {
    ssize_t result;

    // PART 1: count how many peers have that file
    const int number_of_file_owners = ips_with_filename(file_registry, filename);

    #ifdef DEBUG
    printf("We received the file %s from the client; there are %d IPs that have it\n", filename, number_of_file_owners);
    #endif

    // PART 2: if no one has that file send the value 0 and exit the function
    if (number_of_file_owners == 0) {
        uint32_t zero = 0;
        zero = htonl(zero);
        if ((result = send(client_socket, &zero, sizeof(uint32_t), 0)) <= 0) {
            return connection_closed_event((int) result, "send failed\n", file_registry, client_socket);
        }
        return 0;
    }

    // PART 3: create an array of the IPs (in order to avoid calling the same function in part 4 too) + compute the size of the buffer
    RegistryPeer *peers = malloc(sizeof(RegistryPeer) * number_of_file_owners);

    uint32_t buffer_size = 0;
    buffer_size += sizeof(uint32_t);            // for the number of peers
    for (int i = 0; i < number_of_file_owners; i++) {
        if (loop_peers_for_file(file_registry, filename, i, peers + i) != 0) {     // populate the RegistryPeers array
            perror("loop_peers_for_file() failed");
            free(peers);
            return 2;
        }

        buffer_size += sizeof(uint32_t);        // for ip_len
        buffer_size += strlen(peers[i].peer.ip);     // the size of the ip itself
        buffer_size += sizeof(uint16_t);        // for port
    }

    // PART 4: create the actual buffer in which we put the data
    // buffer format: [uint32_t number_of_peers]
    //                  [uint32_t ip_len][ip_len bytes][uint16_t port]  -- for first peer. Repeat for others
    uint8_t *buffer = malloc(buffer_size);
    uint8_t *p = buffer;

    const uint32_t net_owners = htonl(number_of_file_owners);
    memcpy(p, &net_owners, sizeof(uint32_t));
    p += sizeof(uint32_t);

    for (int i = 0; i < number_of_file_owners; i++) {
        // put the ip length
        uint32_t ip_len = htonl(strlen(peers[i].peer.ip));
        memcpy(p, &ip_len, sizeof(uint32_t));
        p += sizeof(uint32_t);

        // put the ip address
        memcpy(p, peers[i].peer.ip, strlen(peers[i].peer.ip));
        p += strlen(peers[i].peer.ip);

        // put the port
        uint16_t net_port = htons(peers[i].peer.port);
        memcpy(p, &net_port, sizeof(uint16_t));
        p += sizeof(uint16_t);
    }

    free(peers);

    // PART 5: send the data: firstly the length of the buffer and then the buffer itself
    const uint32_t net_buffer_size = htonl(buffer_size);
    if ((result = send(client_socket, &net_buffer_size, sizeof(uint32_t), 0)) <= 0) {
        free(buffer);

        return connection_closed_event((int) result, "send failed - couldn't send the length of the buffer\n", file_registry, client_socket);
    }

    if ((result = send(client_socket, buffer, buffer_size, 0)) <= 0) {
        free(buffer);

        return connection_closed_event((int) result, "send failed - couldn't send the buffer\n", file_registry, client_socket);
    }

    free(buffer);
    return 0;
}

int serve_client(FileRegistry *file_registry, const RegistryPeer *client_data) {
    const int client_socket = client_data->owner_socket;
    ssize_t result;

    // PART 1: get the message type from the client
    uint16_t message_type;
    if ((result = recv(client_socket, &message_type, sizeof(uint16_t), MSG_WAITALL)) <= 0) {
        return connection_closed_event((int) result, "recv failed - couldn't get the message type from the client", file_registry, client_socket);
    }
    message_type = ntohs(message_type);

    // PART 2: get the filename from the client
    uint32_t payload_len;
    if ((result = recv(client_socket, &payload_len, sizeof(uint32_t), MSG_WAITALL)) <= 0) {
        return connection_closed_event((int) result, "recv failed - couldn't get the filename length from client\n", file_registry, client_socket);
    }
    payload_len = ntohl(payload_len);

    char filename[payload_len];
    if ((result = recv(client_socket, filename, payload_len, MSG_WAITALL)) <= 0) {
        return connection_closed_event((int) result, "recv failed - couldn't get the filename from client\n", file_registry, client_socket);
    }
    filename[payload_len - 1] = '\0';

    // PART 3: choose the appropriate response method based on the message type
    switch (message_type) {
        case MSG_REQUEST_FILE:
            return serve_client_send_owners_of_file(file_registry, client_socket, filename);
        case MSG_ANNOUNCE_FILE:
            #ifdef DEBUG
            printf("Client wants to announce a new file: %s\n", filename);
            #endif
            return file_registry_add_file_ownership(file_registry, filename, *client_data);
        default:
            return 4;
    }
}