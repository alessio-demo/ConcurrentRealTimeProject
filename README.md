# Webcam Frame Acquisition & Remote Storage System

## 1. Project Overview
This project implements a **Client-Server architecture** in C designed to acquire image frames from a webcam on a Linux system and store them both locally and remotely.

The system consists of two distinct processes:
* **Client:** Acquires frames from the webcam, saves them to the local disk, and transfers them via TCP/IP to the server.
* **Server:** Listens for incoming connections, receives the file metadata (filename, size) and the binary content, and stores the frames on the server's disk.

## 2. Prerequisites & Dependencies
To run this software, the following environment is required:

* **OS:** Linux (Ubuntu/Debian recommended).
* **Compiler:** GCC (GNU Compiler Collection).
* **Hardware:** A USB Webcam or a virtual video device.
* **External Utility:** `fswebcam`.
    * This tool is used for the physical acquisition and JPEG encoding of the frames to ensure the C code remains modular and focused on networking.
    * **Installation:**
      ```bash
      sudo apt-get install fswebcam
      ```

## 3. Compilation
Open a terminal in the project directory and run the following commands to compile both the server and the client:

```bash
# Compile the Server
gcc server.c -o server

# Compile the Client
gcc client.c -o client