#pragma once
#include <stdint.h>
#include <arpa/inet.h>

typedef struct {
    char ip[INET_ADDRSTRLEN];
    uint16_t port;
} Peer;