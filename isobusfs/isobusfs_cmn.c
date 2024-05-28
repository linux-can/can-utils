// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <linux/errqueue.h>
#include <linux/net_tstamp.h>
#include <linux/netlink.h>

#include "isobusfs_cmn.h"
#include "isobusfs_srv.h"

static log_level_t log_level = LOG_LEVEL_INFO;
static bool interactive_mode;

#define LOG_BUFFER_SIZE 1024
#define LOG_ENTRY_MAX_SIZE 256

struct isobusfs_log_buffer {
	char buffer[LOG_BUFFER_SIZE][LOG_ENTRY_MAX_SIZE];
	int write_index;
};

struct isobusfs_log_buffer log_buffer = { .write_index = 0 };

void add_log_to_buffer(const char *log_entry)
{
	int idx = log_buffer.write_index;
	char *buffer = log_buffer.buffer[idx];

	strncpy(buffer, log_entry, LOG_ENTRY_MAX_SIZE);
	buffer[LOG_ENTRY_MAX_SIZE - 1] = '\0'; /* Ensure null termination */
	log_buffer.write_index = (idx + 1) % LOG_BUFFER_SIZE;
}

void isobusfs_print_log_buffer(void)
{
	printf("\n---- Log Buffer Start ----\n");
	for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
		int idx = (log_buffer.write_index + i) % LOG_BUFFER_SIZE;

		if (log_buffer.buffer[idx][0] != '\0')
			printf("%s\n", log_buffer.buffer[idx]);
	}
	printf("\n---- Log Buffer End ----\n");
}

void isobusfs_log(log_level_t level, const char *fmt, ...)
{
	char complete_log_entry[LOG_ENTRY_MAX_SIZE];
	char log_entry[LOG_ENTRY_MAX_SIZE - 64];
	const char *level_str;
	struct timeval tv;
	struct tm *time_info;
	char time_buffer[64];
	int milliseconds;
	va_list args;

	if (level > log_level)
		return;

	switch (level) {
	case LOG_LEVEL_DEBUG:
		level_str = "DEBUG";
		break;
	case LOG_LEVEL_INT:
	case LOG_LEVEL_INFO:
		level_str = "INFO";
		break;
	case LOG_LEVEL_WARN:
		level_str = "WARNING";
		break;
	case LOG_LEVEL_ERROR:
		level_str = "ERROR";
		break;
	default:
		level_str = "UNKNOWN";
		break;
	}

	gettimeofday(&tv, NULL);
	time_info = localtime(&tv.tv_sec);
	milliseconds = tv.tv_usec / 1000;
	strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S",
		 time_info);
	snprintf(time_buffer + strlen(time_buffer),
		 sizeof(time_buffer) - strlen(time_buffer),
		 ".%03d", milliseconds);

	va_start(args, fmt);
	vsnprintf(log_entry, sizeof(log_entry), fmt, args);
	va_end(args);

	snprintf(complete_log_entry, sizeof(complete_log_entry),
		 "[%.40s] [%.10s]: %.150s", time_buffer, level_str, log_entry);

	if (interactive_mode) {
		add_log_to_buffer(complete_log_entry);
		if (level == LOG_LEVEL_INT) {
			fprintf(stdout, "%s", log_entry);
			fflush(stdout);
		}
	} else {
		fprintf(stdout, "%s\n", complete_log_entry);
	}
}

void isobusfs_set_interactive(bool interactive)
{
	interactive_mode = interactive;
}

/* set log level */
void isobusfs_log_level_set(log_level_t level)
{
	log_level = level;
}

int isobusfs_get_timeout_ms(struct timespec *ts)
{
	struct timespec curr_time;
	int64_t time_diff;
	int timeout_ms;

	clock_gettime(CLOCK_MONOTONIC, &curr_time);
	time_diff = timespec_diff_ms(ts, &curr_time);
	if (time_diff < 0) {
		/* Too late to send next message. Send it now */
		timeout_ms = 0;
	} else {
		if (time_diff > INT_MAX) {
			warn("timeout too long: %" PRId64 " ms", time_diff);
			time_diff = INT_MAX;
		}

		timeout_ms = time_diff;
	}

	return timeout_ms;
}

