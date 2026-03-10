cmake_minimum_required(VERSION 3.20)
project(FileSharing C)

set(CMAKE_C_STANDARD 23)

# --- Client executable ---
add_executable(client
        client/client.c
        client/file_scan.c
        client/communication.c
)
target_include_directories(client PRIVATE shared)

# --- Server executable ---
add_executable(server
        server/server.c
        server/file_registry.c
        server/services.c
)
target_include_directories(server PRIVATE shared)