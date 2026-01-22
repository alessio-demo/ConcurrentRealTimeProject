# V4L2 Video Acquisition and Remote TCP Storage

## 1. Project Overview
This project implements a distributed **Client-Server** system for real-time video acquisition on Linux architectures.
The primary engineering goal is to **decouple** the hardware acquisition task (which has strict timing requirements) from the disk storage task (which is subject to I/O latency).

To achieve this, the system uses:
* **Video4Linux2 (V4L2) API:** To communicate directly with the webcam driver in Kernel Space.
* **Memory Mapping (MMAP):** To access video buffers with zero-copy overhead.
* **TCP/IP Sockets:** To transfer data reliably between the acquisition unit (Client) and the storage unit (Server).

### System Architecture
The data flow moves from the hardware to the disk through a network socket, minimizing buffer copies in User Space.

```text
+-----------------+      +---------------+      +-------------------+
| Webcam Hardware |      | Kernel Space  |      | User Space (Apps) |
+-----------------+      +---------------+      +-------------------+
        |                        |                        |
        | (DMA)                  |                        |
        v                        |                        |
+-----------------+      +---------------+      +-------------------+
|  Video Sensor   |----->| Kernel Buffer |----->|  Client (Producer)|
+-----------------+      +---------------+  ^   +-------------------+
                                            |             |
                                          (MMAP)          | (TCP Socket)
                                            |             v
                                            |   +-------------------+
                                            |   | Server (Consumer) |
                                            |   +-------------------+
                                            |             |
                                            |             | (fwrite)
                                            |             v
                                            |   +-------------------+
                                            |   |    Local Disk     |
                                            |   +-------------------+
                                      
```

The graph is divided into three layers: `Hardware`, `Kernel Space` (Operating System), and `User Space` (Applications).

### Hardware Application

`Video Sensor` → `Kernel Buffer`: The webcam sensor captures the image frame.

`DMA`: Instead of passing through the CPU, the data is "shot" directly from the sensor into the RAM (Kernel Buffer).

### Passing to Client

`Kernel Buffer` → `Client (Producer)`: This is where the core V4L2 operation happens.

`MMAP (Memory Mapping)`: Instead of copying data from the Kernel to your program (which would be slow and CPU-intensive), your Client program "maps" (direct access) that specific portion of Kernel memory.
This implements the Zero-Copy concept: the Client reads the data exactly where the hardware placed it, without duplicating it.

### Network Transmission

`Client` → `Server`: Once the Client obtains the pointer to the data (via MMAP), it sends the data over the network.

`TCP Socket`: The data travels through the TCP protocol; the Client is responsible only for acquiring data quickly, while the network handles the transfer to the storage unit.

### Storage

`Server` → `Local Disk`: The Server receives the data packets from the network.

`fwrite`: The Server uses the standard C function fwrite to physically write the bytes onto the hard drive.


## 2. Source Files Description

The project consists of two main C source files.

### 2.1 `client.c` (The Producer)
This file acts as the **Video Acquisition Unit**. It is a native V4L2 driver client that does not rely on external libraries like OpenCV.

* **Device Setup:** Opens `/dev/video0` in non-blocking mode and configures the pixel format (MJPEG or YUYV) and resolution (640x480) via `ioctl`.
* **Buffer Management:** Requests the kernel to allocate 4 video buffers and maps them into the process memory using `mmap()`. This allows the application to read frame data directly from kernel memory without `memcpy` (Zero-Copy).
* **I/O Multiplexing:** Uses `select()` to wait for frame readiness. This ensures the CPU is not blocked in a busy-wait loop.
* **Transmission:** When a frame is ready, the pointer to the memory-mapped data is passed directly to the network socket for transmission.

### 2.2 `server.c` (The Consumer)
This file acts as the **Remote Storage Unit**. It is a concurrent-capable TCP server designed to receive video streams and persist them to disk.

* **Socket Management:** Creates a TCP socket, binds it to port `8080`, and listens for incoming connections.
* **Protocol Implementation:** Implements a strict state machine to parse the incoming byte stream according to the application protocol (Metadata -> Payload).
* **Disk I/O:** Receives data in chunks and writes them immediately to disk using `fwrite`, ensuring that large video files do not exhaust the server's RAM.


## 3. Communication Protocol

Since TCP is a stream-oriented protocol, a custom application-layer protocol is defined to preserve message boundaries. Each video frame is sent as a sequence of 4 fields:

| Field Order | Data Type | Size (Bytes) | Description |
| :--- | :--- | :--- | :--- |
| **1** | `int` | 4 | Length of the filename string (N) |
| **2** | `char[]` | N | Filename (e.g., "frame_0001.raw") |
| **3** | `long` | 8 | File Size in bytes (Payload Size) |
| **4** | `bytes` | Variable | Raw Image Data |

---

## 4. Compilation Instructions

The project depends only on standard Linux system libraries (`glibc`) which provides standard implementations of all basic C functions (those found in `<stdio.h>`, `<stdlib.h>`, `<string.h>`, etc.) and ‘wrappers’ for communicating with the Linux kernel.. No external dependencies are required.

### Prerequisites
* A Linux environment (Ubuntu/Debian recommended).
* GCC Compiler (`build-essential`).
* A V4L2-compatible webcam connected to `/dev/video0`.

### Build Commands
Open a terminal in the project root and run:

```bash
# 1. Compile the Server
gcc server.c -o server

# 2. Compile the Client
gcc client.c -o client
```
Next you need to execute first the server and next the client:

```bash
# 1. Execute the Server
./server

# 2. Execute the Client
./client
```

10 raw images will be generated.

## 4. Troubleshooting

* Bind failed: Address already in use: If the server fails to start, the port 8080 might be occupied. Wait a few seconds or kill the previous process using: `fuser -k 8080/tcp`
* Cannot open `/dev/video0`: Ensure your user has permission to access the webcam. Try running: sudo chmod 666 /dev/video0 or add your user to the video group.