const char *isobusfs_error_to_str(enum isobusfs_error err)
{
	switch (err) {
	case ISOBUSFS_ERR_ACCESS_DENIED:
		return "Access Denied";
	case ISOBUSFS_ERR_INVALID_ACCESS:
		return "Invalid Access";
	case ISOBUSFS_ERR_TOO_MANY_FILES_OPEN:
		return "Too many files open";
	case ISOBUSFS_ERR_FILE_ORPATH_NOT_FOUND:
		return "File or path not found";
	case ISOBUSFS_ERR_INVALID_HANDLE:
		return "Invalid handle";
	case ISOBUSFS_ERR_INVALID_SRC_NAME:
		return "Invalid given source name";
	case ISOBUSFS_ERR_INVALID_DST_NAME:
		return "Invalid given destination name";
	case ISOBUSFS_ERR_NO_SPACE:
		return "Volume out of free space";
	case ISOBUSFS_ERR_ON_WRITE:
		return "Failure during a write operation";
	case ISOBUSFS_ERR_MEDIA_IS_NOT_PRESENT:
		return "Media is not present";
	case ISOBUSFS_ERR_VOLUME_NOT_INITIALIZED:
		return "Volume is possibly not initialized";
	case ISOBUSFS_ERR_ON_READ:
		return "Failure during a read operation";
	case ISOBUSFS_ERR_FUNC_NOT_SUPPORTED:
		return "Function not supported";
	case ISOBUSFS_ERR_INVALID_REQUESTED_LENGHT:
		return "Invalid request length";
	case ISOBUSFS_ERR_OUT_OF_MEM:
		return "Out of memory";
	case ISOBUSFS_ERR_OTHER:
		return "Any other error";
	case ISOBUSFS_ERR_END_OF_FILE:
		return "End of file reached, will only be reported when file pointer is at end of file";
	default:
		return "<unknown>";
	}
}

enum isobusfs_error linux_error_to_isobusfs_error(int linux_err)
{
	switch (linux_err) {
	case 0:
		return ISOBUSFS_ERR_SUCCESS;
	case -EINVAL:
		return ISOBUSFS_ERR_INVALID_DST_NAME;
	case -EACCES:
		return ISOBUSFS_ERR_ACCESS_DENIED;
	case -ENOTDIR:
		return ISOBUSFS_ERR_INVALID_ACCESS;
	case -EMFILE:
		return ISOBUSFS_ERR_TOO_MANY_FILES_OPEN;
	case -ENOENT:
		return ISOBUSFS_ERR_FILE_ORPATH_NOT_FOUND;
	case -EBADF:
		return ISOBUSFS_ERR_INVALID_HANDLE;
	case -ENAMETOOLONG:
		return ISOBUSFS_ERR_INVALID_SRC_NAME;
	case -ENOSPC:
		return ISOBUSFS_ERR_NO_SPACE;
	case -EIO:
		return ISOBUSFS_ERR_ON_WRITE;
	case -ENODEV:
		return ISOBUSFS_ERR_MEDIA_IS_NOT_PRESENT;
	case -EROFS:
		return ISOBUSFS_ERR_VOLUME_NOT_INITIALIZED;
	case -EFAULT:
		return ISOBUSFS_ERR_ON_READ;
	case -ENOSYS:
		return ISOBUSFS_ERR_FUNC_NOT_SUPPORTED;
	case -EMSGSIZE:
		return ISOBUSFS_ERR_INVALID_REQUESTED_LENGHT;
	case -ENOMEM:
		return ISOBUSFS_ERR_OUT_OF_MEM;
	case -EPERM:
		return ISOBUSFS_ERR_OTHER;
	case -ESPIPE:
		return ISOBUSFS_ERR_END_OF_FILE;
	case -EPROTO:
		return ISOBUSFS_ERR_TAN_ERR;
	case -EILSEQ:
		return ISOBUSFS_ERR_MALFORMED_REQUEST;
	default:
		return ISOBUSFS_ERR_OTHER;
	}
}


