#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "file_registry.h"
#include "services.h"
#include "peer.h"

typedef struct {
    FileRegistry *file_registry;
    int client_socket;
    struct sockaddr_in client;
    int id;
} thread_data;

int create_listening_socket(int port) {
    // create the socket
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket creation failed");
        exit(1);
    }

    // bind the socket
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    if (bind (sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind failed");
        exit(1);
    }

    // listen
    if (listen(sock, 5) < 0) {
        perror("listen failed");
        exit(1);
    }

    return sock;
}

void* client_handler(void *arg) {
    pthread_detach(pthread_self());
    const auto td = (thread_data *) arg;
    RegistryPeer *client = malloc(sizeof(RegistryPeer));

    int operations_result = get_and_store_client_files(td->file_registry, td->client_socket, &td->client, client);
    if (operations_result != 0) {
        if (operations_result != 9) {
            printf("Thread %d encountered a malfunction in storing the client's files. Exiting...\n", td->id);
        }

        goto end;
    }

    // TEST PRINTS
#ifdef DEBUG
    printf("The files we have:\n");
    file_registry_debug_print(td->file_registry);

    char filename[] = "dummy-program1.c";
    printf("Test the new functions: \n");
    printf("File %s is owned by %d IPs\n", filename, ips_with_filename(td->file_registry, filename));

    RegistryPeer *p = malloc(sizeof(RegistryPeer));
    loop_peers_for_file(td->file_registry, filename, 0, p);

    printf("First owner: %s:%d\n", p->peer.ip, p->peer.port);
    // TEST PRINTS OVER
#endif

    while (operations_result == 0) {
        operations_result = serve_client(td->file_registry, client);
        if (operations_result != 0) {
            if (operations_result != 9) {
                printf("Operation with thread %d has stopped when serving the client. Exiting...\n", td->id);
            }
        }
    }

    end:
    if (operations_result == 9) {
        printf("Thread %d closed the connection. Goodbye!\n", td->id);
    }

    file_registry_remove_all_file_ownership(td->file_registry, td->client_socket);
    close(td->client_socket);
    free(td);
    free(client);
    return NULL;
}

int main (const int argc, char *argv[]) {
    int port = 1234;
    if (argc < 2) {
        perror("Not enough arguments! Usage: ./program <port>");
        exit(1);
        // printf("This works because you are in test mode\n");
    } else {
        char *endptr;
        const long value = strtol(argv[1], &endptr, 10);

        if (*endptr != '\0' || value <= 0 || value > 65535) {
            fprintf(stderr, "Invalid port number\n");
            exit(1);
        }

        port = (int)value;
    }

    // initialize the FileRegistry
    FileRegistry *file_registry = file_registry_create();

    // PART 1: START THE CONNECTION
    const int sock = create_listening_socket(port);

    int count = 0;

    // PART 2: SERVE THE CLIENTS
    while (1) {
        // accept the client
        struct sockaddr_in client;
        socklen_t client_size;
        memset(&client, 0, sizeof(client));
        client_size = sizeof(client);
        const int client_socket = accept(sock, (struct sockaddr *) &client, &client_size);
        if (client_socket < 0) {
            perror("accept failed");
            exit(1);
        }

        printf("Incoming connection from %s:%d\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));

        // put all the data in the thread_data struct
        thread_data* data = malloc(sizeof(thread_data));
        data->file_registry = file_registry;
        data->client_socket = client_socket;
        data->client = client;
        data->id = count++;

        // serve the client with a new thread
        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, data);
    }
}