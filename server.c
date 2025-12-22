/**
 * @file server.c
 * @brief TCP Server for receiving and storing webcam frames.
 */

// Include standard input/output library for printf, perror, etc.
#include <stdio.h>
// Include standard library for memory allocation and exit functions.
#include <stdlib.h>
// Include string library for functions like memset, strlen.
#include <string.h>
// Include unistd for system calls like close, read, write.
#include <unistd.h>
// Include arpa/inet for network address conversions.
#include <arpa/inet.h>
// Include sys/socket for core socket functions.
#include <sys/socket.h>

// Define the port number on which the server will listen.
#define PORT 8080
// Define the size of the buffer used for data transfer (1KB).
#define BUFFER_SIZE 1024

/**
 * @brief Handles the specific logic for a connected client.
 * @param client_socket File descriptor for the active connection.
 */
void handle_client(int client_socket) {
    // Variable to store the length of the filename (integer).
    int name_len;
    // Buffer to store the filename string (max 256 chars).
    char filename[256];
    // Variable to store the total size of the incoming file (long integer).
    long file_size;
    
    // --- STEP 1: RECEIVE FILENAME LENGTH ---
    // Read the integer 'name_len' from the socket.
    // recv() returns the number of bytes read; if <= 0, it means error or disconnect.
    if (recv(client_socket, &name_len, sizeof(name_len), 0) <= 0) {
        // Print an error message if receiving fails.
        perror("Failed to receive filename length");
        // Exit the function if we cannot proceed.
        return;
    }

    // --- STEP 2: RECEIVE FILENAME ---
    // Clear the filename buffer with zeros to ensure safety.
    memset(filename, 0, sizeof(filename));
    // Read the actual filename string using the length received in Step 1.
    if (recv(client_socket, filename, name_len, 0) <= 0) {
        // Print an error message if receiving the filename fails.
        perror("Failed to receive filename");
        // Exit the function.
        return;
    }
    
    // Create a buffer to hold the final path where the file will be saved.
    char save_path[300];
    // Format the string: prepend "server_" to the received filename.
    // This helps distinguish files saved by the server.
    sprintf(save_path, "server_%s", filename);

    // --- STEP 3: RECEIVE FILE SIZE ---
    // Read the file size (long) from the socket.
    if (recv(client_socket, &file_size, sizeof(file_size), 0) <= 0) {
        // Print error if receiving file size fails.
        perror("Failed to receive file size");
        // Exit the function.
        return;
    }

    // Print a log message indicating the start of the file download.
    printf("[INFO] Receiving '%s' (%ld bytes)...\n", filename, file_size);

    // --- STEP 4: RECEIVE FILE DATA ---
    // Open a file pointer for writing in binary mode ("wb").
    FILE *fp = fopen(save_path, "wb");
    // Check if the file was opened successfully.
    if (fp == NULL) {
        // Print error if file creation on disk fails.
        perror("Error creating file on server");
        // Exit function.
        return;
    }

    // Buffer to hold chunks of data received from the network.
    char buffer[BUFFER_SIZE];
    // Counter to track how many bytes have been received so far.
    long total_received = 0;
    // Variable to store bytes read in a single recv call.
    int bytes_read;

    // Loop until the total received bytes equal the expected file size.
    while (total_received < file_size) {
        // Receive a chunk of data (up to BUFFER_SIZE) from the socket.
        bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
        
        // If bytes_read is 0 (disconnect) or -1 (error), break the loop.
        if (bytes_read <= 0) break;
        
        // Write the received chunk to the file on disk.
        // Arguments: buffer pointer, size of element (1 byte), count (bytes_read), file pointer.
        fwrite(buffer, 1, bytes_read, fp);
        
        // Update the total counter.
        total_received += bytes_read;
    }

    // Close the file explicitly to save changes.
    fclose(fp);
    // Print a success message to the console.
    printf("[SUCCESS] File saved as '%s'.\n", save_path);
}

// Main entry point of the server program.
int main() {
    // File descriptor for the server's listening socket.
    int server_fd;
    // File descriptor for the new socket created when a client connects.
    int new_socket;
    // Structure containing internet address information (IP, Port).
    struct sockaddr_in address;
    // Size of the address structure, needed for the accept() function.
    int addrlen = sizeof(address);

    // Create a socket. 
    // AF_INET = IPv4, SOCK_STREAM = TCP, 0 = Protocol (IP).
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        // Print error if socket creation fails.
        perror("Socket failed");
        // Exit with failure status.
        exit(EXIT_FAILURE);
    }

    // Configure the address structure.
    // Set address family to IPv4.
    address.sin_family = AF_INET;
    // Set IP address to INADDR_ANY (accept connections on all network interfaces).
    address.sin_addr.s_addr = INADDR_ANY;
    // Set the port number. htons() converts the integer to Network Byte Order.
    address.sin_port = htons(PORT);

    // Bind the socket to the specified IP and Port.
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        // Print error if binding fails (e.g., port already in use).
        perror("Bind failed");
        // Exit with failure status.
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections. 
    // The second argument (3) is the maximum length of the queue of pending connections.
    if (listen(server_fd, 3) < 0) {
        // Print error if listen fails.
        perror("Listen failed");
        // Exit with failure status.
        exit(EXIT_FAILURE);
    }

    // Print message indicating the server is running.
    printf("[INFO] Server listening on port %d...\n", PORT);

    // Infinite loop to keep the server running and accepting new clients.
    while (1) {
        // Accept a new incoming connection.
        // This call blocks execution until a client connects.
        // It returns a new file descriptor specifically for this connection.
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            // If accept fails, print error.
            perror("Accept failed");
            // Skip the rest of the loop and wait for the next connection.
            continue; 
        }
        
        // Log that a connection was successful.
        printf("[INFO] Connection established.\n");
        
        // Call the helper function to process the client's data.
        handle_client(new_socket);
        
        // Close the specific client socket as communication is done.
        close(new_socket); 
        
        // Log that the connection is closed.
        printf("[INFO] Connection closed.\n-------------------\n");
    }

    // Return 0 to indicate successful execution (never reached in this infinite loop).
    return 0;
}