void isobusfs_init_sockaddr_can(struct sockaddr_can *sac, uint32_t pgn)
{
	sac->can_family = AF_CAN;
	sac->can_addr.j1939.addr = J1939_NO_ADDR;
	sac->can_addr.j1939.name = J1939_NO_NAME;
	sac->can_addr.j1939.pgn = pgn;
}

static void isobusfs_print_timestamp(struct isobusfs_err_msg *emsg,
				     const char *name, struct timespec *cur)
{
	struct isobusfs_stats *stats = emsg->stats;

	/* TODO: make it configurable */
	return;

	if (!(cur->tv_sec | cur->tv_nsec))
		return;

	fprintf(stderr, "  %s: %lu s %lu us (seq=%u/%u, send=%u)",
			name, cur->tv_sec, cur->tv_nsec / 1000,
			stats->tskey_sch, stats->tskey_ack, stats->send);

	fprintf(stderr, "\n");
}

static const char *isobusfs_tstype_to_str(int tstype)
{
	switch (tstype) {
	case SCM_TSTAMP_SCHED:
		return "  ENQ";
	case SCM_TSTAMP_SND:
		return "  SND";
	case SCM_TSTAMP_ACK:
		return "  ACK";
	default:
		return "  unk";
	}
}

/* Check the stats of SCM_TIMESTAMPING_OPT_STATS */
static void isobusfs_scm_opt_stats(struct isobusfs_err_msg *emsg, void *buf, int len)
{
	struct isobusfs_stats *stats = emsg->stats;
	int offset = 0;

	while (offset < len) {
		struct nlattr *nla = (struct nlattr *) ((char *)buf + offset);

		switch (nla->nla_type) {
		case J1939_NLA_BYTES_ACKED:
			stats->send = *(uint32_t *)((char *)nla + NLA_HDRLEN);
			break;
		default:
			warnx("not supported J1939_NLA field\n");
		}

		offset += NLA_ALIGN(nla->nla_len);
	}
}

static int isobusfs_extract_serr(struct isobusfs_err_msg *emsg)
{
	struct isobusfs_stats *stats = emsg->stats;
	struct sock_extended_err *serr = emsg->serr;
	struct scm_timestamping *tss = emsg->tss;

	switch (serr->ee_origin) {
	case SO_EE_ORIGIN_TIMESTAMPING:
		/*
		 * We expect here following patterns:
		 *   serr->ee_info == SCM_TSTAMP_ACK
		 *     Activated with SOF_TIMESTAMPING_TX_ACK
		 * or
		 *   serr->ee_info == SCM_TSTAMP_SCHED
		 *     Activated with SOF_TIMESTAMPING_SCHED
		 * and
		 *   serr->ee_data == tskey
		 *     session message counter which is activate
		 *     with SOF_TIMESTAMPING_OPT_ID
		 * the serr->ee_errno should be ENOMSG
		 */
		if (serr->ee_errno != ENOMSG)
			warnx("serr: expected ENOMSG, got: %i",
			      serr->ee_errno);

		if (serr->ee_info == SCM_TSTAMP_SCHED)
			stats->tskey_sch = serr->ee_data;
		else
			stats->tskey_ack = serr->ee_data;

		isobusfs_print_timestamp(emsg, isobusfs_tstype_to_str(serr->ee_info),
				     &tss->ts[0]);

		if (serr->ee_info == SCM_TSTAMP_SCHED)
			return -EINTR;
		else
			return 0;
	case SO_EE_ORIGIN_LOCAL:
		/*
		 * The serr->ee_origin == SO_EE_ORIGIN_LOCAL is
		 * currently used to notify about locally
		 * detected protocol/stack errors.
		 * Following patterns are expected:
		 *   serr->ee_info == J1939_EE_INFO_TX_ABORT
		 *     is used to notify about session TX
		 *     abort.
		 *   serr->ee_data == tskey
		 *     session message counter which is activate
		 *     with SOF_TIMESTAMPING_OPT_ID
		 *   serr->ee_errno == actual error reason
		 *     error reason is converted from J1939
		 *     abort to linux error name space.
		 */
		if (serr->ee_info != J1939_EE_INFO_TX_ABORT)
			warnx("serr: unknown ee_info: %i",
			      serr->ee_info);

		isobusfs_print_timestamp(emsg, "  ABT", &tss->ts[0]);
		warnx("serr: tx error: %i, %s", serr->ee_errno, strerror(serr->ee_errno));

		return serr->ee_errno;
	default:
		warnx("serr: wrong origin: %u", serr->ee_origin);
	}

	return 0;
}

