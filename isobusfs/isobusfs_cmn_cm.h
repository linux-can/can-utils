// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#ifndef ISOBUSFS_CMN_CM_H
#define ISOBUSFS_CMN_CM_H

#include "isobusfs_cmn.h"

/* ISOBUSFS_CM_F_FS_STATUS */
#define ISOBUSFS_CM_F_FS_STATUS_IDLE_RATE	2000 /* ms */
#define ISOBUSFS_CM_F_FS_STATUS_BUSY_RATE	200 /* ms */
#define ISOBUSFS_CM_F_FS_STATUS_RATE_JITTER	5 /* ms */
/* File Server Status */
#define ISOBUSFS_FS_SATUS_BUSY_WRITING		BIT(1)
#define ISOBUSFS_FS_SATUS_BUSY_READING		BIT(0)

/**
 * C.1.2 File Server Status
 * struct isobusfs_cm_fss - File Server Status structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0000 (Command - Connection Management, see B.1)
 *               Bits 3-0: 0b0001 (Function - File Server Status, see B.2)
 * @file_server_status: File Server Status (1 byte) (see B.3)
 *                      Bits 7-2: 000000 (Reserved, send as 000000)
 *                      Bit 1: 1 (Busy writing)
 *                      Bit 0: 1 (Busy reading)
 * @num_open_files: Number of open files (1 byte)
 * @reserved: Reserved for future use (5 bytes)
 *
 * Transmission repetition rate: 2 000 ms when the status is not busy, 200 ms
 *                               when the status is busy reading or writing and,
 *                               on change of byte 2, up to five messages per
 *                               second.
 * Data length: 8 bytes
 * Parameter group number: FS to client, destination-specific or use global address: 0xFF
 */
struct isobusfs_cm_fss {
	uint8_t fs_function;
	uint8_t status;
	uint8_t num_open_files;
	uint8_t reserved[5];
};

/**
 * C.1.3 Client Connection Maintenance
 * struct isobusfs_cm_ccm - Client Connection Maintenance structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0000 (Command - Connection Management, see B.1)
 *               Bits 3-0: 0b0000 (Function - Client Connection Maintenance, see B.2)
 * @version: Version number (1 byte) (see B.5)
 * @reserved: Reserved for future use (6 bytes)
 *
 * Transmission repetition rate: 2000 ms
 * Data length: 8 bytes
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_cm_ccm {
	uint8_t fs_function;
	uint8_t version;
	uint8_t reserved[6];
};

/**
 * C.1.4 Get File Server Properties
 * struct isobusfs_cm_get_fs_props_req - Get File Server Properties structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0000 (Command - Connection Management, see B.1)
 *               Bits 3-0: 0b0001 (Function - Get File Server Properties, see B.2)
 * @reserved: Reserved, transmit as 0xFF (7 bytes)
 *
 * Transmission repetition rate: On request
 * Data length: 8 bytes
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_cm_get_fs_props_req {
	uint8_t fs_function;
	uint8_t reserved[7];
};

/* File Server Capabilities */
/* server support removable volumes */
#define ISOBUSFS_SRV_CAP_REMOVABLE_VOL	BIT(1)
/* server support multiple volumes */
#define ISOBUSFS_SRV_CAP_MULTI_VOL	BIT(0)

/**
 * C.1.5 Get File Server Properties Response
 * struct isobusfs_get_fs_props_resp - Get File Server Properties Response
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0000 (Command - Connection Management, see B.1)
 *               Bits 3-0: 0b0001 (Function - Get File Server Properties, see B.2)
 * @version_number: Version Number (1 byte, see B.5)
 * @max_open_files: Maximum Number of Simultaneously Open Files (1 byte, see B.6)
 * @fs_capabilities: File Server Capabilities (1 byte, see B.7)
 * @reserved: Reserved, transmit as 0xFF (4 bytes)
 *
 * Transmission repetition rate: In response to Get File Server Properties message
 * Data length: 8 bytes
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_cm_get_fs_props_resp {
	uint8_t fs_function;
	uint8_t version_number;
	uint8_t max_open_files;
	uint8_t fs_capabilities;
	uint8_t reserved[4];
};

#define ISOBUSFS_VOL_MODE_PREP_TO_REMOVE	BIT(1)
#define ISOBUSFS_VOL_MODE_USED_BY_CLIENT	BIT(0)
#define ISOBUSFS_VOL_MODE_NOT_USED		0

/**
 * C.1.6 Volume Status Request
 * struct isobusfs_cm_vol_stat_req - Volume Status Request structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0000 (Command - Connection Management, see B.1)
 *               Bits 3-0: 0b0010 (Function - Removable Media Status, see B.2)
 * @volume_mode: Volume Mode (1 byte) (see B.30)
 * @name_len:    Path Name Length (2 bytes) (__le16, see B.12)
 * @name:        Volume Name (variable length) (see B.34)
 *
 * Transmission repetition rate: Upon request
 * Data length: Variable
 * Parameter group number: Client to FS, specific to the destination
 */

struct isobusfs_cm_vol_stat_req {
	uint8_t fs_function;
	uint8_t volume_mode;
	__le16 name_len;
	char name[ISOBUSFS_MAX_VOLUME_NAME_LENGTH];
};


enum isobusfs_vol_status {
	ISOBUSFS_VOL_STATUS_PRESENT = 0,
	ISOBUSFS_VOL_STATUS_IN_USE = 1,
	ISOBUSFS_VOL_STATUS_PREP_TO_REMOVE = 2,
	ISOBUSFS_VOL_STATUS_REMOVED = 3,
};
/**
 * C.1.7 Volume Status Response
 * struct isobusfs_cm_vol_stat_res - Volume Status Response structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0000 (Command - Connection Management, see B.1)
 *               Bits 3-0: 0b0010 (Function - Volume Status, see B.2)
 * @volume_status: Volume Status (1 byte) (see B.31)
 * @max_time_before_removal: Maximum Time Before Volume Removal (1 byte) (see B.32)
 * @error_code: Error code (1 byte) (see B.9)
 *              0: Success
 *              1: Access denied
 *              2: Invalid Access
 *              4: File, path or volume not found
 *              6: Invalid given source name
 *              43: Out of memory
 *              44: Any other error
 * @name_len:   Path Name Length (2 bytes) (__le16, see B.12)
 * @name:       Volume Name (variable length) (see B.34)
 *
 * Transmission repetition rate: On request and on change of Volume Status
 * Data length: Variable
 * Parameter group number: FS to client, destination-specific or use global address: FF 16
 */
struct isobusfs_cm_vol_stat_res {
	uint8_t fs_function;
	uint8_t volume_status;
	uint8_t max_time_before_removal;
	uint8_t error_code;
	__le16 name_len;
	char name[ISOBUSFS_MAX_VOLUME_NAME_LENGTH];
};


#endif /* ISOBUSFS_CMN_CM_H */
