// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#ifndef _ISOBUSFS_H_
#define _ISOBUSFS_H_

#include <stdint.h>
#include <endian.h>
#include <stdbool.h>

#include <linux/can.h>
#include <linux/kernel.h>
#include "../libj1939.h"
#include "../lib.h"

/* ISO 11783-13:2021 - C.1.1.a File Server to Client PGN */
#define ISOBUSFS_PGN_FS_TO_CL			0x0ab00 /* 43766 */
/* ISO 11783-13:2021 - C.1.1.b Client to File Server PGN */
#define ISOBUSFS_PGN_CL_TO_FS			0x0aa00 /* 43520 */

#define ISOBUSFS_PRIO_DEFAULT			7
#define ISOBUSFS_PRIO_FSS			5
#define ISOBUSFS_PRIO_ACK			6
#define ISOBUSFS_MAX_OPENED_FILES		255
#define ISOBUSFS_MAX_SHORT_FILENAME_LENGH	12 /* 12 chars */
#define ISOBUSFS_MAX_LONG_FILENAME_LENGH	31 /* 31 chars */
/* ISO 11783-13:2021 - C.3.5.1 Maximal transfer size for TP (Transport Protocol) */
#define ISOBUSFS_TP_MAX_TRANSFER_SIZE		1780
/* ISO 11783-13:2021 - C.3.5.1 Maximal transfer size for ETP (Extended Transport Protocol) */
#define ISOBUSFS_ETP_MAX_TRANSFER_SIZE		65530
#define ISOBUSFS_MAX_DATA_LENGH			65530 /* Bytes */
#define ISOBUSFS_MAX_TRANSFER_LENGH		(6 + ISOBUSFS_MAX_DATA_LENGH)
#define ISOBUSFS_MIN_TRANSFER_LENGH		8
#define ISOBUSFS_CLIENT_TIMEOUT			6000 /* ms */
#define ISOBUSFS_FS_TIMEOUT			6000 /* ms */
#define ISOBUSFS_MAX_BUF_ENTRIES		10
#define ISOBUSFS_MAX_PATH_NAME_LENGTH		ISOBUSFS_MAX_DATA_LENGH

/* not documented, take some max value */
#define ISOBUSFS_SRV_MAX_VOLUMES		10
/* ISO 11783-13:2021 A.2.2.3 Volumes */
#define ISOBUSFS_SRV_MAX_VOLUME_NAME_LEN	254
#define ISOBUSFS_MAX_VOLUME_NAME_LENGTH		254
#define ISOBUSFS_MAX_DIR_ENTRY_NAME_LENGTH	255
/* not documented, take some max value */
#define ISOBUSFS_SRV_MAX_PATH_LEN		4096

#define ISOBUSFS_FILE_HANDLE_ERROR		255

/* ISO 11783-3:2018 - 5.4.5 Acknowledgment */
#define ISOBUS_PGN_ACK				0x0e800 /* 59392 */

enum isobusfs_ack_ctrl {
	ISOBUS_ACK_CTRL_ACK = 0,
	ISOBUS_ACK_CTRL_NACK = 1,
};

struct isobusfs_nack {
	uint8_t ctrl;
	uint8_t group_function;
	uint8_t reserved[2];
	uint8_t address_nack;
	uint8_t pgn_nack[3];
};

/* ISO 11783-13:2021 - Annex B.1 Command Groups (CG) */
enum isobusfs_cg {
	ISOBUSFS_CG_CONNECTION_MANAGMENT = 0,
	ISOBUSFS_CG_DIRECTORY_HANDLING = 1,
	ISOBUSFS_CG_FILE_ACCESS = 2,
	ISOBUSFS_CG_FILE_HANDLING = 3,
	ISOBUSFS_CG_VOLUME_HANDLING = 4,
};

#define ISOBUSFS_CM_F_CCM_RATE	2000 /* ms */

/* Connection Management functions: */
/* ISO 11783-13:2021 - C.1.* Connection Management - Client to File Server
 * functions:
 */
enum isobusfs_cm_cl_to_fs_function {
	/* ISO 11783-13:2021 - C.1.3 Client Connection Maintenance */
	ISOBUSFS_CM_F_CC_MAINTENANCE = 0,
	/* ISO 11783-13:2021 - C.1.4 Get File Server Properties */
	ISOBUSFS_CM_GET_FS_PROPERTIES = 1,
	/* ISO 11783-13:2021 - C.1.6 Volume Status Request */
	ISOBUSFS_CM_VOLUME_STATUS_REQ = 2,
};

/* ISO 11783-13:2021 - C.1.* Connection Management - File Server to client
 * functions:
 */
enum isobusfs_cm_fs_to_cl_function {
	/* ISO 11783-13:2021 - C.1.2 File Server Status */
	ISOBUSFS_CM_F_FS_STATUS = 0,
	/* ISO 11783-13:2021 - C.1.5 Get File Server Properties Response */
	ISOBUSFS_CM_GET_FS_PROPERTIES_RES = 1,
	/* ISO 11783-13:2021 - C.1.7 Volume Status Response */
	ISOBUSFS_CM_VOLUME_STATUS_RES = 2,
};