static int isobusfs_parse_cm(struct isobusfs_err_msg *emsg,
			     struct cmsghdr *cm)
{
	const size_t hdr_len = CMSG_ALIGN(sizeof(struct cmsghdr));

	if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_TIMESTAMPING) {
		emsg->tss = (void *)CMSG_DATA(cm);
	} else if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_TIMESTAMPING_OPT_STATS) {
		void *jstats = (void *)CMSG_DATA(cm);

		/* Activated with SOF_TIMESTAMPING_OPT_STATS */
		isobusfs_scm_opt_stats(emsg, jstats, cm->cmsg_len - hdr_len);
	} else if (cm->cmsg_level == SOL_CAN_J1939 &&
		   cm->cmsg_type == SCM_J1939_ERRQUEUE) {
		emsg->serr = (void *)CMSG_DATA(cm);
	} else
		warnx("serr: not supported type: %d.%d",
		      cm->cmsg_level, cm->cmsg_type);

	return 0;
}

int isobusfs_recv_err(int sock, struct isobusfs_err_msg *emsg)
{
	char control[200];
	struct cmsghdr *cm;
	int ret;
	struct msghdr msg = {
		.msg_control = control,
		.msg_controllen = sizeof(control),
	};

	ret = recvmsg(sock, &msg, MSG_ERRQUEUE | MSG_DONTWAIT);
	if (ret == -1) {
		ret = -errno;
		pr_err("recvmsg error notification: %i (%s)", ret, strerror(ret));
		return ret;
	}

	if (msg.msg_flags & MSG_CTRUNC) {
		pr_err("recvmsg error notification: truncated");
		return -EINVAL;
	}

	emsg->serr = NULL;
	emsg->tss = NULL;

	for (cm = CMSG_FIRSTHDR(&msg); cm && cm->cmsg_len;
	     cm = CMSG_NXTHDR(&msg, cm)) {
		isobusfs_parse_cm(emsg, cm);
		if (emsg->serr && emsg->tss)
			return isobusfs_extract_serr(emsg);
	}

	return 0;
}

/* send NACK message. function should use src, dst and socket as parameters */
void isobusfs_send_nack(int sock, struct isobusfs_msg *msg)
{
	struct sockaddr_can addr = msg->peername;
	struct isobusfs_nack nack;
	int ret;

	nack.ctrl = ISOBUS_ACK_CTRL_NACK;
	nack.group_function = msg->buf[0];
	memset(&nack.reserved[0], 0xff, sizeof(nack.reserved));
	nack.address_nack = addr.can_addr.j1939.addr;
	memcpy(&nack.pgn_nack[0], &addr.can_addr.j1939.pgn,
		sizeof(nack.pgn_nack));

	addr.can_addr.j1939.pgn = ISOBUS_PGN_ACK;
	ret = sendto(sock, &nack, sizeof(nack), MSG_DONTWAIT,
			  (struct sockaddr *)&addr,
			   sizeof(addr));
	if (ret < 0) {
		ret = -errno;
		pr_warn("failed to send NACK: %i (%s)", ret, strerror(ret));
	}

	pr_debug("send NACK");
}

/* store data to a recursive buffer */
void isobufs_store_tx_data(struct isobusfs_buf_log *buffer, uint8_t *data)
{
	struct isobusfs_buf *entry = &buffer->entries[buffer->index];

	/* we assume :) that data is at least 8 bytes long */
	memcpy(entry->data, data, sizeof(entry->data));
	clock_gettime(CLOCK_REALTIME, &entry->ts);

	buffer->index = (buffer->index + 1) % ISOBUSFS_MAX_BUF_ENTRIES;
}

