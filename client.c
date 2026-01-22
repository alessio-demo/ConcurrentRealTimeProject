/**
 * @file client.c
 * @brief V4L2 Client acting as a Producer. Captures frames directly from the kernel driver using Memory Mapping and sends them over TCP.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>              
#include <unistd.h>             
#include <errno.h>              
#include <sys/ioctl.h>          
#include <sys/mman.h>           
#include <sys/socket.h>         
#include <arpa/inet.h>          
#include <linux/videodev2.h>    

/* Defines the device path, resolution, and server connection details. */
#define DEVICE "/dev/video0"
#define WIDTH 640
#define HEIGHT 480
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define FRAME_COUNT 10

/* Tracks memory buffers shared with the camera driver. Stores the user-space pointer and length for each buffer to enable data access. */
struct buffer_info {
    void *start;
    size_t length;
};

struct buffer_info *buffers;
unsigned int n_buffers;
int fd_cam = -1; // File descriptor for the camera device
int fd_sock = -1; // File descriptor for the network socket
int frame_number = 0;

/* Wrapper function for the ioctl system call. Retries the call automatically if interrupted by a system signal (EINTR), increasing robustness. */
static int xioctl(int fh, int request, void *arg) {
    int r;
    do {
    /*It is the system call that enables sending control and configuration commands to a device driver, performing operations that cannot be handled by standard read or write calls.
    In my project, I use it to set the video format, manage memory buffers, and start or stop the streaming.*/
        r = ioctl(fh, request, arg);

    } while (-1 == r && errno == EINTR); // Retry if  ioctl interrupted by signal
    return r;
}

/**
 * @brief Handles network transmission of image data.
 * Implements the client-side protocol: sends metadata first, followed by raw image data.
 */
void send_frame_via_network(const void *p, int size) {
    char filename[64];
    
    /* Generates a sequential filename for each frame. Uses .raw extension as the data matches the camera sensor output (MJPEG/YUYV) without a container. */
    sprintf(filename, "frame_%04d.raw", frame_number++);
    
    int name_len = strlen(filename);
    long file_size = size;

    /* Sends filename length, filename string, and image payload size. This header enables the server to prepare for the incoming stream. */
    send(fd_sock, &name_len, sizeof(name_len), 0); // send the length of the filename to the server
    send(fd_sock, filename, name_len, 0); // send the filename string to the server
    send(fd_sock, &file_size, sizeof(file_size), 0); // send the size of the image payload to the server
    
    /* Assumes pointer 'p' points to valid image data. Loops to send all bytes to the server socket, continuing until total_sent matches file size. */
    long total_sent = 0;
    while(total_sent < file_size) {
        int sent = send(fd_sock, p + total_sent, file_size - total_sent, 0); // sends a portion of the image data from the 'p' pointer
        if(sent < 0) { 
            perror("[CLIENT] Network send error"); 
            break; 
        }
        total_sent += sent;
    }
    printf("[CLIENT] Successfully transmitted %s (%d bytes)\n", filename, size);
}

/**
 * @brief Configures the V4L2 device and sets up Memory Mapping.
 */
void init_camera() {
    struct v4l2_format fmt; // Image format structure
    struct v4l2_requestbuffers req; // Buffer request structure

    /* Opens the video device in Read/Write mode. O_NONBLOCK flag ensures calls return immediately if data is not ready, preventing program freeze. */
    fd_cam = open(DEVICE, O_RDWR | O_NONBLOCK, 0);
    if (fd_cam < 0) { 
        perror("Failed to open video device"); 
        exit(1); 
    }

    /* Configures the image format, setting width, height, and pixel format. MJPEG is preferred for compression and smaller network transfer size. */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // Specifies we are configuring video capture settings
    fmt.fmt.pix.width = WIDTH; // Sets image width
    fmt.fmt.pix.height = HEIGHT; // Sets image height
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG; // Sets pixel format to MJPEG
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED; // Sets field type

    /* Applies these settings to the hardware driver via ioctl. */
    if (xioctl(fd_cam, VIDIOC_S_FMT, &fmt) == -1) {
        perror("Error setting Pixel Format (MJPEG might not be supported)");
        exit(1);
    }

    /* Requests allocation of 4 buffers in kernel memory. This enables 'streaming I/O', which is more efficient than read/write by avoiding data copies between kernel and user space. */
    memset(&req, 0, sizeof(req));
    req.count = 4; // Requests 4 buffers from driver
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // Specifies buffer type
    req.memory = V4L2_MEMORY_MMAP; // Specifies memory mapping I/O method

    // Sends request to driver to allocate buffers.
    if (xioctl(fd_cam, VIDIOC_REQBUFS, &req) == -1) {
        perror("Error requesting buffer allocation");
        exit(1);
    }

    /* Allocates array to track these buffers. */
    buffers = calloc(req.count, sizeof(*buffers));
    
    /* Iterates through each driver-allocated buffer to map it into process memory. */
    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        struct v4l2_buffer buf;
        
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = n_buffers;

        /* Queries the driver for details (offset and length) of the buffer at this index. */
        if (xioctl(fd_cam, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("Query buffer error");
            exit(1);
        }

        buffers[n_buffers].length = buf.length;
        
        /* Maps device memory (buf.m.offset) directly to a user-space pointer (buffers[n_buffers].start) using mmap. This implements the 'Zero-Copy' mechanism. */
        buffers[n_buffers].start = mmap(NULL, buf.length, // Maps buffer into user space
                                      PROT_READ | PROT_WRITE, MAP_SHARED, // permissions reading/writing and shared mapping
                                      fd_cam, buf.m.offset); // offset provided by driver
    
        if (MAP_FAILED == buffers[n_buffers].start) {
            perror("Memory Map failed");
            exit(1);
        }
    }
}