/* Directory Handling functions: */
/* send by server: */
enum isobusfs_dh_fs_to_cl_function {
	ISOBUSFS_DH_F_GET_CURRENT_DIR_RES = 0,
	ISOBUSFS_DH_F_CHANGE_CURRENT_DIR_RES = 1,
};

/* send by client: */
enum isobusfs_dh_cl_to_fs_function {
	ISOBUSFS_DH_F_GET_CURRENT_DIR_REQ = 0,
	ISOBUSFS_DH_F_CHANGE_CURRENT_DIR_REQ = 1,
};

/* File Access functions: */
/* send by server: */
enum isobusfs_fa_fs_to_cl_function {
	ISOBUSFS_FA_F_OPEN_FILE_RES = 0,
	ISOBUSFS_FA_F_SEEK_FILE_RES = 1,
	ISOBUSFS_FA_F_READ_FILE_RES = 2,
	ISOBUSFS_FA_F_WRITE_FILE_RES = 3,
	ISOBUSFS_FA_F_CLOSE_FILE_RES = 4,
};

/* send by client: */
enum isobusfs_fa_cl_to_fs_function {
	ISOBUSFS_FA_F_OPEN_FILE_REQ = 0,
	ISOBUSFS_FA_F_SEEK_FILE_REQ = 1,
	ISOBUSFS_FA_F_READ_FILE_REQ = 2,
	ISOBUSFS_FA_F_WRITE_FILE_REQ = 3,
	ISOBUSFS_FA_F_CLOSE_FILE_REQ = 4,
};

/* File Handling functions: */
/* send by server: */
enum isobusfs_fh_fs_to_cl_function {
	ISOBUSFS_FH_F_MOVE_FILE_RES = 0,
	ISOBUSFS_FH_F_DELETE_FILE_RES = 1,
	ISOBUSFS_FH_F_GET_FILE_ATTR_RES = 2,
	ISOBUSFS_FH_F_SET_FILE_ATTR_RES = 3,
	ISOBUSFS_FH_F_GET_FILE_DATETIME_RES = 4,
};

/* send by client: */
enum isobusfs_fh_cl_to_fs_function {
	ISOBUSFS_FH_F_MOVE_FILE_REQ = 0,
	ISOBUSFS_FH_F_DELETE_FILE_REQ = 1,
	ISOBUSFS_FH_F_GET_FILE_ATTR_REQ = 2,
	ISOBUSFS_FH_F_SET_FILE_ATTR_REQ = 3,
	ISOBUSFS_FH_F_GET_FILE_DATETIME_REQ = 4,
};

/* Volume Access functions: */
/* Preparing or repairing the volume for files and directory structures.
 * These commands should be limited to initial setup, intended to be used by
 * service tool clients only.
 */
/* send by server: */
/* Initialize Volume: Prepare the volume to accept files and directories. All
 * data will be lost upon completion of this command.
 */
enum isobusfs_va_fs_to_cl_function {
	ISOBUSFS_VA_F_INITIALIZE_VOLUME_RES = 0,
};

/* send by client: */
enum isobusfs_va_cl_to_fs_function {
	/* Initialize Volume: Prepare the volume to accept files and directories.
	 * All data will be lost upon completion of this command.
	 */
	ISOBUSFS_VA_F_INITIALIZE_VOLUME_REQ = 0,
};

/* ISO 11783-13:2021 - Annex B.9 Error Code */
enum isobusfs_error {
	/* Success */
	ISOBUSFS_ERR_SUCCESS = 0,
	/* Access Denied */
	ISOBUSFS_ERR_ACCESS_DENIED = 1,
	/* Invalid Access */
	ISOBUSFS_ERR_INVALID_ACCESS = 2,
	/* Too many files open */
	ISOBUSFS_ERR_TOO_MANY_FILES_OPEN = 3,
	/* File or path not found */
	ISOBUSFS_ERR_FILE_ORPATH_NOT_FOUND = 4,
	/* Invalid handle */
	ISOBUSFS_ERR_INVALID_HANDLE = 5,
	/* Invalid given source name */
	ISOBUSFS_ERR_INVALID_SRC_NAME = 6,
	/* Invalid given destination name */
	ISOBUSFS_ERR_INVALID_DST_NAME = 7,
	/* Volume out of free space */
	ISOBUSFS_ERR_NO_SPACE = 8,
	/* Failure during a write operation */
	ISOBUSFS_ERR_ON_WRITE = 9,
	/* Media is not present */
	ISOBUSFS_ERR_MEDIA_IS_NOT_PRESENT = 10,
	/* Failure during a read operation */
	ISOBUSFS_ERR_ON_READ = 11,
	/* Function not supported */
	ISOBUSFS_ERR_FUNC_NOT_SUPPORTED = 12,
	/* Volume is possibly not initialized */
	ISOBUSFS_ERR_VOLUME_NOT_INITIALIZED = 13,
	/* Invalid request length */
	ISOBUSFS_ERR_INVALID_REQUESTED_LENGHT = 42,
	/* Out of memory */
	ISOBUSFS_ERR_OUT_OF_MEM = 43,
	/* Any other error */
	ISOBUSFS_ERR_OTHER = 44,
	/* End of file reached, will only be reported when file pointer is at
	 * end of file
	 */
	ISOBUSFS_ERR_END_OF_FILE = 45,
	/* TAN error:
	 * Same TAN, but different request compared to the previous one (change
	 * in content or size).
	 */
	ISOBUSFS_ERR_TAN_ERR = 46,
	/* Malformed request:
	 * Message is shorter than expected. If the message is too short to
	 * provide a TAN (less than 2 bytes), the TAN shall be set to 0xff in
	 * the response.
	 */
	ISOBUSFS_ERR_MALFORMED_REQUEST = 47,
};

