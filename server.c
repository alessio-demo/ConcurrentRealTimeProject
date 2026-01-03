/**
 * @file server.c
 * @brief TCP Server acting as a Consumer. Receives raw video frames via network and persists them to disk.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

/* Defines port 8080 as the listening port. This must match the configuration in the client. */
#define PORT 8080
/* Defines a 4KB buffer for reading data chunks. This matches the standard page size, offering a balance between memory usage and system call overhead. */
#define BUFFER_SIZE 4096

/**
 * @brief Encapsulates the logic for handling a single connected client.
 * Implements the application-layer protocol to distinguish filenames and file data within the continuous TCP byte stream.
 */
void handle_client(int client_socket) {
    int name_len;
    char filename[256];
    long file_size;
    long total_received;
    int bytes_read;
    char buffer[BUFFER_SIZE];

    /* Enters an infinite loop to continuously process files sent by the client. Terminates only upon client disconnection or network error. */
    while(1) {
        
        /* --- METADATA RECEPTION PHASE --- */

        /* Attempts to read the integer representing the filename length. Since TCP is a stream protocol, knowing the exact length is necessary to parse the subsequent string. Returns <= 0 on disconnection. */
        if (recv(client_socket, &name_len, sizeof(name_len), 0) <= 0) {
            printf("[SERVER] Client disconnected or handshake failed.\n");
            break;
        }

        /* Clears the filename buffer to ensure no residual data affects the new string. */
        memset(filename, 0, sizeof(filename));

        /* Reads the exact number of bytes specified by name_len to retrieve the filename string. This ensures correct alignment for the subsequent file size data. */
        if (recv(client_socket, filename, name_len, 0) <= 0) {
            perror("[SERVER] Error receiving filename string");
            break; 
        }

        /* Reads the long integer representing the total size of the incoming raw image. This value determines when to stop reading data for the current file. */
        if (recv(client_socket, &file_size, sizeof(file_size), 0) <= 0) {
            perror("[SERVER] Error receiving file size");
            break;
        }

        printf("[SERVER] Incoming file: %s (%ld bytes)\n", filename, file_size);

        /* --- DISK I/O PREPARATION --- */

        /* Opens a new file on the local disk using the received filename. "wb" (Write Binary) mode ensures raw byte data is written without text formatting or translation. */
        FILE *fp = fopen(filename, "wb");
        if (fp == NULL) {
            perror("[SERVER] Critical error creating file on disk");
            return;
        }

        /* --- PAYLOAD RECEPTION PHASE --- */

        /* Resets the counter to track the bytes received for the current image. */
        total_received = 0;

        /* Enters a nested loop to handle file transfer. Since the file may exceed the TCP buffer size, reception occurs in chunks until the total bytes match file_size. */
        while (total_received < file_size) {
            
            /* Calculates remaining bytes. Requests a full buffer if the remainder exceeds the buffer size; otherwise, requests only the exact remaining amount. */
            long bytes_to_read = file_size - total_received;
            if (bytes_to_read > BUFFER_SIZE) {
                bytes_to_read = BUFFER_SIZE;
            }

            /* Executes the system call to read data from the network buffer. */
            bytes_read = recv(client_socket, buffer, bytes_to_read, 0);
            
            /* Detects unexpected disconnections or errors during transfer. Breaks the loop to prevent data corruption. */
            if (bytes_read <= 0) {
                printf("[SERVER] Unexpected disconnection during file transfer.\n");
                break; 
            }

            /* Writes the received data chunk directly to disk. This minimizes memory usage by avoiding loading the entire file into RAM. */
            fwrite(buffer, 1, bytes_read, fp);
            
            /* Updates the progress counter. */
            total_received += bytes_read;
        }

        /* Closes the file handle to flush write buffers and ensure physical storage on disk. */
        fclose(fp);
        printf("[SERVER] Successfully saved: %s\n", filename);
    }

    /* Closes the client socket to release the file descriptor resource back to the operating system. */
    close(client_socket);
}

int main() {
    int server_fd;
    int new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    /* Creates a socket endpoint. AF_INET specifies IPv4, and SOCK_STREAM specifies TCP for reliable, ordered data delivery. */
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    /* Configures the address structure to bind the socket to all available network interfaces (INADDR_ANY) and the specific port. htons converts the port number to network byte order. */
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    /* Binds the socket to the address and port, instructing the OS to forward packets destined for this port to the process. */
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    /* Sets the socket to passive mode, waiting for incoming connections. The backlog parameter 3 defines the maximum size of the pending connection queue. */
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("[SERVER] Service started. Listening on port %d...\n", PORT);

    /* Main server loop handles incoming connections sequentially. */
    while (1) {
        /* Extracts the first connection request from the queue, creates a new connected socket, and returns a new file descriptor. Blocks the process until a client connects. */
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        printf("[SERVER] New client connected.\n");
        
        /* Passes the new connection descriptor to the handler function for data processing. */
        handle_client(new_socket);
        
        printf("[SERVER] Client session ended.\n");
    }

    return 0;
}