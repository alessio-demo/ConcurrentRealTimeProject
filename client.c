/**
 * @file client.c
 * @brief TCP Client for acquiring webcam frames and sending them to a server.
 *
 * This program loops to:
 * 1. Acquire a frame using an external system command (fswebcam).
 * 2. Store the frame locally.
 * 3. Connect to the server.
 * 4. Send the file metadata and content via TCP.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1" // Change to server IP if on different machine
#define PORT 8080
#define BUFFER_SIZE 1024

/**
 * @brief Acquires a frame from the webcam using a system call.
 * * Uses 'fswebcam' to capture a JPEG image.
 * * @param filename The name of the file to save locally.
 * @return int 0 on success, -1 on failure.
 */
int acquire_frame(const char *filename) {
    char command[256];
    // -r: resolution, --no-banner: remove timestamp text, -q: quiet mode
    snprintf(command, sizeof(command), "fswebcam -r 640x480 --jpeg 85 --no-banner -q %s", filename);
    
    printf("[CLIENT] Acquiring frame: %s\n", filename);
    int status = system(command);
    
    if (status != 0) {
        fprintf(stderr, "[ERROR] Frame acquisition failed. Is fswebcam installed?\n");
        return -1;
    }
    return 0;
}

/**
 * @brief Sends a file to the server using the defined protocol.
 * * Protocol:
 * [Int: Filename Length] [String: Filename] [Long: File Size] [Binary: Data]
 * * @param filename The name of the file to read and send.
 */
void send_frame_to_server(const char *filename) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    FILE *fp;

    // Create Socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return;
    }

    // Connect to Server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return;
    }

    // Open local file to read
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("File open error");
        close(sock);
        return;
    }

    // Calculate file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET); // Reset pointer to start

    // --- PROTOCOL START ---
    
    // 1. Send Filename Length
    int name_len = strlen(filename);
    send(sock, &name_len, sizeof(name_len), 0);

    // 2. Send Filename
    send(sock, filename, name_len, 0);

    // 3. Send File Size
    send(sock, &file_size, sizeof(file_size), 0);

    // 4. Send File Data
    char buffer[BUFFER_SIZE];
    int bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        send(sock, buffer, bytes_read, 0);
    }
    
    // --- PROTOCOL END ---

    printf("[CLIENT] Sent '%s' to server.\n", filename);

    fclose(fp);
    close(sock);
}

int main() {
    int frame_count = 0;
    char filename[50];

    // Main Acquisition Loop
    // In a real scenario, this might run infinitely or on a timer.
    // Here we capture 3 frames as an example.
    while (frame_count < 3) {
        sprintf(filename, "frame_%d.jpg", frame_count);
        
        // 1. Acquire
        if (acquire_frame(filename) == 0) {
            // 2. Send
            send_frame_to_server(filename);
        }

        frame_count++;
        sleep(2); // Wait 2 seconds before next frame
    }

    return 0;
}