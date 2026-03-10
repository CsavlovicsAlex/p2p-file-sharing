#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "file_scan.h"
#include "peer.h"
#include "communication.h"

typedef struct {
    int socket;
    ClientFiles *cf;
}thread_data;

void read_filename (char *filename) {
    printf("\nEnter filename you want to download: ");
    fflush(stdout);

    if (scanf("%1023s", filename) != 1) {
        // input error
        perror("Couldn't read the name of the file!\n");
        exit(1);
    }

    const int next_char = getchar();
    if (next_char != '\n'&& next_char != EOF) {
        // filename too long
        perror("\nFilename too long!\n");
        clear_input_buffer();
        exit(1);
    }
}

/// prints appropriate messages / terminates the execution of the program, depending on the return parameter of the function call download_file
/// @param result the result of the function call download_file
void interpret_download_file_result(const int result) {
    if (result >= 1 && result <= 5) {
        exit(1);
    }

    switch (result) {
        case 0:
            break;
        case 6:
            printf("You already have the file in the ./client-data folder!\n");
            break;
        case 7:
            printf("No peers have the chosen file!\n");
            break;
        case 8:
            printf("Your request operation was rejected!\n");
            break;
        default:
            break;
    }
}

void *request_handler (void *arg) {
    pthread_detach(pthread_self());

    thread_data *td = (thread_data *) arg;

    if (serve_peer(td->socket, td->cf) != 0) {
        printf("Warning: failed to serve peer on socket %d\n", td->socket);
    }

    close(td->socket);
    free(td);
    return NULL;
}

void *accept_connections (void *arg) {
    thread_data *td = (thread_data *) arg;

    while (1) {
        const int peer_socket = accept_connection(td->socket);

        thread_data *new_td = malloc(sizeof(thread_data));
        if (! new_td) {
            perror("malloc failed");
            close(peer_socket);
            continue;
        }

        new_td->socket = peer_socket;
        new_td->cf = td->cf;

        pthread_t tid;
        pthread_create(&tid, NULL, request_handler, new_td);
    }

    close(td->socket);
    free(td);
    return NULL;
}

int main(const int argc, char *argv[]) {
    int port = 1234;
    char *ip_address = "127.0.0.1";
    if (argc < 3) {
        perror("Not enough arguments! Usage: ./program <IP> <port>");
        exit(1);
        // printf("This works because you are in test mode\n");
    } else {
        char *endptr;
        const long value = strtol(argv[2], &endptr, 10);

        if (*endptr != '\0' || value <= 0 || value > 65535) {
            fprintf(stderr, "Invalid port number\n");
            exit(1);
        }

        port = (int)value;
        ip_address = argv[1];
    }

    // PART 1: START THE CONNECTION
    // create the socket
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket creation failed");
        exit(1);
    }

    // connect
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(ip_address);
    server.sin_port = htons(port);
    if (connect(sock, (struct sockaddr *) &server, sizeof(server)) < 0) {
        perror("connect failed");
        exit(1);
    }

    // PART 2: CREATE THE LISTENING SOCKET (dynamic port)
    uint16_t listening_port;
    const int listening_sock = create_listening_socket(&listening_port);
    #ifdef DEBUG
    printf("Listening on port %d\n", listening_port);
    #endif

    // PART 3: SEND THE AVAILABLE FILES AND STORE THEM
    ClientFiles *cf;
    if (send_available_files(sock, listening_port, &cf) != 0) {
        perror("send_available_files failed");
        exit(1);
    }

    // PART 4: LAUNCH THE THREAD THAT SERVES OTHER PEERS
    thread_data *td = malloc(sizeof(thread_data));
    td->cf = cf;
    td->socket = listening_sock;

    pthread_t tid;
    pthread_create(&tid, NULL, accept_connections, td);

    // PART 5: REQUEST FILES
    char filename[1024];
    while (1) {
        memset(filename, 0, 1024);
        read_filename(filename);
        interpret_download_file_result(download_file(sock, cf, filename));
    }

    pthread_join(tid, NULL);
    close(sock);
    free(cf);
    return 0;
}
