// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#ifndef _ISOBUSFS_CMN_VA_H
#define _ISOBUSFS_CMN_VA_H

#include "isobusfs_cmn.h"

/**
 * C.5.2.2 Initialize Volume Request
 * struct isobusfs_va_init_vol_req - Initialize Volume Request structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0100 (Command - Volume Access, see B.1)
 *               Bits 3-0: 0b0000 (Function - Initialize Volume, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @space:       Space (2 bytes) (see B.11)
 * @volume_flags: Volume Flags (1 byte) (see B.29)
 * @name_len:    Pathname Length (2 bytes) (__le16, see B.12)
 * @name:        Volume, Path and Filename (variable length) (see B.35)
 *
 * Transmission repetition rate: On request
 * Data length: Variable
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_va_init_vol_req {
	uint8_t fs_function;
	uint8_t tan;
	__le16 space;
	uint8_t volume_flags;
	__le16 name_len;
	uint8_t name[];
};

/**
 * C.5.2.3 Initialize Volume Response
 * struct isobusfs_va_init_vol_res - Initialize Volume Response structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0100 (Command - Volume Access, see B.1)
 *               Bits 3-0: 0b0000 (Function - Initialize Volume, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @error_code:  Error code (1 byte) (see B.9)
 *               0: Success
 *               1: Access denied
 *               4: Volume, path or file not found
 *               6: Invalid given source name
 *               8: Volume out of free space
 *               9: Failure during write operation
 *               10: Media is not present
 *               11 Failure during read operation
 *               12: Function not supported
 *               13: Volume is possibly not initialized
 *               43: Out of memory
 *               44: Any other error
 * @reserved:    Reserved, transmit as 0xFF (4 bytes)
 *
 * Transmission repetition rate: In response to Initialize Volume Request message
 * Data length: 8 bytes
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_va_init_vol_res {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	uint8_t reserved[4];
};

#endif /* _ISOBUSFS_CMN_VA_H */
