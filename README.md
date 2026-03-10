

# C Peer-to-Peer File Sharing System
**Platform:** UNIX (Linux / macOS)

A peer-to-peer file sharing system written in **C** where clients discover each other through a **central server** and transfer files **directly between peers**.

The architecture is inspired by early **Napster-style networks**, where the server only manages metadata while actual file transfers happen directly between clients.

The project focuses on **low-level networking, concurrency, and systems programming** using POSIX sockets and multithreading.

---

# Features

- Client–server architecture
- Peer-to-peer file transfers
- Automatic client directory scanning
- Centralized file indexing
- Multi-client support
- **Multithreaded server**
- **Multithreaded client**
- Multiple simultaneous file transfers

---

# Architecture

The system consists of two main components.

## Server

The server acts as a **directory service**.

Responsibilities:

- Accept client connections
- Receive the list of files shared by each client
- Maintain a global index of available files
- Respond to file list requests
- Help clients discover which peer owns a requested file

The server **does not transfer files**. It only coordinates peers.

## Client

Each client performs several tasks:

- Scans a local shared directory
- Registers its files with the server
- Requests the list of available files
- Selects a peer hosting the desired file
- Downloads files directly from that peer

Clients also act as **mini-servers**, allowing other peers to download files from them.

---

# Multithreading

Both the **server** and the **client** are multithreaded.

## Server

The server creates a new thread for each connected client. This allows it to:

- Handle multiple client connections simultaneously
- Process requests without blocking other clients

## Client

The client uses multiple threads to:

- Communicate with the main server
- Serve files to other peers
- Download files from peers

This enables **parallel transfers and responsive communication**.

---

# Project Structure

```
project/
│
├── client/
│   ├── communication.c # Logic responsible for talking with the server and other clients
│   ├── file_scan.c     # Manages the client side file-tracker structure (ClientFiles)
│   └── client.c        # Coordinates the actions on the client side
├── server/
│   ├── services.c      # Manages interactions with other clients
│   ├── file_registry.c # Manages the server side file-tracker (FileRegistry)
│   └── server.c        # Coordinates the actions on the server side
├── shared/
│   ├── communication_protocol.txt  # Describes the protocol used by the programs to talk between themselves
│   ├── message_types.h         # Defines macros used by the server to know what is the communication purpose of a client
│   └── peer.h                  # A common structure between server and client
├── client-data/
│   ├── received/               # The downloaded files
│   └──                         # The files the user wants to share
└── README.md
```

---
# Platform

This project is currently designed for **UNIX-based operating systems** such as:

- Linux
- macOS

It relies on **POSIX sockets and POSIX threads**, which are not directly supported on Windows.

Compilation on Windows is not currently supported without compatibility layers such as WSL.

# Build

Compile using GCC:

```
gcc server/server.c server/services.c server/file_registry.c -o server -lpthread

gcc client/client.c client/communication.c client/file_scan.c -o client -lpthread
```

---

# Usage

Start the server:

```
./server <server_port>
```

Run one or more clients:

```
./client <server_IP> <server_port>
```

Clients will:

1. Scan the shared directory
2. Register files with the server
3. Request the list of available files
4. Download files directly from peers

---

# Roadmap

Implemented:

- Basic client–server communication
- Directory scanning
- File indexing on server
- File list retrieval
- Peer-to-peer file download
- Multithreaded client
- Multithreaded server

Planned improvements:

- Transfer progress indicator
- Connection heartbeat mechanism
- Improved error handling
- File integrity verification

---

# Technologies

- C
- POSIX sockets
- POSIX threads (pthreads)
- Linux networking

---

# Learning Goals

This project explores core concepts in **systems programming**, including:

- Socket programming
- Network protocol design
- Concurrent programming
- Resource synchronization
- Peer-to-peer architecture

---

# Inspiration

The architecture is inspired by early peer-to-peer systems such as **Napster**, where a central index server enables peers to discover and exchange files directly.