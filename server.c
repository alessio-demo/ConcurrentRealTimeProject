/**
 * @file server.c
 * @brief TCP Server for receiving and storing image frames.
 *
 * This program creates a TCP socket, binds to a port, and listens for incoming
 * connections. Upon connection, it reads the filename, file size, and the
 * image data, storing the result on the local disk.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8080
#define BUFFER_SIZE 1024

/**
 * @brief Handles the client connection to receive the file.
 *
 * This function follows the custom protocol:
 * 1. Read filename length (int).
 * 2. Read filename (string).
 * 3. Read file size (long).
 * 4. Read file binary data in chunks.
 *
 * @param client_socket The file descriptor for the connected client socket.
 */
void handle_client(int client_socket) {
    int name_len;
    char filename[256];
    long file_size;
    
    // 1. Receive filename length
    if (recv(client_socket, &name_len, sizeof(name_len), 0) <= 0) {
        perror("Failed to receive filename length");
        return;
    }

    // 2. Receive filename
    memset(filename, 0, sizeof(filename));
    if (recv(client_socket, filename, name_len, 0) <= 0) {
        perror("Failed to receive filename");
        return;
    }
    
    // Add a prefix to distinguish server files from client files
    char save_path[300];
    sprintf(save_path, "server_%s", filename);

    // 3. Receive file size
    if (recv(client_socket, &file_size, sizeof(file_size), 0) <= 0) {
        perror("Failed to receive file size");
        return;
    }

    printf("[INFO] Receiving '%s' (%ld bytes)...\n", filename, file_size);

    // Open file for writing binary data
    FILE *fp = fopen(save_path, "wb");
    if (fp == NULL) {
        perror("Error creating file on server");
        return;
    }

    // 4. Receive file data loop
    char buffer[BUFFER_SIZE];
    long total_received = 0;
    int bytes_read;

    while (total_received < file_size) {
        bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_read <= 0) break;
        
        fwrite(buffer, 1, bytes_read, fp);
        total_received += bytes_read;
    }

    fclose(fp);
    printf("[SUCCESS] File saved as '%s'.\n", save_path);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Create socket file descriptor (IPv4, TCP)
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Define socket address structure
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    address.sin_port = htons(PORT);

    // Bind the socket to the port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Start listening for connections
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("[INFO] Server listening on port %d...\n", PORT);

    // Main loop to accept incoming connections
    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue; // Try next connection
        }
        
        printf("[INFO] Connection established.\n");
        handle_client(new_socket);
        close(new_socket); // Close connection after transfer
        printf("[INFO] Connection closed.\n-------------------\n");
    }

    return 0;
}