/**
 * @brief Initializes the capture process by queuing buffers and enabling the stream.
 */
void start_capturing() {
    enum v4l2_buf_type type;
    
    /* Enqueues all empty buffers into the driver's incoming queue, providing memory locations for storing video frames. */
    for (int i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // Specifies buffer type
        buf.memory = V4L2_MEMORY_MMAP; // Specifies memory mapping I/O method
        buf.index = i;
        
        if (xioctl(fd_cam, VIDIOC_QBUF, &buf) == -1) //put the buffer in the driver queue
            perror("Queue Buffer error");
    }

    /* Sends STREAMON command to hardware to begin capturing images and filling queued buffers. */
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_cam, VIDIOC_STREAMON, &type) == -1) // start streaming
        perror("Stream ON error");
}

/**
 * @brief Retrieves a filled buffer, processes it, and returns it to the driver.
 */
int read_frame() {
    struct v4l2_buffer buf;
    
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // Specifies buffer type
    buf.memory = V4L2_MEMORY_MMAP; // Specifies memory mapping I/O method

    /* Dequeues a buffer from the driver's outgoing queue containing a valid video frame. Returns EAGAIN if no buffer is ready due to non-blocking mode. */
    if (xioctl(fd_cam, VIDIOC_DQBUF, &buf) == -1) {
        if (errno == EAGAIN) return 0; 
        perror("Dequeue Buffer error");
        return -1;
    }

    /* Passes the pointer to raw image data (buffers[buf.index].start) to the network function. Uses buf.bytesused for exact frame size. */
    send_frame_via_network(buffers[buf.index].start, buf.bytesused);

    /* Enqueues the buffer back to the driver for reuse, maintaining the circular buffer cycle. */
    if (xioctl(fd_cam, VIDIOC_QBUF, &buf) == -1) 
        perror("Re-Queue Buffer error");
    
    return 1;
}

/**
 * @brief Main loop synchronizing capture and transmission using select().
 */
void main_loop() {
    int count = FRAME_COUNT;
    
    while (count > 0) {
        fd_set fds;
        struct timeval tv;
        int r;

        /* Uses select system call to wait efficiently. Avoids busy waiting by sleeping until the camera file descriptor is ready or timeout expires. */
        FD_ZERO(&fds); // Clears the set
        FD_SET(fd_cam, &fds); // Adds camera fd to the set

        tv.tv_sec = 2; // Sets timeout to 2 seconds
        tv.tv_usec = 0; // Sets microseconds to 0

        r = select(fd_cam + 1, &fds, NULL, NULL, &tv); // Waits for camera fd to be ready or timeout

        if (-1 == r) { 
            perror("Select system call error"); 
            break; 
        }
        if (0 == r) { 
            fprintf(stderr, "Select timeout: Camera is not producing data.\n"); 
            continue; 
        }

        /* Calls read_frame to process data if select returns a positive number indicating readiness. */
        if (read_frame()) 
            count--; 
    }
}

/**
 * @brief Establishes the network connection.
 */
void init_network() {
    struct sockaddr_in serv_addr;
    
    /* Creates a standard TCP socket. */
    if ((fd_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { 
        perror("Socket creation error"); 
        exit(1); 
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    
    /* Converts IP address string to binary form. */
    if(inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) { 
        perror("Invalid IP Address"); 
        exit(1); 
    }
    
    /* Attempts to establish a connection to the server. */
    if (connect(fd_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        exit(1);
    }
}

int main() {
    /* Establishes connection to the storage server. */
    init_network(); 

    /* Configures the camera driver and maps memory buffers. */
    init_camera();    

    /* Signals the camera to start streaming frames to buffers. */
    start_capturing();
    
    printf("[INFO] Starting capture loop for %d frames...\n", FRAME_COUNT);
    
    /* Enters the loop to consume frames and send them via network. */
    main_loop();      
    
    printf("[INFO] Operations finished. Closing resources.\n");

    /* Closes file descriptors for a clean shutdown. */
    close(fd_sock);
    close(fd_cam);
    
    return 0;
}