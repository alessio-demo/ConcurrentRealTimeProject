/**
 * @file client.c
 * @brief TCP Client for acquiring webcam frames and sending them to the server.
 */

// Include standard I/O library.
#include <stdio.h>
// Include standard library.
#include <stdlib.h>
// Include string manipulation library.
#include <string.h>
// Include POSIX operating system API (unistd.h).
#include <unistd.h>
// Include definitions for internet operations.
#include <arpa/inet.h>
// Include main socket definitions.
#include <sys/socket.h>

// Define the Server IP address (Localhost for testing).
#define SERVER_IP "127.0.0.1" 
// Define the port number to connect to.
#define PORT 8080
// Define buffer size for file reading.
#define BUFFER_SIZE 1024

/**
 * @brief Acquires a frame using a system command.
 * @param filename The name of the file to be created.
 * @return int 0 on success, -1 on failure.
 */
int acquire_frame(const char *filename) {
    // Buffer to hold the shell command string.
    char command[256];
    
    // Construct the command string using snprintf.
    // "fswebcam" is the external tool. 
    // "-r 640x480": set resolution.
    // "--jpeg 85": set format to JPEG with 85% quality.
    // "--no-banner": disable the text overlay.
    // "-q": quiet mode (less output).
    snprintf(command, sizeof(command), "fswebcam -r 640x480 --jpeg 85 --no-banner -q %s", filename);
    
    // Print status to console.
    printf("[CLIENT] Acquiring frame: %s\n", filename);
    
    // Execute the command in the shell. system() returns the exit status.
    int status = system(command);
    
    // Check if the command executed successfully (0 usually means success).
    if (status != 0) {
        // Print error to standard error output.
        fprintf(stderr, "[ERROR] Frame acquisition failed. Is fswebcam installed?\n");
        // Return error code.
        return -1;
    }
    // Return success code.
    return 0;
}

/**
 * @brief Sends the local file to the server via TCP.
 * @param filename The name of the file to send.
 */
void send_frame_to_server(const char *filename) {
    // File descriptor for the socket.
    int sock = 0;
    // Struct for server address configuration.
    struct sockaddr_in serv_addr;
    // File pointer for reading the local image.
    FILE *fp;

    // Create a TCP socket.
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        // Print error if socket creation fails.
        perror("Socket creation error");
        // Exit function.
        return;
    }

    // Set address family to IPv4.
    serv_addr.sin_family = AF_INET;
    // Set port number, converting to network byte order.
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 address from text to binary form and store it in serv_addr.
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        // Print error if IP address is invalid.
        perror("Invalid address/ Address not supported");
        // Exit function.
        return;
    }

    // Attempt to connect to the server.
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        // Print error if connection fails (e.g., server not running).
        perror("Connection Failed");
        // Exit function.
        return;
    }

    // Open the local file in binary read mode ("rb").
    fp = fopen(filename, "rb");
    // Check if file opened successfully.
    if (fp == NULL) {
        // Print error if file not found.
        perror("File open error");
        // Close the socket before returning.
        close(sock);
        // Exit function.
        return;
    }

    // Move file pointer to the end of the file to calculate size.
    fseek(fp, 0, SEEK_END);
    // Get the current position (which is the file size in bytes).
    long file_size = ftell(fp);
    // Reset file pointer to the beginning of the file.
    fseek(fp, 0, SEEK_SET); 

    // --- PROTOCOL: SEND METADATA ---
    
    // Calculate length of filename string.
    int name_len = strlen(filename);
    // Send filename length to server.
    send(sock, &name_len, sizeof(name_len), 0);

    // Send the actual filename string.
    send(sock, filename, name_len, 0);

    // Send the file size.
    send(sock, &file_size, sizeof(file_size), 0);

    // --- PROTOCOL: SEND DATA ---

    // Buffer for reading file data.
    char buffer[BUFFER_SIZE];
    // Variable to track bytes read from file.
    int bytes_read;
    
    // Loop to read file in chunks and send them.
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        // Send the chunk of data to the server.
        send(sock, buffer, bytes_read, 0);
    }
    
    // Print success message.
    printf("[CLIENT] Sent '%s' to server.\n", filename);

    // Close the file.
    fclose(fp);
    // Close the socket connection.
    close(sock);
}

// Main entry point of the client.
int main() {
    // Counter for the number of frames acquired.
    int frame_count = 0;
    // Buffer to hold the generated filename.
    char filename[50];

    // Loop to capture 3 frames (as an example limit).
    while (frame_count < 3) {
        // Format the filename (e.g., "frame_0.jpg").
        sprintf(filename, "frame_%d.jpg", frame_count);
        
        // Call function to acquire frame from webcam.
        if (acquire_frame(filename) == 0) {
            // If acquisition successful, call function to send to server.
            send_frame_to_server(filename);
        }

        // Increment frame counter.
        frame_count++;
        // Pause execution for 2 seconds before next capture.
        sleep(2); 
    }

    // Return success code.
    return 0;
}