void isobusfs_dump_tx_data(const struct isobusfs_buf_log *buffer)
{
	uint i;

	for (i = 0; i < ISOBUSFS_MAX_BUF_ENTRIES; ++i) {
		const struct isobusfs_buf *entry = &buffer->entries[i];
		char data_str[ISOBUSFS_MIN_TRANSFER_LENGH * 3] = {0};
		uint j;

		for (j = 0; j < ISOBUSFS_MIN_TRANSFER_LENGH; ++j)
			snprintf(data_str + j * 3, 4, "%02X ", entry->data[j]);

		pr_debug("Entry %u: %s Timestamp: %ld.%09ld\n", i, data_str,
			 entry->ts.tv_sec, entry->ts.tv_nsec);
	}
}

/* wrapper for sendto() */
int isobusfs_sendto(int sock, const void *data, size_t len,
		    const struct sockaddr_can *addr,
		    struct isobusfs_buf_log *isobusfs_tx_buffer)
{
	int ret;

	/* store to tx buffer */
	isobufs_store_tx_data(isobusfs_tx_buffer, (uint8_t *)data);

	ret = sendto(sock, data, len, MSG_DONTWAIT,
		     (struct sockaddr *)addr, sizeof(*addr));
	if (ret == -1) {
		ret = -errno;
		pr_warn("failed to send data: %i (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

/* wrapper for send() */
int isobusfs_send(int sock, const void *data, size_t len,
		  struct isobusfs_buf_log *isobusfs_tx_buffer)
{
	int ret;

	/* store to tx buffer */
	isobufs_store_tx_data(isobusfs_tx_buffer, (uint8_t *)data);

	ret = send(sock, data, len, MSG_DONTWAIT);
	if (ret == -1) {
		ret = -errno;
		pr_warn("failed to send data: %i (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

/**
 * isobusfs_cmn_open_socket - Open a CAN J1939 socket
 *
 * This function opens a CAN J1939 socket and returns the file descriptor
 * on success. In case of an error, the function returns the negative
 * error code.
 */
int isobusfs_cmn_open_socket(void)
{
	int ret;

	/* Create a new CAN J1939 socket */
	ret = socket(PF_CAN, SOCK_DGRAM, CAN_J1939);
	if (ret < 0) {
		/* Get the error code and print an error message */
		ret = -errno;
		pr_err("socket(j1939): %d (%s)", ret, strerror(ret));
		return ret;
	}
	return ret;
}

/**
 * isobusfs_cmn_configure_socket_filter - Configure a J1939 socket filter
 * @sock: Socket file descriptor
 * @pgn: Parameter Group Number to filter
 *
 * This function configures a J1939 socket filter for the provided PGN.
 * It allows ISOBUS FS role-specific PGN and ACK messages for troubleshooting.
 * Returns 0 on success or a negative error code on failure.
 */
int isobusfs_cmn_configure_socket_filter(int sock, pgn_t pgn)
{
	struct j1939_filter sock_filter[2] = {0};
	int ret;

	if (pgn != ISOBUSFS_PGN_CL_TO_FS && pgn != ISOBUSFS_PGN_FS_TO_CL) {
		pr_err("invalid pgn: %d", pgn);
		return -EINVAL;
	}

	/* Allow ISOBUS FS role specific PGN */
	sock_filter[0].pgn = pgn;
	sock_filter[0].pgn_mask = J1939_PGN_PDU1_MAX;

	/*
	 * ISO 11783-3:2018 - 5.4.5 Acknowledgment.
	 * Allow ACK messages for troubleshooting
	 */
	sock_filter[1].pgn = ISOBUS_PGN_ACK;
	sock_filter[1].pgn_mask = J1939_PGN_PDU1_MAX;

	ret = setsockopt(sock, SOL_CAN_J1939, SO_J1939_FILTER, &sock_filter,
			 sizeof(sock_filter));
	if (ret < 0) {
		ret = -errno;
		pr_err("failed to set j1939 filter: %d (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

/**
 * isobusfs_cmn_configure_timestamping - Configure timestamping options for a
 *					 socket
 * @sock: Socket file descriptor
 *
 * This function configures various timestamping options for the given socket,
 * such as software timestamping, CMSG timestamping, transmission
 * acknowledgment, transmission scheduling, statistics, and timestamp-only
 * options. These options are needed to get a response from different kernel
 * j1939 stack layers about egress status, allowing the caller to know if the
 * ETP session has finished or if status messages have actually been sent.
 * These options make sense only in combination with SO_J1939_ERRQUEUE. Returns
 * 0 on success or a negative error code on failure.
 */
static int isobusfs_cmn_configure_timestamping(int sock)
{
		unsigned int sock_opt;
		int ret;

	sock_opt = SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_OPT_CMSG |
		   SOF_TIMESTAMPING_TX_ACK | SOF_TIMESTAMPING_TX_SCHED |
		   SOF_TIMESTAMPING_OPT_STATS | SOF_TIMESTAMPING_OPT_TSONLY |
		   SOF_TIMESTAMPING_OPT_ID;

	ret = setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING,
					 (char *)&sock_opt, sizeof(sock_opt));
	if (ret < 0) {
		ret = -errno;
		pr_err("setsockopt timestamping: %d (%s)", ret, strerror(ret));
		return ret;
	}
	return 0;
}

/**
 * isobusfs_cmn_configure_error_queue - Configure error queue for a J1939 socket
 * @sock: socket file descriptor
 *
 * This function configures the error queue for a given J1939 socket, enabling
 * timestamping options and the error queue itself. SO_J1939_ERRQUEUE enables
 * the actual feedback channel from the kernel J1939 stack. Timestamping options
 * are configured to subscribe the socket to different notifications over this
 * channel, providing information on egress status. This helps in determining if
 * an ETP session has finished or if status messages were actually sent.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int isobusfs_cmn_configure_error_queue(int sock)
{
	int err_queue = true;
	int ret;

	ret = setsockopt(sock, SOL_CAN_J1939, SO_J1939_ERRQUEUE,
			 &err_queue, sizeof(err_queue));
	if (ret < 0) {
		ret = -errno;
		pr_err("set recverr: %d (%s)", ret, strerror(ret));
		return ret;
	}

	ret = isobusfs_cmn_configure_timestamping(sock);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * isobusfs_cmn_bind_socket - Bind a J1939 socket to a given address
 * @sock: socket file descriptor
 * @addr: pointer to a sockaddr_can structure containing the address
 *         information to bind the socket to
 *
 * This function binds a J1939 socket to the specified address. It returns
 * 0 on successful binding or a negative error code on failure.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int isobusfs_cmn_bind_socket(int sock, struct sockaddr_can *addr)
{
	int ret;

	ret = bind(sock, (void *)addr, sizeof(*addr));
	if (ret < 0) {
		ret = -errno;
		pr_err("failed to bind: %d (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

int isobusfs_cmn_socket_prio(int sock, int prio)
{
	int ret;

	ret = setsockopt(sock, SOL_CAN_J1939, SO_J1939_SEND_PRIO,
			 &prio, sizeof(prio));
	if (ret < 0) {
		ret = -errno;
		pr_warn("Failed to set priority %i. Error %i (%s)", prio, ret,
			strerror(ret));
		return ret;
	}

	return 0;
}

int isobusfs_cmn_connect_socket(int sock, struct sockaddr_can *addr)
{
	int ret;

	ret = connect(sock, (void *)addr, sizeof(*addr));
	if (ret < 0) {
		ret = -errno;
		pr_err("failed to connect socket: %d (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

/**
 * isobusfs_cmn_set_broadcast - Enable broadcast option for a socket
 * @sock: socket file descriptor
 *
 * This function enables the SO_BROADCAST option for the given socket,
 * allowing it to send and receive broadcast messages. It returns 0 on success
 * or a negative error code on failure.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int isobusfs_cmn_set_broadcast(int sock)
{
	int broadcast = true;
	int ret;

	ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast,
			 sizeof(broadcast));
	if (ret < 0) {
		ret = -errno;
		pr_err("setsockopt(SO_BROADCAST): %d (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

/* FIXME: linger is currently not supported by the kernel J1939 stack
 * but it would be nice to have it. Especially if we wont to stop sending
 * messages on a socket when the connection is lost.
 */
int isobusfs_cmn_set_linger(int sock)
{
	struct linger linger_opt;
	int ret;

	linger_opt.l_onoff = 1;
	linger_opt.l_linger = 0;
	ret = setsockopt(sock, SOL_SOCKET, SO_LINGER, &linger_opt,
			 sizeof(linger_opt));
	if (ret < 0) {
		ret = -errno;
		pr_err("setsockopt(SO_LINGER): %d (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

int isobusfs_cmn_add_socket_to_epoll(int epoll_fd, int sock, uint32_t events)
{
	struct epoll_event ev = {0};
	int ret;

	ev.events = events;
	ev.data.fd = sock;

	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &ev);
	if (ret < 0) {
		ret = errno;
		pr_err("epoll_ctl(EPOLL_CTL_ADD): %d (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

int isobusfs_cmn_create_epoll(void)
{
	int ret, epoll_fd;

	epoll_fd = epoll_create1(0);
	if (epoll_fd < 0) {
		ret = -errno;
		pr_err("epoll_create1: %d (%s)", ret, strerror(ret));
		return ret;
	}

	return epoll_fd;
}

int isobusfs_cmn_prepare_for_events(struct isobusfs_cmn *cmn, int *nfds,
				    bool dont_wait)
{
	int ret, timeout_ms;

	if (dont_wait)
		timeout_ms = 0;
	else
		timeout_ms = isobusfs_get_timeout_ms(&cmn->next_send_time);

	ret = epoll_wait(cmn->epoll_fd, cmn->epoll_events,
			 cmn->epoll_events_size, timeout_ms);
	if (ret < 0) {
		ret = -errno;
		if (ret != -EINTR) {
			*nfds = 0;
			return ret;
		}
	}

	*nfds = ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &cmn->last_time);
	if (ret < 0) {
		ret = -errno;
		pr_err("failed to get time: %i (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

void isobusfs_cmn_dump_last_x_bytes(const uint8_t *buffer, size_t buffer_size,
				    size_t x)
{
	size_t start_offset = 0;
	char *output_ptr;
	unsigned char c;
	int remaining;
	char output[80];
	int n, j;

	if (x > 0 && x < buffer_size)
		start_offset = (buffer_size - x) & ~0x7;

	for (size_t i = start_offset; i < buffer_size; i += 8) {
		output_ptr = output;
		remaining = (int)sizeof(output);

		n = snprintf(output_ptr, remaining, "%08lx: ",
			     (unsigned long)(start_offset + i));
		if (n < 0 || n >= remaining)
			break;

		output_ptr += n;
		remaining -= n;

		for (j = 0; j < 8 && i + j < buffer_size; ++j) {
			n = snprintf(output_ptr, remaining, "%02x ", buffer[i+j]);
			if (n < 0 || n >= remaining)
				break;

			output_ptr += n;
			remaining -= n;
		}

		for (j = buffer_size - i; j < 8; ++j) {
			n = snprintf(output_ptr, remaining, "   ");
			if (n < 0 || n >= remaining)
				break;

			output_ptr += n;
			remaining -= n;
		}

		n = snprintf(output_ptr, remaining, "  ");
		if (n < 0 || n >= remaining)
			break;

		output_ptr += n;
		remaining -= n;

		for (j = 0; j < 8 && i + j < buffer_size; ++j) {
			c = buffer[i+j];
			n = snprintf(output_ptr, remaining, "%c",
				     isprint(c) ? c : '.');
			if (n < 0 || n >= remaining)
				break;

			output_ptr += n;
			remaining -= n;
		}

		pr_debug("%s", output);
		if (n < 0 || n >= remaining)
			break;
	}
}
