/*
 * Copyright (c) 2023-2025
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/***********************************************************************************************************************
 * canperformance.c - SocketCAN performance testing utility
 *
 * Author: Ken Li (ken.li@nxp.com) - NXP Semiconductors
 *
 * This program implements a comprehensive CAN bus testing and benchmarking utility that provides
 * the following key features:
 *
 * - High-performance random CAN frame transmission with configurable intervals (nanosecond precision)
 * - Accurate reception and verification of CAN frames
 * - Real-time performance metrics including frames per second (FPS) calculation
 * - Data integrity verification using MD5 checksums for both CAN IDs and payload data
 * - File transfer capabilities over CAN bus with integrity verification
 * - Progress visualization with dynamic progress bars
 * - Support for both standard (11-bit) and extended (29-bit) CAN frame formats
 * - Support for fixed CAN IDs or random ID generation
 * - Detailed debugging options for protocol analysis
 * - Memory-efficient buffer management for handling large frame counts
 *
 * This tool is designed for CAN bus performance testing, protocol verification,
 * and file transfer in automotive and industrial applications.
 **********************************************************************************************************************/

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <time.h>
 #include <signal.h>
 #include <stdbool.h>
 #include <net/if.h>
 #include <sys/ioctl.h>
 #include <sys/socket.h>
 #include <sys/time.h>
 #include <linux/can.h>
 #include <linux/can/raw.h>
 #include <getopt.h>

 /* MD5 implementation */
 #include <stdint.h>
 #include <errno.h>

 /* MD5 Constants */
 #define A 0x67452301
 #define B 0xefcdab89
 #define C 0x98badcfe
 #define D 0x10325476

 /* MD5 Functions */
 #define F(x, y, z) ((x & y) | (~x & z))
 #define G(x, y, z) ((x & z) | (y & ~z))
 #define H(x, y, z) (x ^ y ^ z)
 #define I(x, y, z) (y ^ (x | ~z))

 /* MD5 Rotation amounts */
 static const uint32_t S[] = {
     7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
     5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
     4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
     6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
 };

 /* MD5 Constants */
 static const uint32_t K[] = {
     0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
     0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
     0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
     0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
     0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
     0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
     0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
     0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
     0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
     0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
     0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
     0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
     0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
     0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
     0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
     0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
 };

 /* MD5 Context structure */
 typedef struct {
     uint64_t size;		/* Size of input in bytes */
     uint32_t buffer[4];	/* Current accumulation of hash */
     uint8_t input[64];	/* Input to be used in the next step */
     uint8_t digest[16];	/* Result of algorithm */
 } MD5Context;

 /* MD5 Function prototypes */
 void md5Init(MD5Context * ctx);
 void md5Update(MD5Context * ctx, uint8_t * input, size_t input_len);
 void md5Finalize(MD5Context * ctx);
 void md5Step(uint32_t * buffer, uint32_t * input);
 void md5String(uint8_t * input, size_t input_len, uint8_t * result);

 /* System memory management function */
 void free_system_caches(void);

 /* Rotate a 32-bit number left */
 static inline uint32_t rotateLeft(uint32_t x, uint32_t n)
 {
     return (x << n) | (x >> (32 - n));
 }

 /* Initialize the MD5 context */
 void md5Init(MD5Context * ctx)
 {
     ctx->size = 0;
     ctx->buffer[0] = A;
     ctx->buffer[1] = B;
     ctx->buffer[2] = C;
     ctx->buffer[3] = D;
 }

 /* Update the MD5 context with new data */
 void md5Update(MD5Context * ctx, uint8_t * input_buffer, size_t input_len)
 {
     uint32_t input[16];
     unsigned int offset = ctx->size % 64;
     ctx->size += (uint64_t) input_len;

     /* Copy each byte in input_buffer into the next space in our context input */
     for (unsigned int i = 0; i < input_len; ++i) {
         ctx->input[offset++] = (uint8_t) * (input_buffer + i);

         /* If we've filled our context input, copy it into our local array input */
         /* then reset the offset to 0 and fill in a new buffer. */
         /* Every time we fill out a chunk, we run it through the algorithm */
         if (offset % 64 == 0) {
             for (unsigned int j = 0; j < 16; ++j) {
                 /* Convert to little-endian */
                 input[j] =
                     (uint32_t) (ctx->input[(j * 4) +
                                3]) << 24 |
                     (uint32_t) (ctx->input[(j * 4) +
                                2]) << 16 |
                     (uint32_t) (ctx->input[(j * 4) +
                                1]) << 8 |
                     (uint32_t) (ctx->input[(j * 4)]);
             }
             md5Step(ctx->buffer, input);
             offset = 0;
         }
     }
 }

 /* Finalize the MD5 calculation */
 void md5Finalize(MD5Context * ctx)
 {
     unsigned int offset = ctx->size % 64;
     unsigned int padding_length =
         offset < 56 ? 56 - offset : (56 + 64) - offset;

     /* Padding - add a 1 bit followed by zeros */
     uint8_t padding[64] = { 0 };
     padding[0] = 0x80;	/* 10000000 */

     /* Add padding */
     md5Update(ctx, padding, padding_length);

     /* Add the length in bits at the end (little-endian) */
     uint64_t size_in_bits = ctx->size * 8;
     uint8_t size_bytes[8];
     for (int i = 0; i < 8; i++) {
         size_bytes[i] = (size_in_bits >> (i * 8)) & 0xFF;
     }
     md5Update(ctx, size_bytes, 8);

     /* Convert the final state to a byte array (little-endian) */
     for (unsigned int i = 0; i < 4; ++i) {
         ctx->digest[(i * 4) + 0] =
             (uint8_t) ((ctx->buffer[i] & 0x000000FF));
         ctx->digest[(i * 4) + 1] =
             (uint8_t) ((ctx->buffer[i] & 0x0000FF00) >> 8);
         ctx->digest[(i * 4) + 2] =
             (uint8_t) ((ctx->buffer[i] & 0x00FF0000) >> 16);
         ctx->digest[(i * 4) + 3] =
             (uint8_t) ((ctx->buffer[i] & 0xFF000000) >> 24);
     }
 }

 /* MD5 main algorithm */
 void md5Step(uint32_t * buffer, uint32_t * input)
 {
     uint32_t AA = buffer[0];
     uint32_t BB = buffer[1];
     uint32_t CC = buffer[2];
     uint32_t DD = buffer[3];
     uint32_t E;
     unsigned int j;

     for (unsigned int i = 0; i < 64; ++i) {
         switch (i / 16) {
         case 0:
             E = F(BB, CC, DD);
             j = i;
             break;
         case 1:
             E = G(BB, CC, DD);
             j = ((i * 5) + 1) % 16;
             break;
         case 2:
             E = H(BB, CC, DD);
             j = ((i * 3) + 5) % 16;
             break;
         default:
             E = I(BB, CC, DD);
             j = (i * 7) % 16;
             break;
         }

         uint32_t temp = DD;
         DD = CC;
         CC = BB;
         BB = BB + rotateLeft(AA + E + K[i] + input[j], S[i]);
         AA = temp;
     }

     buffer[0] += AA;
     buffer[1] += BB;
     buffer[2] += CC;
     buffer[3] += DD;
 }

 /* Calculate MD5 for a memory buffer */
 void md5String(uint8_t * input, size_t input_len, uint8_t * result)
 {
     MD5Context ctx;
     md5Init(&ctx);
     md5Update(&ctx, input, input_len);
     md5Finalize(&ctx);
     memcpy(result, ctx.digest, 16);
 }

 /* Print MD5 sum in hexadecimal format */
 void printMD5(uint8_t * md5_sum)
 {
     for (int i = 0; i < 16; i++) {
         printf("%02x", md5_sum[i]);
     }
     printf("\n");
 }

 /* CAN frame buffer structure */
 struct {
     struct can_frame *frames;	/* Dynamically allocated array of frames */
     int count;		/* Current number of frames */
     int capacity;		/* Current capacity of the frames array */
 } frame_buffer;

 /* Global variables */
 static volatile int keep_running = 1;
 static int socket_fd = -1;
 static int debug_mode = 0;
 static int default_buffer_size = 1000000;	/* Default buffer size (1 million frames) */
 static int file_mode = 0;	/* File transfer mode flag */
 static char *file_path = NULL;	/* File path for file transfer mode */
 static int extended_frame_mode = 0;	/* Extended frame mode flag (29-bit CAN ID) */

 /* File transfer functions */
 int transmit_file(const char *ifname, int interval_ns, const char *file_path,
           uint32_t fixed_can_id);
 int receive_file(const char *ifname, const char *file_path);

 /* Calculate MD5 of a file */
 void calculate_file_md5(const char *file_path, uint8_t * result)
 {
     FILE *file = fopen(file_path, "rb");
     if (!file) {
         perror("Failed to open file for MD5 calculation");
         memset(result, 0, 16);	/* Set result to zeros on failure */
         return;
     }

     MD5Context ctx;
     md5Init(&ctx);

     /* Read file in chunks and update MD5 */
     uint8_t buffer[4096];
     size_t bytes_read;

     while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
         md5Update(&ctx, buffer, bytes_read);
     }

     md5Finalize(&ctx);
     memcpy(result, ctx.digest, 16);

     fclose(file);
 }

 /* Initialize frame buffer with given capacity */
 void init_frame_buffer(int capacity)
 {
     /* Default capacity if not specified */
     if (capacity <= 0) {
         capacity = default_buffer_size;	/* Use the default buffer size */
     }

     /* Free existing buffer if any */
     if (frame_buffer.frames != NULL) {
         free(frame_buffer.frames);
         frame_buffer.frames = NULL;
     }

     /* Allocate new buffer - always use dynamic allocation */
     frame_buffer.frames =
         (struct can_frame *)malloc(capacity * sizeof(struct can_frame));
     if (frame_buffer.frames == NULL) {
         perror("Failed to allocate frame buffer");
         printf("Requested buffer size: %d frames (%zu bytes)\n",
                capacity, capacity * sizeof(struct can_frame));
         exit(EXIT_FAILURE);
     }

     frame_buffer.count = 0;
     frame_buffer.capacity = capacity;
 }

 /* Add a frame to the buffer, resizing if necessary */
 void add_frame_to_buffer(struct can_frame *frame)
 {
     /* Check if we need to resize */
     if (frame_buffer.count >= frame_buffer.capacity) {
         /* Try to increase capacity by 50% */
         int new_capacity =
             frame_buffer.capacity + (frame_buffer.capacity / 2);
         if (new_capacity <= frame_buffer.capacity) {
             /* Handle potential overflow */
             new_capacity = frame_buffer.capacity + 1000;
         }

         struct can_frame *new_frames =
             (struct can_frame *)realloc(frame_buffer.frames,
                         new_capacity *
                         sizeof(struct can_frame));

         if (new_frames == NULL) {
             perror("Failed to resize frame buffer");
             printf
                 ("Buffer capacity reached: %d frames. Cannot allocate more memory.\n",
                  frame_buffer.capacity);
             printf
                 ("Stopping frame reception. Some frames may be lost.\n");
             /* Don't exit, just return and let the program continue with the existing buffer */
             keep_running = 0;
             return;
         }

         frame_buffer.frames = new_frames;
         frame_buffer.capacity = new_capacity;
     }

     /* Add the frame */
     frame_buffer.frames[frame_buffer.count++] = *frame;
 }

 /* Free the frame buffer */
 void free_frame_buffer()
 {
     if (frame_buffer.frames != NULL) {
         free(frame_buffer.frames);
         frame_buffer.frames = NULL;
     }
     frame_buffer.count = 0;
     frame_buffer.capacity = 0;
 }

 /* Signal handler for Ctrl+C */
 void signal_handler(int sig)
 {
     printf("\nReceived signal %d, stopping...\n", sig);
     keep_running = 0;

     /* Note: We don't free memory here because signal handlers should be kept minimal */
     /* The main loop will detect keep_running=0 and exit properly, freeing memory */
 }

 /* Initialize CAN socket */
 int init_can_socket(const char *ifname)
 {
     int s;
     struct sockaddr_can addr;
     struct ifreq ifr;

     /* Create socket */
     if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
         perror("Socket");
         return -1;
     }

     /* Get interface index */
     strcpy(ifr.ifr_name, ifname);
     if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
         perror("ioctl");
         close(s);
         return -1;
     }

     /* Set CAN filter to receive all frames */
     struct can_filter rfilter[1];
     rfilter[0].can_id = 0;
     rfilter[0].can_mask = 0;
     if (setsockopt
         (s, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter)) < 0) {
         perror("setsockopt filter");
         close(s);
         return -1;
     }

     /* Bind socket to the CAN interface */
     memset(&addr, 0, sizeof(addr));
     addr.can_family = AF_CAN;
     addr.can_ifindex = ifr.ifr_ifindex;

     if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
         perror("Bind");
         close(s);
         return -1;
     }

     /* Print frame mode information */
     if (extended_frame_mode) {
         printf("Using extended frame format (29-bit CAN ID)\n");
     } else {
         printf("Using standard frame format (11-bit CAN ID)\n");
     }

     return s;
 }

 /* FUNCTION ************************************************************************************************************
  *
  * Function Name : generate_random_frame
  * Description   : Generates a random CAN frame with optional fixed CAN ID.
  *                 Supports both standard (11-bit) and extended (29-bit) CAN IDs.
  *                 Avoids special frame IDs (0x7FB-0x7FF) when using random IDs.
  *
  * END ****************************************************************************************************************/
 void generate_random_frame(struct can_frame *frame, uint32_t fixed_can_id)
 {
     if (extended_frame_mode) {
         /* Set CAN ID (extended 29-bit) */
         if (fixed_can_id != 0) {
             /* Use fixed CAN ID if provided */
             frame->can_id =
                 (fixed_can_id & 0x1FFFFFFF) | CAN_EFF_FLAG;
         } else {
             /* Random CAN ID (extended 29-bit) */
             /* Reserve the range 0x1FFFFF7B-0x1FFFFFFF for special frames in extended mode */
             uint32_t random_id;
             do {
                 random_id =
                     ((uint32_t) rand() << 16) | ((uint32_t)
                                  rand() &
                                  0xFFFF);
                 random_id &= 0x1FFFFFFF;	/* Mask to 29 bits */
             } while (random_id >= 0x1FFFFF7B
                  && random_id <= 0x1FFFFFFF);

             frame->can_id = random_id | CAN_EFF_FLAG;
         }
     } else {
         /* Set CAN ID (standard 11-bit) */
         if (fixed_can_id != 0) {
             /* Use fixed CAN ID if provided */
             frame->can_id = fixed_can_id & 0x7FF;
         } else {
             /* Random CAN ID (standard 11-bit), avoiding special frame IDs (0x7FB-0x7FF) */
             do {
                 frame->can_id = rand() % 0x7FB;	/* Only use IDs up to 0x7FA to avoid special frames */
             } while (frame->can_id >= 0x7FB
                  && frame->can_id <= 0x7FF);
         }
     }

     /* Random data length (0-8 bytes) */
     frame->can_dlc = rand() % 9;

     /* Random data */
     for (int i = 0; i < frame->can_dlc; i++) {
         frame->data[i] = rand() % 256;
     }
 }

 /* FUNCTION ************************************************************************************************************
  *
  * Function Name : calculate_can_id_md5
  * Description   : Calculates MD5 checksum for all CAN IDs in the frame buffer.
  *
  * END ****************************************************************************************************************/
 void calculate_can_id_md5(uint8_t * result)
 {
     /* Initialize result to zeros in case of failure */
     memset(result, 0, 16);

     /* Check if we have frames to process */
     if (frame_buffer.count == 0 || frame_buffer.frames == NULL) {
         printf("Warning: No frames to calculate MD5 for CAN IDs\n");
         return;
     }

     uint32_t *can_ids = malloc(frame_buffer.count * sizeof(uint32_t));
     if (!can_ids) {
         perror("Memory allocation failed in calculate_can_id_md5");
         printf
             ("Warning: Unable to calculate MD5 for CAN IDs due to memory allocation failure\n");
         return;		/* Return with zeros in result instead of exiting */
     }

     /* Extract CAN IDs */
     for (int i = 0; i < frame_buffer.count; i++) {
         can_ids[i] = frame_buffer.frames[i].can_id;
     }

     /* Calculate MD5 */
     md5String((uint8_t *) can_ids, frame_buffer.count * sizeof(uint32_t),
           result);

     free(can_ids);
 }

 /* FUNCTION ************************************************************************************************************
  *
  * Function Name : calculate_can_data_md5
  * Description   : Calculates MD5 checksum for all CAN data in the frame buffer.
  *
  * END ****************************************************************************************************************/
 void calculate_can_data_md5(uint8_t * result)
 {
     /* Initialize result to zeros in case of failure */
     memset(result, 0, 16);

     /* Check if we have frames to process */
     if (frame_buffer.count == 0 || frame_buffer.frames == NULL) {
         printf("Warning: No frames to calculate MD5 for CAN data\n");
         return;
     }

     /* Calculate total data size */
     size_t total_size = 0;
     for (int i = 0; i < frame_buffer.count; i++) {
         total_size += frame_buffer.frames[i].can_dlc;
     }

     /* If no data, return early */
     if (total_size == 0) {
         printf("Warning: No data to calculate MD5 for\n");
         return;
     }

     /* Allocate buffer for all data */
     uint8_t *data_buffer = malloc(total_size);
     if (!data_buffer) {
         perror("Memory allocation failed in calculate_can_data_md5");
         printf
             ("Warning: Unable to calculate MD5 for CAN data due to memory allocation failure\n");
         return;		/* Return with zeros in result instead of exiting */
     }

     /* Copy all data to buffer */
     size_t offset = 0;
     for (int i = 0; i < frame_buffer.count; i++) {
         memcpy(data_buffer + offset, frame_buffer.frames[i].data,
                frame_buffer.frames[i].can_dlc);
         offset += frame_buffer.frames[i].can_dlc;
     }

     /* Calculate MD5 */
     md5String(data_buffer, total_size, result);

     free(data_buffer);
 }

 /* FUNCTION ************************************************************************************************************
  *
  * Function Name : display_progress_bar
  * Description   : Displays a progress bar with real-time FPS information.
  *
  * END ****************************************************************************************************************/
 void display_progress_bar(int current, int total, int width,
               double elapsed_seconds)
 {
     /* Calculate progress percentage (cap at 100%) */
     float progress = (float)current / total;
     if (progress > 1.0)
         progress = 1.0;	/* Cap at 100% */

     int filled_width = (int)(progress * width);
     if (filled_width > width)
         filled_width = width;	/* Ensure we don't exceed the width */

     /* Calculate percentage (cap at 100%) */
     int percent = (int)(progress * 100);
     if (percent > 100)
         percent = 100;	/* Cap at 100% */

     /* Calculate current FPS */
     double current_fps = 0;
     if (elapsed_seconds > 0) {
         current_fps = current / elapsed_seconds;
     }

     /* Print the progress bar */
     printf("\r[%3d%%] [", percent);

     /* Print the filled part */
     for (int i = 0; i < filled_width; i++) {
         printf("█");
     }

     /* Print the unfilled part */
     for (int i = filled_width; i < width; i++) {
         printf(" ");
     }

     /* Print the current/total count and current FPS */
     /* If current > total, show the actual total to indicate we've received more than expected */
     if (current > total) {
         printf("] %d/%d frames  FPS: %.2f", current, total,
                current_fps);
     } else {
         printf("] %d/%d frames  FPS: %.2f", current, total,
                current_fps);
     }

     /* Flush stdout to ensure the progress bar is displayed immediately */
     fflush(stdout);
 }

 /* FUNCTION ************************************************************************************************************
  *
  * Function Name : transmit_frames
  * Description   : Transmits random CAN frames with configurable interval.
  *                 Calculates MD5 checksums of sent data for verification.
  *
  * END ****************************************************************************************************************/
 void transmit_frames(const char *ifname, int interval_ns, int max_frames,
              uint32_t fixed_can_id)
 {
     struct timespec ts;
     uint8_t md5_can_ids[16];
     uint8_t md5_can_data[16];
     struct timespec start_time, end_time;
     double elapsed_seconds;
     int progress_bar_width = 50;	/* Width of the progress bar in characters */
     int update_interval = 100;	/* Update progress every 100 frames or 1% of max_frames, whichever is smaller */

     /* Set up interval */
     ts.tv_sec = interval_ns / 1000000000;
     ts.tv_nsec = interval_ns % 1000000000;

     /* Initialize frame buffer with capacity based on max_frames */
     init_frame_buffer(max_frames > 0 ? max_frames : default_buffer_size);

     printf("TX Start send %d\n", max_frames);

     /* Record start time */
     clock_gettime(CLOCK_MONOTONIC, &start_time);

     /* Calculate update interval based on max_frames if specified */
     if (max_frames > 0) {
         update_interval = max_frames / 100;	/* Update approximately 100 times during transmission */
         if (update_interval < 1)
             update_interval = 1;	/* Ensure at least 1 */
     }

     /* Always send a special frame with the total frame count before starting transmission */
     struct can_frame info_frame;
     if (extended_frame_mode) {
         info_frame.can_id = 0x1FFFFFFA | CAN_EFF_FLAG;	/* Use extended ID 0x1FFFFFFA for the info frame */
     } else {
         info_frame.can_id = 0x7FA;	/* Use ID 0x7FA for the info frame (just below our special frames range) */
     }
     info_frame.can_dlc = 8;

     /* Determine the number of frames to send */
     int frames_to_send = max_frames > 0 ? max_frames : default_buffer_size;

     /* Store frame count in the first 4 bytes (little-endian) */
     info_frame.data[0] = (frames_to_send >> 0) & 0xFF;
     info_frame.data[1] = (frames_to_send >> 8) & 0xFF;
     info_frame.data[2] = (frames_to_send >> 16) & 0xFF;
     info_frame.data[3] = (frames_to_send >> 24) & 0xFF;
     /* Store interval in the next 4 bytes (little-endian) */
     info_frame.data[4] = (interval_ns >> 0) & 0xFF;
     info_frame.data[5] = (interval_ns >> 8) & 0xFF;
     info_frame.data[6] = (interval_ns >> 16) & 0xFF;
     info_frame.data[7] = (interval_ns >> 24) & 0xFF;

     if (debug_mode) {
         /* Print info frame data bytes for debugging */
         printf
             ("Sending info frame with bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n",
              info_frame.data[0], info_frame.data[1], info_frame.data[2],
              info_frame.data[3], info_frame.data[4], info_frame.data[5],
              info_frame.data[6], info_frame.data[7]);
     }

     /* Send info frame */
     if (write(socket_fd, &info_frame, sizeof(struct can_frame)) !=
         sizeof(struct can_frame)) {
         perror("Write info frame");
     }

     /* Longer delay to ensure the receiver has time to start and process the info frame */
     struct timespec long_delay;
     long_delay.tv_sec = 1;	/* 1 second delay */
     long_delay.tv_nsec = 0;
     printf("TX Waiting for RX to process info frame...\n");
     nanosleep(&long_delay, NULL);

     /* If max_frames is not specified, use a default value for progress bar */
     int display_max_frames =
         max_frames > 0 ? max_frames : default_buffer_size;

     /* Transmit frames */
     while (keep_running
            && (max_frames == 0 || frame_buffer.count < max_frames)) {
         struct can_frame frame;

         /* Generate frame (random or with fixed CAN ID) */
         generate_random_frame(&frame, fixed_can_id);

         /* Send frame */
         if (write(socket_fd, &frame, sizeof(struct can_frame)) !=
             sizeof(struct can_frame)) {
             perror("Write");
             break;
         }

         /* Store frame in buffer */
         add_frame_to_buffer(&frame);

         /* Print frame info if debug mode is enabled */
         if (debug_mode) {
             if (extended_frame_mode) {
                 /* Format for extended frames: can0  12345678   [8]  11 22 33 44 55 66 77 88 */
                 printf("%s  %08X   [%d]  ", ifname,
                        frame.can_id & CAN_EFF_MASK,
                        frame.can_dlc);
             } else {
                 /* Format for standard frames: can0  123   [8]  11 22 33 44 55 66 77 88 */
                 printf("%s  %03X   [%d]  ", ifname,
                        frame.can_id & CAN_SFF_MASK,
                        frame.can_dlc);
             }
             for (int i = 0; i < frame.can_dlc; i++) {
                 printf("%02X", frame.data[i]);
                 if (i < frame.can_dlc - 1) {
                     printf(" ");
                 }
             }
             printf("\n");
         }

         /* Update progress bar (always show progress) */
         if (frame_buffer.count % update_interval == 0 ||
             (max_frames > 0 && frame_buffer.count == max_frames)) {
             /* Calculate current elapsed time for real-time FPS */
             struct timespec current_time;
             clock_gettime(CLOCK_MONOTONIC, &current_time);
             double current_elapsed =
                 (current_time.tv_sec - start_time.tv_sec) +
                 (current_time.tv_nsec -
                  start_time.tv_nsec) / 1000000000.0;

             display_progress_bar(frame_buffer.count,
                          display_max_frames,
                          progress_bar_width,
                          current_elapsed);
         }

         /* Sleep for the specified interval */
         if (interval_ns > 0) {
             nanosleep(&ts, NULL);
         }
     }

     /* Record end time */
     clock_gettime(CLOCK_MONOTONIC, &end_time);

     /* Calculate elapsed time in seconds */
     elapsed_seconds = (end_time.tv_sec - start_time.tv_sec) +
         (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

     /* Calculate FPS */
     double fps = 0;
     if (elapsed_seconds > 0) {
         fps = frame_buffer.count / elapsed_seconds;
     }

     /* Ensure we show final progress with the final FPS */
     int final_count = frame_buffer.count;
     display_progress_bar(final_count, display_max_frames,
                  progress_bar_width, elapsed_seconds);
     printf("\n");		/* Add a newline after the progress bar */

     printf("Target: %d frames, Actually sent: %d frames\n\n",
            max_frames > 0 ? max_frames : frame_buffer.count,
            frame_buffer.count);

     /* Calculate and print MD5 sums */
     calculate_can_id_md5(md5_can_ids);
     calculate_can_data_md5(md5_can_data);

     printf("LOCAL   CAN ID   MD5: ");
     printMD5(md5_can_ids);

     printf("LOCAL   CAN Data MD5: ");
     printMD5(md5_can_data);

     printf("MD5 MATCH or Not See RX log\n");

     /* Send MD5 and frame count as special frames */
     struct can_frame special_frame;

     /* Send CAN ID MD5 (first 8 bytes) */
     if (extended_frame_mode) {
         special_frame.can_id = 0x1FFFFFFF | CAN_EFF_FLAG;	/* Use highest extended ID */
     } else {
         special_frame.can_id = 0x7FF;	/* Use highest standard ID */
     }
     special_frame.can_dlc = 8;
     memcpy(special_frame.data, md5_can_ids, 8);

     if (write(socket_fd, &special_frame, sizeof(struct can_frame)) !=
         sizeof(struct can_frame)) {
         perror("Write MD5 CAN ID frame (first 8 bytes)");
     }

     /* Send CAN ID MD5 (second 8 bytes) */
     if (extended_frame_mode) {
         special_frame.can_id = 0x1FFFFFFE | CAN_EFF_FLAG;	/* Use second highest extended ID */
     } else {
         special_frame.can_id = 0x7FE;	/* Use second highest standard ID */
     }
     special_frame.can_dlc = 8;
     memcpy(special_frame.data, md5_can_ids + 8, 8);

     if (write(socket_fd, &special_frame, sizeof(struct can_frame)) !=
         sizeof(struct can_frame)) {
         perror("Write MD5 CAN ID frame (second 8 bytes)");
     }

     /* Send CAN Data MD5 (first 8 bytes) */
     if (extended_frame_mode) {
         special_frame.can_id = 0x1FFFFFFD | CAN_EFF_FLAG;	/* Use third highest extended ID */
     } else {
         special_frame.can_id = 0x7FD;	/* Use third highest standard ID */
     }
     special_frame.can_dlc = 8;
     memcpy(special_frame.data, md5_can_data, 8);

     if (write(socket_fd, &special_frame, sizeof(struct can_frame)) !=
         sizeof(struct can_frame)) {
         perror("Write MD5 CAN Data frame (first 8 bytes)");
     }

     /* Send CAN Data MD5 (second 8 bytes) */
     if (extended_frame_mode) {
         special_frame.can_id = 0x1FFFFFFC | CAN_EFF_FLAG;	/* Use fourth highest extended ID */
     } else {
         special_frame.can_id = 0x7FC;	/* Use fourth highest standard ID */
     }
     special_frame.can_dlc = 8;
     memcpy(special_frame.data, md5_can_data + 8, 8);

     if (write(socket_fd, &special_frame, sizeof(struct can_frame)) !=
         sizeof(struct can_frame)) {
         perror("Write MD5 CAN Data frame (second 8 bytes)");
     }

     /* Send frame count and FPS */
     if (extended_frame_mode) {
         special_frame.can_id = 0x1FFFFFFB | CAN_EFF_FLAG;	/* Use fifth highest extended ID */
     } else {
         special_frame.can_id = 0x7FB;	/* Use fifth highest standard ID */
     }
     special_frame.can_dlc = 8;
     /* Store frame count in the first 4 bytes (little-endian) */
     special_frame.data[0] = (frame_buffer.count >> 0) & 0xFF;
     special_frame.data[1] = (frame_buffer.count >> 8) & 0xFF;
     special_frame.data[2] = (frame_buffer.count >> 16) & 0xFF;
     special_frame.data[3] = (frame_buffer.count >> 24) & 0xFF;
     /* Store FPS (as integer) in the next 4 bytes (little-endian) */
     int fps_int = (int)fps;
     special_frame.data[4] = (fps_int >> 0) & 0xFF;
     special_frame.data[5] = (fps_int >> 8) & 0xFF;
     special_frame.data[6] = (fps_int >> 16) & 0xFF;
     special_frame.data[7] = (fps_int >> 24) & 0xFF;

     if (debug_mode) {
         printf
             ("Sending frame count frame with bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n",
              special_frame.data[0], special_frame.data[1],
              special_frame.data[2], special_frame.data[3],
              special_frame.data[4], special_frame.data[5],
              special_frame.data[6], special_frame.data[7]);
     }

     if (write(socket_fd, &special_frame, sizeof(struct can_frame)) !=
         sizeof(struct can_frame)) {
         perror("Write frame count frame");
     }
 }

 /* FUNCTION ************************************************************************************************************
  *
  * Function Name : receive_first_frame
  * Description   : Receives the first frame to get expected frame count information.
  *                 Waits for the special info frame with ID 0x7FA.
  *
  * END ****************************************************************************************************************/
 int receive_first_frame(const char *ifname, int *expected_count)
 {
     struct can_frame frame;
     fd_set readfds;
     int ret;

     printf("RX Waiting for first frame with length information...\n");

     /* Wait for the first frame with length information - no timeout */
     while (keep_running) {
         FD_ZERO(&readfds);
         FD_SET(socket_fd, &readfds);

         /* No timeout - wait indefinitely for the first frame */
         ret = select(socket_fd + 1, &readfds, NULL, NULL, NULL);

         if (ret < 0) {
             if (errno == EINTR) {
                 if (!keep_running)
                     return -1;
                 continue;
             }
             perror("select");
             return -1;
         }

         if (!FD_ISSET(socket_fd, &readfds)) {
             continue;
         }

         /* Read a frame */
         ssize_t nbytes =
             read(socket_fd, &frame, sizeof(struct can_frame));
         if (nbytes < 0) {
             perror("Read");
             return -1;
         }

         /* Check if this is the info frame */
         if ((extended_frame_mode
              && (frame.can_id & CAN_EFF_MASK) == 0x1FFFFFFA)
             || (!extended_frame_mode && frame.can_id == 0x7FA)) {
             /* Extract frame count - little-endian byte order */
             int frame_count = ((uint32_t) frame.data[0]) |
                 ((uint32_t) frame.data[1] << 8) |
                 ((uint32_t) frame.data[2] << 16) |
                 ((uint32_t) frame.data[3] << 24);

             /* Sanity check */
             if (frame_count <= 0 || frame_count > 10000000) {
                 printf("Invalid frame count received: %d\n",
                        frame_count);
                 return -1;
             }

             *expected_count = frame_count;
             printf("RX Received info frame: expecting %d frames\n",
                    frame_count);
             return 0;	/* Success */
         }
     }

     return -1;		/* Failed to receive info frame */
 }

 /* FUNCTION ************************************************************************************************************
  *
  * Function Name : receive_frames
  * Description   : Receives CAN frames and verifies MD5 checksums against transmitted data.
  *                 Handles special frames with MD5 checksums and frame count information.
  *
  * END ****************************************************************************************************************/
 void receive_frames(const char *ifname, int max_frames)
 {
     struct can_frame frame;
     struct timeval timeout;
     fd_set readfds;
     int nbytes;
     uint8_t md5_can_ids[16];
     uint8_t md5_can_data[16];
     struct timespec start_time, end_time;
     double elapsed_seconds;
     int progress_bar_width = 50;	/* Width of the progress bar in characters */
     int update_interval = 100;	/* Update progress every 100 frames */
     int expected_frame_count = 0;	/* Expected total frames from transmitter */

     /* Special frames from transmitter */
     int tx_frame_count = 0;
     int tx_fps = 0;
     uint8_t tx_md5_id[16];
     uint8_t tx_md5_data[16];
     int special_frames_received = 0;

     /* First, receive the first frame to get the expected frame count */
     if (receive_first_frame(ifname, &expected_frame_count) < 0) {
         printf
             ("Failed to receive first frame with length information\n");
         return;
     }

     /* Now we know how many frames to expect, initialize the buffer with the exact size */
     /* Add a small margin for special frames */
     int buffer_size = expected_frame_count + 10;
     init_frame_buffer(buffer_size);

     /* Set display max frames */
     int display_max_frames = expected_frame_count;

     /* Update the update_interval based on expected frame count */
     /* Overwrite the default value set earlier */
     update_interval = expected_frame_count / 100;
     if (update_interval < 1)
         update_interval = 1;

     printf("RX Start receive %d frames\n", expected_frame_count);

     /* Record start time */
     clock_gettime(CLOCK_MONOTONIC, &start_time);

     /* Set timeout for select */
     timeout.tv_sec = 3;	/* 3 seconds timeout */
     timeout.tv_usec = 0;

     /* We already know the expected frame count from the first frame */

     /* Receive frames */
     while (keep_running
            && (max_frames == 0 || frame_buffer.count < max_frames + 5)) {
         FD_ZERO(&readfds);
         FD_SET(socket_fd, &readfds);

         /* Wait for data or timeout */
         int ret = select(socket_fd + 1, &readfds, NULL, NULL, &timeout);

         if (ret < 0) {
             perror("Select");
             break;
         } else if (ret == 0) {
             /* Timeout occurred */
             if (frame_buffer.count > 0) {
                 printf
                     ("Timeout: No frames received for 3 seconds\n");
                 break;
             }
             /* Reset timeout and continue waiting */
             timeout.tv_sec = 3;
             timeout.tv_usec = 0;
             continue;
         }

         /* Read frame */
         nbytes = read(socket_fd, &frame, sizeof(struct can_frame));
         if (nbytes < 0) {
             perror("Read");
             break;
         }

         /* Check if this is a special frame from transmitter */
         bool is_special_frame = false;

         if (extended_frame_mode) {
             /* Check for extended frame special IDs */
             uint32_t ext_id = frame.can_id & CAN_EFF_MASK;
             is_special_frame = (ext_id >= 0x1FFFFFFB
                         && ext_id <= 0x1FFFFFFF);
         } else {
             /* Check for standard frame special IDs */
             is_special_frame = (frame.can_id >= 0x7FB
                         && frame.can_id <= 0x7FF);
         }

         if (is_special_frame) {
             /* Skip info frames (0x7FA) as we've already processed the first one */
             uint32_t frame_id =
                 extended_frame_mode ? (frame.can_id & CAN_EFF_MASK)
                 : frame.can_id;

             /* Check for first MD5 frame (highest ID) */
             if ((extended_frame_mode && frame_id == 0x1FFFFFFF) || (!extended_frame_mode && frame_id == 0x7FF)) {	/* CAN ID MD5 (first 8 bytes) */
                 memcpy(tx_md5_id, frame.data, 8);
                 special_frames_received++;
                 if (debug_mode) {
                     printf
                         ("Received TX CAN ID MD5 frame (first 8 bytes)\n");
                 }
             }
             /* Check for second MD5 frame */
             else if ((extended_frame_mode && frame_id == 0x1FFFFFFE) || (!extended_frame_mode && frame_id == 0x7FE)) {	/* CAN ID MD5 (second 8 bytes) */
                 memcpy(tx_md5_id + 8, frame.data, 8);
                 special_frames_received++;
                 if (debug_mode) {
                     printf
                         ("Received TX CAN ID MD5 frame (second 8 bytes)\n");
                 }
             }
             /* Check for third MD5 frame */
             else if ((extended_frame_mode && frame_id == 0x1FFFFFFD) || (!extended_frame_mode && frame_id == 0x7FD)) {	/* CAN Data MD5 (first 8 bytes) */
                 memcpy(tx_md5_data, frame.data, 8);
                 special_frames_received++;
                 if (debug_mode) {
                     printf
                         ("Received TX CAN Data MD5 frame (first 8 bytes)\n");
                 }
             }
             /* Check for fourth MD5 frame */
             else if ((extended_frame_mode && frame_id == 0x1FFFFFFC) || (!extended_frame_mode && frame_id == 0x7FC)) {	/* CAN Data MD5 (second 8 bytes) */
                 memcpy(tx_md5_data + 8, frame.data, 8);
                 special_frames_received++;
                 if (debug_mode) {
                     printf
                         ("Received TX CAN Data MD5 frame (second 8 bytes)\n");
                 }
             }
             /* Check for frame count frame */
             else if ((extended_frame_mode && frame_id == 0x1FFFFFFB) || (!extended_frame_mode && frame_id == 0x7FB)) {	/* Frame count and FPS */
                 /* Extract frame count from first 4 bytes - use the same byte order as info frame */
                 /* Little-endian byte order (least significant byte first) */
                 tx_frame_count = ((uint32_t) frame.data[0]) |
                     ((uint32_t) frame.data[1] << 8) |
                     ((uint32_t) frame.data[2] << 16) |
                     ((uint32_t) frame.data[3] << 24);
                 /* Extract FPS from next 4 bytes - use the same byte order as frame count */
                 /* Little-endian byte order (least significant byte first) */
                 tx_fps = ((uint32_t) frame.data[4]) |
                     ((uint32_t) frame.data[5] << 8) |
                     ((uint32_t) frame.data[6] << 16) |
                     ((uint32_t) frame.data[7] << 24);
                 special_frames_received++;
                 if (debug_mode) {
                     printf
                         ("Received TX frame count frame: count=%d, fps=%d\n",
                          tx_frame_count, tx_fps);
                 }
             }

             /* If we've received all special frames, we can stop */
             if (special_frames_received >= 5
                 && frame_buffer.count >= expected_frame_count) {
                 printf("\n");	/* Add a newline to separate from progress bar */
                 break;
             }

             /* Don't add special frames to our regular frame buffer */
             continue;
         }

         /* Store regular frame in buffer */
         add_frame_to_buffer(&frame);

         /* Print frame info if debug mode is enabled */
         if (debug_mode) {
             if (extended_frame_mode) {
                 /* Format for extended frames: can0  12345678   [8]  11 22 33 44 55 66 77 88 */
                 printf("%s  %08X   [%d]  ", ifname,
                        frame.can_id & CAN_EFF_MASK,
                        frame.can_dlc);
             } else {
                 /* Format for standard frames: can0  123   [8]  11 22 33 44 55 66 77 88 */
                 printf("%s  %03X   [%d]  ", ifname,
                        frame.can_id & CAN_SFF_MASK,
                        frame.can_dlc);
             }
             for (int i = 0; i < frame.can_dlc; i++) {
                 printf("%02X", frame.data[i]);
                 if (i < frame.can_dlc - 1) {
                     printf(" ");
                 }
             }
             printf("\n");
         }

         /* Update progress bar (always show progress) */
         if (frame_buffer.count % update_interval == 0) {
             /* Calculate current elapsed time for real-time FPS */
             struct timespec current_time;
             clock_gettime(CLOCK_MONOTONIC, &current_time);
             double current_elapsed =
                 (current_time.tv_sec - start_time.tv_sec) +
                 (current_time.tv_nsec -
                  start_time.tv_nsec) / 1000000000.0;

             display_progress_bar(frame_buffer.count,
                          display_max_frames,
                          progress_bar_width,
                          current_elapsed);
         }

         /* Reset timeout */
         timeout.tv_sec = 3;
         timeout.tv_usec = 0;

         /* If we've received the requested number of frames, wait for special frames */
         /* Only print this message once when we first reach the threshold */
         if ((max_frames > 0 && frame_buffer.count == max_frames) ||
             (expected_frame_count > 0
              && frame_buffer.count == expected_frame_count)) {
             printf
                 ("\nReceived %d frames, waiting for special frames...\n",
                  frame_buffer.count);
             /* Continue receiving to get the special frames */
         }
     }

     /* Record end time */
     clock_gettime(CLOCK_MONOTONIC, &end_time);

     /* Calculate elapsed time in seconds */
     elapsed_seconds = (end_time.tv_sec - start_time.tv_sec) +
         (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

     /* Adjust frame count to exclude special frames */
     int actual_frame_count = frame_buffer.count;

     /* Calculate FPS */
     /*double fps = 0;
     if (actual_frame_count > 0 && elapsed_seconds > 0) {
         fps = actual_frame_count / elapsed_seconds;
     }*/

     /* Only show final progress bar if we didn't already receive all special frames */
     if (special_frames_received < 5) {
         int final_count = frame_buffer.count;
         display_progress_bar(final_count, display_max_frames,
                      progress_bar_width, elapsed_seconds);
         printf("\n");	/* Add a newline after the progress bar */
     }
     printf("Expected: %d frames, Actually received: %d frames\n\n",
            expected_frame_count >
            0 ? expected_frame_count : actual_frame_count,
            actual_frame_count);

     /* Calculate and print MD5 sums */
     if (actual_frame_count > 0) {
         /* Check if we have enough frames for a valid comparison */
         /* Use tx_frame_count from the special frame if available, otherwise use expected_frame_count */
         int target_count =
             (special_frames_received >=
              5) ? tx_frame_count : expected_frame_count;

         /* Only proceed with MD5 calculation if we have exactly the right number of frames */
         if (actual_frame_count == target_count) {	/* Must match exactly */
             calculate_can_id_md5(md5_can_ids);
             calculate_can_data_md5(md5_can_data);

             /* Compare with transmitter values if special frames were received */
             if (special_frames_received >= 5) {
                 /* Print received MD5 values */
                 printf("RECEIVE CAN ID   MD5: ");
                 for (int i = 0; i < 16; i++) {
                     printf("%02x", tx_md5_id[i]);
                 }
                 printf("\n");

                 printf("RECEIVE CAN Data MD5: ");
                 for (int i = 0; i < 16; i++) {
                     printf("%02x", tx_md5_data[i]);
                 }
                 printf("\n");

                 /* Print local MD5 values */
                 printf("LOCAL   CAN ID   MD5: ");
                 printMD5(md5_can_ids);

                 printf("LOCAL   CAN Data MD5: ");
                 printMD5(md5_can_data);

                 /* Compare MD5 values */
                 bool id_md5_match =
                     (memcmp(tx_md5_id, md5_can_ids, 16) == 0);
                 bool data_md5_match =
                     (memcmp(tx_md5_data, md5_can_data, 16) ==
                      0);

                 /* Print match result with color */
                 if (id_md5_match && data_md5_match) {
                     printf("MD5 \033[32mMATCH\033[0m\n");	/* Green for match */
                 } else {
                     printf("MD5 \033[31mNOT MATCH\033[0m\n");	/* Red for not match */
                 }

                 /* Print frame count comparison */
                 if (tx_frame_count != actual_frame_count) {
                     printf
                         ("\033[31mERROR:\033[0m Frame count mismatch: TX reported %d frames, RX received %d frames\n",
                          tx_frame_count,
                          actual_frame_count);
                 } else {
                     printf
                         ("\033[32mPERFECT:\033[0m Received all frames (100%%)\n");
                 }
             } else {
                 printf
                     ("\nNo special frames received from transmitter for verification.\n");
             }
         } else {
             printf
                 ("\nFrame count mismatch, MD5 calculation skipped.\n");
             printf
                 ("Received %d frames, needed exactly %d frames.\n",
                  actual_frame_count, target_count);
         }
     } else {
         printf("\nNo frames received for MD5 calculation.\n");
     }
 }

 /* FUNCTION ************************************************************************************************************
  *
  * Function Name : transmit_file
  * Description   : Transmits a file over CAN bus by breaking it into CAN frames.
  *                 Sends file size and MD5 information for verification.
  *
  * END ****************************************************************************************************************/
 int transmit_file(const char *ifname, int interval_ns, const char *file_path,
           uint32_t fixed_can_id)
 {
     FILE *file = fopen(file_path, "rb");
     if (!file) {
         perror("Failed to open file for transmission");
         return -1;
     }

     /* Get file size */
     fseek(file, 0, SEEK_END);
     long file_size = ftell(file);
     fseek(file, 0, SEEK_SET);

     if (file_size <= 0) {
         printf("Error: File is empty or invalid\n");
         fclose(file);
         return -1;
     }

     /* Calculate number of frames needed (8 bytes per frame) */
     int frame_count = (file_size + 7) / 8;	/* Ceiling division */

     printf("TX File: %s, Size: %ld bytes, Frames: %d\n", file_path,
            file_size, frame_count);

     /* Set up interval */
     struct timespec ts;
     ts.tv_sec = interval_ns / 1000000000;
     ts.tv_nsec = interval_ns % 1000000000;

     /* Variables for progress bar */
     int progress_bar_width = 50;
     int update_interval = frame_count / 100;
     if (update_interval < 1)
         update_interval = 1;

     /* Calculate file MD5 */
     uint8_t file_md5[16];
     calculate_file_md5(file_path, file_md5);

     printf("File MD5: ");
     for (int i = 0; i < 16; i++) {
         printf("%02x", file_md5[i]);
     }
     printf("\n");

     /* Send info frame with file size and frame count */
     struct can_frame info_frame;
     if (extended_frame_mode) {
         info_frame.can_id = 0x1FFFFFFA | CAN_EFF_FLAG;	/* Use extended ID 0x1FFFFFFA for the info frame */
     } else {
         info_frame.can_id = 0x7FA;	/* Use ID 0x7FA for the info frame */
     }
     info_frame.can_dlc = 8;

     /* Store file size in the first 4 bytes (little-endian) */
     info_frame.data[0] = (file_size >> 0) & 0xFF;
     info_frame.data[1] = (file_size >> 8) & 0xFF;
     info_frame.data[2] = (file_size >> 16) & 0xFF;
     info_frame.data[3] = (file_size >> 24) & 0xFF;

     /* Store frame count in the next 4 bytes (little-endian) */
     info_frame.data[4] = (frame_count >> 0) & 0xFF;
     info_frame.data[5] = (frame_count >> 8) & 0xFF;
     info_frame.data[6] = (frame_count >> 16) & 0xFF;
     info_frame.data[7] = (frame_count >> 24) & 0xFF;

     if (debug_mode) {
         printf
             ("Sending info frame with file size: %ld bytes, frame count: %d\n",
              file_size, frame_count);
     }

     /* Send info frame */
     if (write(socket_fd, &info_frame, sizeof(struct can_frame)) !=
         sizeof(struct can_frame)) {
         perror("Write info frame");
         fclose(file);
         return -1;
     }

     /* Longer delay to ensure the receiver has time to start and process the info frame */
     struct timespec long_delay;
     long_delay.tv_sec = 1;	/* 1 second delay */
     long_delay.tv_nsec = 0;
     printf("TX Waiting for RX to process info frame...\n");
     nanosleep(&long_delay, NULL);

     /* Record start time */
     struct timespec start_time, end_time;
     clock_gettime(CLOCK_MONOTONIC, &start_time);

     /* Buffer for reading file */
     uint8_t buffer[8];
     int frames_sent = 0;

     /* Send file data frames */
     while (frames_sent < frame_count && keep_running) {
         struct can_frame frame;

         /* Set CAN ID */
         if (extended_frame_mode) {
             if (fixed_can_id != 0) {
                 frame.can_id =
                     (fixed_can_id & 0x1FFFFFFF) | CAN_EFF_FLAG;
             } else {
                 /* Random extended CAN ID, avoiding special frame IDs */
                 uint32_t random_id;
                 do {
                     random_id =
                         ((uint32_t) rand() << 16) |
                         ((uint32_t) rand() & 0xFFFF);
                     random_id &= 0x1FFFFFFF;	/* Mask to 29 bits */
                 } while (random_id >= 0x1FFFFF7B
                      && random_id <= 0x1FFFFFFF);

                 frame.can_id = random_id | CAN_EFF_FLAG;
             }
         } else {
             if (fixed_can_id != 0) {
                 frame.can_id = fixed_can_id & 0x7FF;
             } else {
                 /* Random standard CAN ID, avoiding special frame IDs */
                 do {
                     frame.can_id = rand() % 0x7FB;
                 } while (frame.can_id >= 0x7FB
                      && frame.can_id <= 0x7FF);
             }
         }

         /* Read data from file */
         size_t bytes_read = fread(buffer, 1, 8, file);
         if (bytes_read == 0) {
             break;	/* End of file */
         }

         /* Set data length */
         frame.can_dlc = bytes_read;

         /* Copy data to frame */
         memcpy(frame.data, buffer, bytes_read);

         /* Send frame */
         if (write(socket_fd, &frame, sizeof(struct can_frame)) !=
             sizeof(struct can_frame)) {
             perror("Write data frame");
             fclose(file);
             return -1;
         }

         frames_sent++;

         /* Update progress bar */
         if (frames_sent % update_interval == 0
             || frames_sent == frame_count) {
             /* Calculate current elapsed time for real-time FPS */
             struct timespec current_time;
             clock_gettime(CLOCK_MONOTONIC, &current_time);
             double current_elapsed =
                 (current_time.tv_sec - start_time.tv_sec) +
                 (current_time.tv_nsec -
                  start_time.tv_nsec) / 1000000000.0;

             display_progress_bar(frames_sent, frame_count,
                          progress_bar_width,
                          current_elapsed);
         }

         /* Sleep for the specified interval */
         nanosleep(&ts, NULL);
     }

     /* Record end time */
     clock_gettime(CLOCK_MONOTONIC, &end_time);
     double elapsed_seconds = (end_time.tv_sec - start_time.tv_sec) +
         (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

     /* Calculate FPS */
     double fps = frames_sent / elapsed_seconds;

     /* Display final progress */
     display_progress_bar(frames_sent, frame_count, progress_bar_width,
                  elapsed_seconds);
     printf("\n");

     printf("TX File transfer complete: %d/%d frames sent, %.2f FPS\n",
            frames_sent, frame_count, fps);

     /* Send MD5 checksum frames (2 frames for 16 bytes) */
     struct can_frame md5_frame1, md5_frame2;

     /* First MD5 frame (first 8 bytes) */
     md5_frame1.can_id = 0x7FF;
     md5_frame1.can_dlc = 8;
     memcpy(md5_frame1.data, file_md5, 8);

     /* Second MD5 frame (second 8 bytes) */
     md5_frame2.can_id = 0x7FE;
     md5_frame2.can_dlc = 8;
     memcpy(md5_frame2.data, file_md5 + 8, 8);

     /* Send frame count and FPS */
     struct can_frame count_frame;
     count_frame.can_id = 0x7FB;
     count_frame.can_dlc = 8;

     /* Store frame count in the first 4 bytes (little-endian) */
     count_frame.data[0] = (frames_sent >> 0) & 0xFF;
     count_frame.data[1] = (frames_sent >> 8) & 0xFF;
     count_frame.data[2] = (frames_sent >> 16) & 0xFF;
     count_frame.data[3] = (frames_sent >> 24) & 0xFF;

     /* Store FPS in the next 4 bytes (little-endian) */
     int fps_int = (int)fps;
     count_frame.data[4] = (fps_int >> 0) & 0xFF;
     count_frame.data[5] = (fps_int >> 8) & 0xFF;
     count_frame.data[6] = (fps_int >> 16) & 0xFF;
     count_frame.data[7] = (fps_int >> 24) & 0xFF;

     /* Send MD5 and count frames */
     if (write(socket_fd, &md5_frame1, sizeof(struct can_frame)) !=
         sizeof(struct can_frame)) {
         perror("Write MD5 frame 1");
     }

     nanosleep(&ts, NULL);	/* Small delay between frames */

     if (write(socket_fd, &md5_frame2, sizeof(struct can_frame)) !=
         sizeof(struct can_frame)) {
         perror("Write MD5 frame 2");
     }

     nanosleep(&ts, NULL);	/* Small delay between frames */

     if (write(socket_fd, &count_frame, sizeof(struct can_frame)) !=
         sizeof(struct can_frame)) {
         perror("Write count frame");
     }

     fclose(file);
     return 0;
 }

 /* FUNCTION ************************************************************************************************************
  *
  * Function Name : free_system_caches
  * Description   : Frees system caches to reduce memory fragmentation.
  *                 Uses Linux's drop_caches mechanism and memory allocation/deallocation.
  *
  * END ****************************************************************************************************************/
 void free_system_caches()
 {
     /* Try to drop caches using Linux's drop_caches mechanism */
     FILE *fp = fopen("/proc/sys/vm/drop_caches", "w");
     if (fp) {
         /* Write '3' to drop all caches (pagecache, dentries and inodes) */
         fprintf(fp, "3");
         fclose(fp);
     }

     /* Force a garbage collection by allocating and freeing memory */
     for (int i = 0; i < 5; i++) {
         void *ptr = malloc(1024 * 1024);	/* Allocate 1MB */
         if (ptr) {
             /* Touch the memory to ensure it's actually allocated */
             memset(ptr, 0, 1024 * 1024);
             free(ptr);
         }
     }

     /* Sleep briefly to let the system process our requests */
     struct timespec ts;
     ts.tv_sec = 0;
     ts.tv_nsec = 100000000;	/* 100ms */
     nanosleep(&ts, NULL);
 }

 /* FUNCTION ************************************************************************************************************
  *
  * Function Name : receive_file
  * Description   : Receives a file over CAN bus and saves it to disk.
  *                 Verifies file integrity using MD5 checksums.
  *
  * END ****************************************************************************************************************/
 int receive_file(const char *ifname, const char *file_path)
 {
     struct can_frame frame;
     fd_set readfds;
     int ret;

     /* Variables for file reception */
     unsigned long file_size = 0;
     int expected_frame_count = 0;
     int frames_received = 0;
     uint8_t *file_buffer = NULL;
     long bytes_written = 0;

     /* Variables for progress bar */
     int progress_bar_width = 50;
     int update_interval = 100;	/* Will be updated after receiving info frame */

     /* Variables for MD5 verification */
     uint8_t tx_md5[16] = { 0 };
     uint8_t rx_md5[16] = { 0 };
     int tx_frame_count = 0;
     int tx_fps = 0;
     int special_frames_received = 0;

     printf("RX Waiting for file info frame...\n");

     /* Wait for the info frame with file size and frame count */
     while (keep_running) {
         FD_ZERO(&readfds);
         FD_SET(socket_fd, &readfds);

         /* No timeout - wait indefinitely for the first frame */
         ret = select(socket_fd + 1, &readfds, NULL, NULL, NULL);

         if (ret < 0) {
             if (errno == EINTR) {
                 if (!keep_running)
                     return -1;
                 continue;
             }
             perror("select");
             return -1;
         }

         if (!FD_ISSET(socket_fd, &readfds)) {
             continue;
         }

         /* Read a frame */
         ssize_t nbytes =
             read(socket_fd, &frame, sizeof(struct can_frame));
         if (nbytes < 0) {
             perror("Read");
             return -1;
         }

         /* Check if this is the info frame (ID 0x7FA) */
         if (frame.can_id == 0x7FA) {
             /* Extract file size from first 4 bytes (little-endian) */
             file_size = ((uint32_t) frame.data[0]) |
                 ((uint32_t) frame.data[1] << 8) |
                 ((uint32_t) frame.data[2] << 16) |
                 ((uint32_t) frame.data[3] << 24);

             /* Extract frame count from next 4 bytes (little-endian) */
             expected_frame_count = ((uint32_t) frame.data[4]) |
                 ((uint32_t) frame.data[5] << 8) |
                 ((uint32_t) frame.data[6] << 16) |
                 ((uint32_t) frame.data[7] << 24);

             /* Sanity check */
             if (file_size <= 0 || file_size > 1000000000
                 || expected_frame_count <= 0
                 || expected_frame_count > 10000000) {
                 printf
                     ("Invalid file info: size=%ld bytes, frames=%d\n",
                      file_size, expected_frame_count);
                 return -1;
             }

             printf
                 ("RX File info received: size=%ld bytes, frames=%d\n",
                  file_size, expected_frame_count);

             /* Allocate buffer for file data */
             file_buffer = (uint8_t *) malloc(file_size);
             if (!file_buffer) {
                 perror("Failed to allocate file buffer");
                 return -1;
             }

             /* Update progress bar interval */
             update_interval = expected_frame_count / 100;
             if (update_interval < 1)
                 update_interval = 1;

             break;	/* Exit the loop after receiving the info frame */
         }
     }

     /* Record start time */
     struct timespec start_time, end_time;
     clock_gettime(CLOCK_MONOTONIC, &start_time);

     /* Set timeout for subsequent frames */
     struct timeval timeout;
     timeout.tv_sec = 3;	/* 3 seconds timeout */
     timeout.tv_usec = 0;

     printf("RX Start receiving file data...\n");

     /* Receive file data frames */
     while (keep_running) {
         FD_ZERO(&readfds);
         FD_SET(socket_fd, &readfds);

         /* Wait for data or timeout */
         ret = select(socket_fd + 1, &readfds, NULL, NULL, &timeout);

         if (ret == 0) {
             /* Timeout occurred */
             if (frames_received > 0) {
                 /* If we've received some frames but not all, and no activity for 3 seconds, */
                 /* check if we have all special frames */
                 if (special_frames_received >= 3) {
                     printf
                         ("\nTimeout: No frames received for 3 seconds, but all special frames received\n");
                     break;
                 } else {
                     printf
                         ("\nTimeout: No frames received for 3 seconds, waiting for special frames...\n");
                     /* Reset timeout and continue waiting */
                     timeout.tv_sec = 3;
                     timeout.tv_usec = 0;
                     continue;
                 }
             } else {
                 printf
                     ("\nTimeout: No frames received for 3 seconds\n");
                 break;
             }
         }

         if (ret < 0) {
             if (errno == EINTR) {
                 if (!keep_running)
                     break;
                 continue;
             }
             perror("select");
             break;
         }

         if (!FD_ISSET(socket_fd, &readfds)) {
             continue;
         }

         /* Read a frame */
         ssize_t nbytes =
             read(socket_fd, &frame, sizeof(struct can_frame));
         if (nbytes < 0) {
             perror("Read");
             break;
         }

         /* Check if this is a special frame */
         if (frame.can_id == 0x7FF || frame.can_id == 0x7FE
             || frame.can_id == 0x7FB) {
             if (frame.can_id == 0x7FF) {	/* First MD5 frame (first 8 bytes) */
                 memcpy(tx_md5, frame.data, 8);
                 special_frames_received++;
                 if (debug_mode) {
                     printf
                         ("Received TX MD5 frame (first 8 bytes)\n");
                 }
             } else if (frame.can_id == 0x7FE) {	/* Second MD5 frame (second 8 bytes) */
                 memcpy(tx_md5 + 8, frame.data, 8);
                 special_frames_received++;
                 if (debug_mode) {
                     printf
                         ("Received TX MD5 frame (second 8 bytes)\n");
                 }
             } else if (frame.can_id == 0x7FB) {	/* Frame count and FPS */
                 /* Extract frame count from first 4 bytes (little-endian) */
                 tx_frame_count = ((uint32_t) frame.data[0]) |
                     ((uint32_t) frame.data[1] << 8) |
                     ((uint32_t) frame.data[2] << 16) |
                     ((uint32_t) frame.data[3] << 24);

                 /* Extract FPS from next 4 bytes (little-endian) */
                 tx_fps = ((uint32_t) frame.data[4]) |
                     ((uint32_t) frame.data[5] << 8) |
                     ((uint32_t) frame.data[6] << 16) |
                     ((uint32_t) frame.data[7] << 24);

                 special_frames_received++;
                 if (debug_mode) {
                     printf
                         ("Received TX frame count frame: count=%d, fps=%d\n",
                          tx_frame_count, tx_fps);
                 }
             }

             /* If we've received all special frames, we can stop */
             if (special_frames_received >= 3
                 && frames_received >= expected_frame_count) {
                 printf("\nAll special frames received\n");
                 break;
             }

             /* Reset timeout after receiving a special frame */
             timeout.tv_sec = 3;
             timeout.tv_usec = 0;

             continue;	/* Skip processing for special frames */
         }

         /* Process regular data frame */
         if (file_buffer && frames_received < expected_frame_count) {
             /* Copy frame data to file buffer */
             long offset = frames_received * 8;	/* 8 bytes per frame */

             /* Make sure we don't write beyond the file size */
             size_t bytes_to_write = frame.can_dlc;
             if (offset + bytes_to_write > file_size) {
                 bytes_to_write = file_size - offset;
             }

             if (bytes_to_write > 0) {
                 memcpy(file_buffer + offset, frame.data,
                        bytes_to_write);
                 bytes_written += bytes_to_write;
             }

             frames_received++;

             /* Update progress bar */
             if (frames_received % update_interval == 0
                 || frames_received == expected_frame_count) {
                 /* Calculate current elapsed time for real-time FPS */
                 struct timespec current_time;
                 clock_gettime(CLOCK_MONOTONIC, &current_time);
                 double current_elapsed =
                     (current_time.tv_sec - start_time.tv_sec) +
                     (current_time.tv_nsec -
                      start_time.tv_nsec) / 1000000000.0;

                 display_progress_bar(frames_received,
                              expected_frame_count,
                              progress_bar_width,
                              current_elapsed);
             }

             /* Reset timeout after receiving a data frame */
             timeout.tv_sec = 3;
             timeout.tv_usec = 0;
         }

         /* If we've received all expected frames, wait for special frames */
         if (frames_received >= expected_frame_count
             && special_frames_received < 3) {
             if (frames_received == expected_frame_count) {
                 printf
                     ("\nReceived all %d data frames, waiting for special frames...\n",
                      frames_received);
             }
         }
     }

     /* Record end time */
     clock_gettime(CLOCK_MONOTONIC, &end_time);
     double elapsed_seconds = (end_time.tv_sec - start_time.tv_sec) +
         (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

     /* Calculate FPS */
     double fps = frames_received / elapsed_seconds;

     /* Display final progress */
     display_progress_bar(frames_received, expected_frame_count,
                  progress_bar_width, elapsed_seconds);
     printf("\n");

     printf("RX File reception complete: %d/%d frames received, %.2f FPS\n",
            frames_received, expected_frame_count, fps);

     /* Calculate MD5 of received data */
     MD5Context ctx;
     md5Init(&ctx);
     md5Update(&ctx, file_buffer, file_size);
     md5Finalize(&ctx);
     memcpy(rx_md5, ctx.digest, 16);

     /* Print MD5 checksums */
     printf("TX MD5: ");
     for (int i = 0; i < 16; i++) {
         printf("%02x", tx_md5[i]);
     }
     printf("\n");

     printf("RX MD5: ");
     for (int i = 0; i < 16; i++) {
         printf("%02x", rx_md5[i]);
     }
     printf("\n");

     /* Compare MD5 checksums */
     bool md5_match = (memcmp(tx_md5, rx_md5, 16) == 0);

     if (md5_match) {
         printf("MD5 \033[32mMATCH\033[0m\n");	/* Green for match */
     } else {
         printf("MD5 \033[31mNOT MATCH\033[0m\n");	/* Red for not match */
     }

     /* Compare frame counts */
     if (tx_frame_count != frames_received) {
         printf
             ("\033[31mERROR:\033[0m Frame count mismatch: TX reported %d frames, RX received %d frames\n",
              tx_frame_count, frames_received);
     } else {
         printf("\033[32mPERFECT:\033[0m Received all frames (100%%)\n");
     }

     /* Write received data to file if MD5 matches */
     if (md5_match) {
         FILE *file = fopen(file_path, "wb");
         if (!file) {
             perror("Failed to open output file");
             free(file_buffer);
             return -1;
         }

         size_t written = fwrite(file_buffer, 1, file_size, file);
         if (written != file_size) {
             printf("Error: Only wrote %zu of %ld bytes to file\n",
                    written, file_size);
             fclose(file);
             free(file_buffer);
             return -1;
         }

         fclose(file);
         printf("File saved to: %s\n", file_path);
     } else {
         printf("File not saved due to MD5 mismatch\n");
     }

     /* Free file buffer */
     free(file_buffer);

     return md5_match ? 0 : -1;
 }

 /* FUNCTION ************************************************************************************************************
  *
  * Function Name : print_usage
  * Description   : Prints program usage information and available command-line options.
  *                 Includes both short (-x) and long (--xxx) option formats.
  *
  * END ****************************************************************************************************************/
 void print_usage(const char *program_name)
 {
     printf("Usage: %s [options]\n\n", program_name);
     printf("SocketCAN performance testing utility\n");
     printf("This program implements a comprehensive CAN bus testing and benchmarking utility\n");
     printf("for performance testing, protocol verification, and file transfer.\n\n");

     printf("Options:\n");
     printf("  -r, --receive         Receive mode (default is transmit mode if not specified)\n");
     printf("  -t, --interval=TIME   Set transmit interval in nanoseconds (default: 60000)\n");
     printf("  -n, --count=NUM       Number of frames to send/receive (default: unlimited)\n");
     printf("  -i, --interface=NAME  CAN interface name (default: can0)\n");
     printf("  -I, --id=CANID        Fixed CAN ID for transmission (default: random)\n");
     printf("                        Can be specified in decimal or hex (with 0x prefix)\n");
     printf("  -e, --extended        Use extended frame format (29-bit CAN ID) instead of\n");
     printf("                        standard (11-bit)\n");
     printf("  -d, --debug=LEVEL     Debug mode: 0=off, 1=on (default: 0)\n");
     printf("  -f, --file=PATH       File transfer mode: TX reads from file, RX saves to file\n");
     printf("  -h, --help            Show this help message\n");

     printf("\nExamples:\n");
     printf("  %s -t 100000 -n 1000 -i can0       # Send 1000 frames with 100us interval\n", program_name);
     printf("  %s -r -i can0                     # Receive frames\n", program_name);
     printf("  %s -e -I 0x12345678 -i can0       # Send extended frames (29-bit) with fixed ID\n", program_name);
     printf("  %s -f data.bin -i can0            # Transmit file data.bin\n", program_name);
     printf("  %s -r -f received.bin -i can0     # Receive file and save as received.bin\n", program_name);

     printf("\nUsing virtual CAN (vcan0) for testing:\n");
     printf("  1. Load vcan kernel module:\n");
     printf("     sudo modprobe vcan\n");
     printf("  2. Create virtual CAN interface:\n");
     printf("     sudo ip link add dev vcan0 type vcan\n");
     printf("  3. Bring up the interface:\n");
     printf("     sudo ip link set up vcan0\n");
     printf("  4. Run in transmit mode (terminal 1):\n");
     printf("     %s -i vcan0 -t 100000 -n 100\n", program_name);
     printf("  5. Run in receive mode (terminal 2):\n");
     printf("     %s -r -i vcan0\n", program_name);
     printf("  6. For file transfer testing (terminal 1 & 2):\n");
     printf("     %s -f myfile.bin -i vcan0       # Terminal 1 (sender)\n", program_name);
     printf("     %s -r -f received.bin -i vcan0  # Terminal 2 (receiver)\n", program_name);
 }

 /* Define long options structure for getopt_long */
 static struct option long_options[] = {
     {"receive",   no_argument,       0, 'r'},
     {"interval",  required_argument, 0, 't'},
     {"count",     required_argument, 0, 'n'},
     {"interface", required_argument, 0, 'i'},
     {"id",        required_argument, 0, 'I'},
     {"extended",  no_argument,       0, 'e'},
     {"debug",     required_argument, 0, 'd'},
     {"file",      required_argument, 0, 'f'},
     {"help",      no_argument,       0, 'h'},
     {0, 0, 0, 0}
 };

 /* FUNCTION ************************************************************************************************************
  *
  * Function Name : main
  * Description   : Main entry point for the CAN performance testing utility.
  *                 Parses command-line arguments and runs in the specified mode.
  *
  * END ****************************************************************************************************************/
 int main(int argc, char **argv)
 {
     /* Initialize frame buffer pointer to NULL */
     frame_buffer.frames = NULL;

     /* Free system caches to reduce memory fragmentation */
     free_system_caches();
     int opt;
     int option_index = 0;
     int receive_mode = 0;	/* Default to transmit mode */
     int interval_ns = 60000;	/* Default: 60us */
     int max_frames = 0;	/* Default: unlimited */
     char ifname[IFNAMSIZ] = "can0";	/* Default interface */
     uint32_t fixed_can_id = 0;	/* Default: random CAN ID (0 means random) */
     char *endptr;

     /* Parse command line arguments using getopt_long */
     while ((opt = getopt_long(argc, argv, "rt:n:i:I:ed:f:h", long_options, &option_index)) != -1) {
         switch (opt) {
         case 'r':
             receive_mode = 1;
             break;
         case 't':
             /* Validate interval value */
             if (optarg == NULL || *optarg == '\0') {
                 printf("Error: Option -t/--interval requires a valid numeric value\n");
                 print_usage(argv[0]);
                 return EXIT_FAILURE;
             }

             interval_ns = atoi(optarg);
             if (interval_ns < 0) {
                 printf("Error: Interval value must be non-negative\n");
                 print_usage(argv[0]);
                 return EXIT_FAILURE;
             }
             break;
         case 'n':
             /* Validate frame count */
             if (optarg == NULL || *optarg == '\0') {
                 printf("Error: Option -n/--count requires a valid numeric value\n");
                 print_usage(argv[0]);
                 return EXIT_FAILURE;
             }

             max_frames = atoi(optarg);
             if (max_frames < 0) {
                 printf("Error: Frame count must be non-negative\n");
                 print_usage(argv[0]);
                 return EXIT_FAILURE;
             }
             break;
         case 'i':
             /* Validate interface name */
             if (optarg == NULL || *optarg == '\0') {
                 printf("Error: Option -i/--interface requires a valid interface name\n");
                 print_usage(argv[0]);
                 return EXIT_FAILURE;
             }

             strncpy(ifname, optarg, IFNAMSIZ - 1);
             ifname[IFNAMSIZ - 1] = '\0'; /* Ensure null termination */
             break;
         case 'I':
             /* Validate CAN ID */
             if (optarg == NULL || *optarg == '\0') {
                 printf("Error: Option -I/--id requires a valid CAN ID value\n");
                 print_usage(argv[0]);
                 return EXIT_FAILURE;
             }

             /* Parse CAN ID (accept decimal or hex with 0x prefix) */
             if (strncmp(optarg, "0x", 2) == 0) {
                 fixed_can_id = (uint32_t)strtol(optarg, &endptr, 16);
             } else {
                 fixed_can_id = (uint32_t)strtol(optarg, &endptr, 10);
             }

             /* Check if conversion was successful */
             if (*endptr != '\0') {
                 printf("Error: Invalid CAN ID format: %s\n", optarg);
                 print_usage(argv[0]);
                 return EXIT_FAILURE;
             }

             /* Check if the CAN ID is in the reserved range for special frames */
             if (!extended_frame_mode) {
                 /* For standard frames, check 11-bit ID range */
                 if (fixed_can_id > 0x7FF) {
                     printf("Error: Standard CAN ID must be in range 0x000-0x7FF (0-2047)\n");
                     print_usage(argv[0]);
                     return EXIT_FAILURE;
                 }

                 if ((fixed_can_id & 0x7FF) >= 0x7FB && (fixed_can_id & 0x7FF) <= 0x7FF) {
                     printf("Error: CAN ID 0x%03X is reserved for special frames. Please use a different ID.\n",
                            fixed_can_id & 0x7FF);
                     print_usage(argv[0]);
                     return EXIT_FAILURE;
                 }
             } else {
                 /* For extended frames, check 29-bit ID range */
                 if (fixed_can_id > 0x1FFFFFFF) {
                     printf("Error: Extended CAN ID must be in range 0x00000000-0x1FFFFFFF (0-536870911)\n");
                     print_usage(argv[0]);
                     return EXIT_FAILURE;
                 }

                 if ((fixed_can_id & 0x1FFFFFFF) >= 0x1FFFFF7B &&
                     (fixed_can_id & 0x1FFFFFFF) <= 0x1FFFFFFF) {
                     printf("Error: Extended CAN ID 0x%08X is reserved for special frames. Please use a different ID.\n",
                            fixed_can_id & 0x1FFFFFFF);
                     print_usage(argv[0]);
                     return EXIT_FAILURE;
                 }
             }
             break;
         case 'e':
             /* Enable extended frame mode (29-bit CAN ID) */
             extended_frame_mode = 1;
             break;
         case 'd':
             /* Validate debug level */
             if (optarg == NULL || *optarg == '\0') {
                 printf("Error: Option -d/--debug requires a valid debug level (0 or 1)\n");
                 print_usage(argv[0]);
                 return EXIT_FAILURE;
             }

             debug_mode = atoi(optarg);
             if (debug_mode != 0 && debug_mode != 1) {
                 printf("Error: Debug level must be 0 (off) or 1 (on)\n");
                 print_usage(argv[0]);
                 return EXIT_FAILURE;
             }
             break;
         case 'f':
             /* Validate file path */
             if (optarg == NULL || *optarg == '\0') {
                 printf("Error: Option -f/--file requires a valid file path\n");
                 print_usage(argv[0]);
                 return EXIT_FAILURE;
             }

             file_mode = 1;
             file_path = strdup(optarg); /* Make a copy of the file path */
             if (!file_path) {
                 perror("Memory allocation for file path");
                 print_usage(argv[0]);
                 return EXIT_FAILURE;
             }
             break;
         case 'h':
             print_usage(argv[0]);
             return EXIT_SUCCESS;
         case '?':
             /* getopt_long already printed an error message */
             print_usage(argv[0]);
             return EXIT_FAILURE;
         default:
             printf("Unknown option: %c\n", opt);
             print_usage(argv[0]);
             return EXIT_FAILURE;
         }
     }

     /* Check for any non-option arguments which are not supported */
     if (optind < argc) {
         printf("Error: Unexpected non-option argument: %s\n", argv[optind]);
         print_usage(argv[0]);
         return EXIT_FAILURE;
     }

     /* No need to check for mode specification - default is transmit if -r is not specified */

     /* Initialize random number generator */
     srand(time(NULL));

     /* Set up signal handler */
     signal(SIGINT, signal_handler);
     signal(SIGTERM, signal_handler);

     /* Initialize CAN socket */
     socket_fd = init_can_socket(ifname);
     if (socket_fd < 0) {
         printf("Error: Failed to initialize CAN socket for interface %s\n", ifname);
         print_usage(argv[0]);
         return EXIT_FAILURE;
     }

     /* Run in the specified mode */
     if (file_mode) {
         /* File transfer mode */
         if (receive_mode) {
             /* Receive file mode */
             receive_file(ifname, file_path);
         } else {
             /* Transmit file mode */
             transmit_file(ifname, interval_ns, file_path,
                       fixed_can_id);
         }
     } else {
         /* Normal CAN frame mode */
         if (receive_mode) {
             receive_frames(ifname, max_frames);
         } else {
             /* Default to transmit mode */
             transmit_frames(ifname, interval_ns, max_frames,
                     fixed_can_id);
         }
     }

     /* Clean up */
     if (socket_fd >= 0) {
         close(socket_fd);
     }

     /* Free the frame buffer */
     free_frame_buffer();

     /* Free file path if allocated */
     if (file_path) {
         free(file_path);
         file_path = NULL;
     }

     return EXIT_SUCCESS;
 }