/* recursive buffer entry */
struct isobusfs_buf {
	uint8_t data[ISOBUSFS_MIN_TRANSFER_LENGH];
	struct timespec ts;
};

struct isobusfs_buf_log {
	struct isobusfs_buf entries[ISOBUSFS_MAX_BUF_ENTRIES];
	unsigned int index;
};

struct isobusfs_stats {
	int err;
	uint32_t tskey_sch;
	uint32_t tskey_ack;
	uint32_t send;
};

struct isobusfs_msg {
	uint8_t buf[ISOBUSFS_MAX_TRANSFER_LENGH];
	size_t buf_size;
	ssize_t len; /* length of received message */
	struct sockaddr_can peername;
	socklen_t peer_addr_len;
	int sock;
};

struct isobusfs_err_msg {
	struct sock_extended_err *serr;
	struct scm_timestamping *tss;
	struct isobusfs_stats *stats;
};

struct isobusfs_cmn {
	int epoll_fd;
	struct epoll_event *epoll_events;
	size_t epoll_events_size;
	struct timespec next_send_time;
	struct timespec last_time;
};

void isobusfs_init_sockaddr_can(struct sockaddr_can *sac, uint32_t pgn);
int isobusfs_recv_err(int sock, struct isobusfs_err_msg *emsg);

/*
 * min()/max()/clamp() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static inline int isobusfs_buf_to_cmd(uint8_t *buf)
{
	return (buf[0] & 0xf0) >> 4;
}

static inline int isobusfs_buf_to_function(uint8_t *buf)
{
	return (buf[0] & 0xf);
}

static inline uint8_t isobusfs_cg_function_to_buf(enum isobusfs_cg cg,
						   uint8_t func)
{
	return (func & 0xf) | ((cg & 0xf) << 4);
}

const char *isobusfs_error_to_str(enum isobusfs_error err);
enum isobusfs_error linux_error_to_isobusfs_error(int linux_err);

int isobusfs_get_timeout_ms(struct timespec *ts);
void isobusfs_send_nack(int sock, struct isobusfs_msg *msg);
void isobufs_store_tx_data(struct isobusfs_buf_log *buffer, uint8_t *data);
void isobusfs_dump_tx_data(const struct isobusfs_buf_log *buffer);
int isobusfs_sendto(int sock, const void *data, size_t len,
		    const struct sockaddr_can *addr,
		    struct isobusfs_buf_log *isobusfs_tx_buffer);
int isobusfs_send(int sock, const void *data, size_t len,
		  struct isobusfs_buf_log *isobusfs_tx_buffer);

void isobusfs_cmn_dump_last_x_bytes(const uint8_t *buffer, size_t buffer_size,
				    size_t x);

int isobusfs_cmn_open_socket(void);
int isobusfs_cmn_configure_socket_filter(int sock, pgn_t pgn);
int isobusfs_cmn_configure_error_queue(int sock);
int isobusfs_cmn_bind_socket(int sock, struct sockaddr_can *addr);
int isobusfs_cmn_connect_socket(int sock, struct sockaddr_can *addr);
int isobusfs_cmn_set_broadcast(int sock);
int isobusfs_cmn_add_socket_to_epoll(int epoll_fd, int sock, uint32_t events);
int isobusfs_cmn_create_epoll(void);
int isobusfs_cmn_socket_prio(int sock, int prio);
int isobusfs_cmn_set_linger(int sock);

int isobusfs_cmn_prepare_for_events(struct isobusfs_cmn *cmn, int *nfds,
				    bool dont_wait);

/* ============ directory handling ============ */
int isobusfs_cmn_dh_validate_dir_path(const char *path, bool writable);

/* ============ logging ============ */

typedef enum {
	LOG_LEVEL_INT,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_WARN,
	LOG_LEVEL_INFO,
	LOG_LEVEL_DEBUG,
} log_level_t;

void isobusfs_log_level_set(log_level_t level);
void isobusfs_log(log_level_t level, const char *fmt, ...);
void isobusfs_set_interactive(bool interactive);
void isobusfs_print_log_buffer(void);

/* undefine kernel logging macros */
#undef pr_int
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

/* pr_int - print for interactive session  */
#define pr_int(fmt, ...) isobusfs_log(LOG_LEVEL_INT, fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...) isobusfs_log(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...) isobusfs_log(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) isobusfs_log(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...) isobusfs_log(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

#endif /* !_ISOBUSFS_H_ */
