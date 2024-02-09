// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#ifndef _ISOBUSFS_CMN_FA_H
#define _ISOBUSFS_CMN_FA_H

#include <linux/kernel.h>

#include "isobusfs_cmn.h"


/* B.14 Flags */
/**
 * ISOBUSFS_FA_REPORT_HIDDEN: (version 4 and later)
 *	0 - Do not report hidden files and folders in directory listing.
 *	1 - Report hidden files and folders in directory listing.
 */
#define ISOBUSFS_FA_REPORT_HIDDEN	BIT(5)
/**
 * ISOBUSFS_FA_OPEN_EXCLUSIVE:
 *	0 - Open file for shared read access
 *	1 - Open file with exclusive access (fails if already open)
 */
#define ISOBUSFS_FA_OPEN_EXCLUSIVE	BIT(4)
/**
 * ISOBUSFS_FA_OPEN_APPEND
 *	0 - Open file for random access (file pointer set to the start of
 *	    the file)
 *	1 - Open file for appending data to the end of the file (file
 *	    pointer set to the end of the file).
 */
#define ISOBUSFS_FA_OPEN_APPEND		BIT(3)
/**
 * ISOBUSFS_FA_CREATE_FILE_DIR:
 *	0 - Open an existing file (fails if non-existent file)
 *	1 - Create a new file and/or directories if not yet existing
 */
#define ISOBUSFS_FA_CREATE_FILE_DIR	BIT(2)
#define ISOBUSFS_FA_OPEN_MASK		GENMASK(1, 0)
#define ISOBUSFS_FA_OPEN_FILE_RO	0
#define ISOBUSFS_FA_OPEN_FILE_WO	1
#define ISOBUSFS_FA_OPEN_FILE_WR	2
#define ISOBUSFS_FA_OPEN_DIR		3

/*
 * ISO 11783-13:2021 B.15 - File Attributes
 * Bit 7: Case Sensitivity
 *    0 - Volume is case-insensitive
 *    1 - Volume is case-sensitive (Version 3 and later FS support this attribute)
 * Bit 6: Removability
 *    0 - Volume is removable
 *    1 - Volume is not removable
 * Bit 5: Long Filename Support
 *    0 - Volume does not support long filenames
 *    1 - Volume supports long filenames
 * Bit 4: Directory Specification
 *    0 - Does not specify a directory
 *    1 - Specifies a directory
 * Bit 3: Volume Specification
 *    0 - Does not specify a volume
 *    1 - Specifies a volume
 * Bit 2: Hidden Attribute Support
 *    0 - Volume does not support hidden attribute
 *    1 - Volume supports hidden attribute and implementation supports it for the given volume
 * Bit 1: Hidden Attribute Setting
 *    0 - "Hidden" attribute is not set
 *    1 - "Hidden" attribute is set (not applicable unless volume supports hidden attribute)
 * Bit 0: Read-Only Attribute
 *    0 - "Read-only" attribute is not set
 *    1 - "Read-only" attribute is set
 */

#define ISOBUSFS_ATTR_CASE_SENSITIVE  BIT(7)
#define ISOBUSFS_ATTR_REMOVABLE       BIT(6)
#define ISOBUSFS_ATTR_LONG_FILENAME   BIT(5)
#define ISOBUSFS_ATTR_DIRECTORY       BIT(4)
#define ISOBUSFS_ATTR_VOLUME          BIT(3)
#define ISOBUSFS_ATTR_HIDDEN_SUPPORT  BIT(2)
#define ISOBUSFS_ATTR_HIDDEN          BIT(1)
#define ISOBUSFS_ATTR_READ_ONLY       BIT(0)

/**
 * C.3.3.2 Open File Request
 * struct isobusfs_fa_openf_req - Open File Request structure
 * @fs_function:     Function and command (1 byte)
 *                   Bits 7-4: 0b0010 (Command - File Access)
 *                   Bits 3-0: 0b0000 (Function - Read File) (see B.2)
 * @tan:             Transaction number (1 byte) (see B.8)
 * @flags:           Flags (1 byte) (see B.14)
 * name_len:         Path Name Length (2 bytes) (see B.12)
 * @name:            Name (Filename, Path or Wildcard name, depending on Flags)
 *                   (Variable length)
 *
 * Transmission repetition rate: On request
 * Data length: Variable
 * Parameter group number: Client to FS, destination-specific
 *
 * Open File Request message is used to request opening a file, directory or
 * using a wildcard name. The message contains the TAN, Flags, Path Name
 * Length, and Name.
 */
struct isobusfs_fa_openf_req {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t flags;
	__le16 name_len;
	uint8_t name[];
};

/**
 * C.3.3.3 Open File Response
 * struct isobusfs_fa_openf_res - Open File Response structure
 * @fs_function:   Function and command (1 byte)
 *                 Bits 7-4: 0b0010 (Command - File Access)    (see B.1)
 *                 Bits 3-0: 0b0000 (Function - Open File)   (see B.2)
 * @tan:           Transaction number (1 byte)
 * @error_code:    Error code (1 byte), possible values:
 *                 0: Success
 *                 1: Access denied
 *                 2: Invalid access
 *                 3: Too many files open
 *                 4: File, path or volume not found
 *                 6: Invalid given source name
 *                 8: Volume out of free space
 *                 10: Media is not present
 *                 13: Volume is possibly not initialized
 *                 43: Out of memory
 *                 44: Any other error
 * @handle:        File handle (1 byte)
 * @attributes:    File attributes (1 byte)
 * @reserved:      Reserved, transmit as 0xFF (3 bytes)
 *
 * Transmission repetition rate: In response to Open File Request message
 * Data length: 8 bytes
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_fa_openf_res {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	uint8_t handle;
	uint8_t attributes;
	uint8_t reserved[3];
};

/* B.17 Position mode */
/* From the beginning of the file */
#define ISOBUSFS_FA_SEEK_SET	0
/* From the current position in the file */
#define ISOBUSFS_FA_SEEK_CUR	1
/* From the end of the file (can only be negative or 0 value) */
#define ISOBUSFS_FA_SEEK_END	2

/**
 * C.3.4.2 Seek File Request
 * struct isobusfs_fa_seekf_req - Seek File Request structure
 * @fs_function:     Function and command (1 byte)
 *                   Bits 7-4: 0b0010 (Command - File Access) (see B.1)
 *                   Bits 3-0: 0b0001 (Function - Seek File) (see B.2)
 * @tan:             Transaction number (1 byte) (see B.8)
 * @handle:          Handle (1 byte) (see B.10)
 * @position_mode:   Position mode (1 byte) (see B.17)
 * @offset:          Offset (4 bytes) (see B.18)
 */
struct isobusfs_fa_seekf_req {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t handle;
	uint8_t position_mode;
	__le32 offset;
};

/**
 * C.3.4.2 Seek File Response
 * struct isobusfs_fa_seekf_res - Seek File Response structure
 * @fs_function:     Function and command (1 byte)
 *                   Bits 7-4: 0b0010 (Command - File Access) (see B.1)
 *                   Bits 3-0: 0b0001 (Function - Seek File) (see B.2)
 * @tan:             Transaction number (1 byte) (see B.8)
 * @error_code:      Error code (1 byte) (see B.9)
 *                   0: Success
 *                   1: Access denied
 *                   5: Invalid Handle
 *                   11: Failure during a read operation
 *                   42: Invalid request length
 *                   43: Out of memory
 *                   44: Any other error
 *                   45: File pointer at end of file
 * @reserved:        Reserved, transmit as 0xFF (1 byte)
 * @position:        Position (4 bytes) (see B.19)
 */
struct isobusfs_fa_seekf_res {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	uint8_t reserved;
	__le32 position;
};

/**
 * C.3.5.2 Read File Request
 * struct isobusfs_fa_readf_req - Read File Request structure
 * @fs_function:   Function and command (1 byte)
 *                 Bits 7-4: 0b0010 (Command: File Access, see B.1)
 *                 Bits 3-0: 0b0010 (Function: Read File, see B.2)
 * @tan:           Transaction number (1 byte) (see B.8)
 * @handle:        File handle (1 byte) (see B.10)
 * @count:         Count (2 bytes) (see B.20)
 * @reserved:      Reserved, transmit as 0xFF (3 bytes)
 *                 Byte 6: Version 4 and later: Reserved
 *                         Version 3 and prior: Report Hidden Files (see B.28)
 *
 * Transmission repetition rate: On request
 * Data length: 8 bytes
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_fa_readf_req {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t handle;
	__le16 count;
	uint8_t reserved[3];
};

/**
 * C.3.5.3 Read File Response (Handle-referenced file)
 * struct isobusfs_read_file_response - Read File Response structure
 * @fs_function:   Function and command (1 byte)
 *                 Bits 7-4: 0b0010 (Command: File Access, see B.1)
 *                 Bits 3-0: 0b0010 (Function: Read File, see B.2)
 * @tan:           Transaction number (1 byte) (see B.8)
 * @error_code:    Error code (1 byte) (see B.9)
 *                 0: Success
 *                 1: Access denied
 *                 5: Invalid Handle
 *                11: Failure during a read operation
 *                43: Out of memory
 *                44: Any other error
 *                45: File pointer at end of file
 * @count:         Count (2 bytes) (see B.20)
 * @data:          Data (variable length)
 *
 * Transmission repetition rate: In response to Read File Request message
 * Data length: Variable
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_read_file_response {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	__le16 count;
	uint8_t data[];
};

/**
 * C.3.5.4 Read Directory Response (Handle-referenced directory)
 * struct isobusfs_read_dir_response - Read Directory Response structure
 * @fs_function:   Function and command (1 byte)
 *                 Bits 7-4: 0b0010 (Command: File Access, see B.1)
 *                 Bits 3-0: 0b0010 (Function: Read File, see B.2)
 * @tan:           Transaction number (1 byte) (see B.8)
 * @error_code:    Error code (1 byte) (see B.9)
 *                 0: Success
 *                 1: Access denied
 *                 5: Invalid Handle
 *                11: Failure during a read operation
 *                43: Out of memory
 *                44: Any other error
 *                45: File pointer at end of file
 * @count:         Count (2 bytes) (see B.20)
 * @data:          Data (variable length) (see B.21)
 *
 * Transmission repetition rate: In response to Read File Request message
 * Data length: Variable
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_read_dir_response {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	__le16 count;
	uint8_t data[];
};

/**
 * C.3.6.2 Write File Request
 * struct isobusfs_write_file_request - Write File Request structure
 * @fs_function:   Function and command (1 byte)
 *                 Bits 7-4: 0b0010 (Command: File Access, see B.1)
 *                 Bits 3-0: 0b0011 (Function: Write File, see B.2)
 * @tan:           Transaction number (1 byte) (see B.8)
 * @handle:        Handle (1 byte) (see B.10)
 * @count:         Count (2 bytes) (see B.20)
 * @data:          Data (variable length)
 *
 * Transmission repetition rate: On request
 * Data length: Variable
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_write_file_request {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t handle;
	__le16 count;
	uint8_t data[];
};

/**
 * C.3.6.3 Write File Response
 * struct isobusfs_write_file_response - Write File Response structure
 * @fs_function:   Function and command (1 byte)
 *                 Bits 7-4: 0b0010 (Command: File Access, see B.1)
 *                 Bits 3-0: 0b0011 (Function: Write File, see B.2)
 * @tan:           Transaction number (1 byte) (see B.8)
 * @error_code:    Error code (1 byte) (see B.9)
 *                 0: Success
 *                 1: Access denied
 *                 5: Invalid Handle
 *                 8: Volume out of space
 *                 9: Failure during a write operation
 *                43: Out of memory
 *                44: Any other error
 * @count:         Count (2 bytes) (see B.20)
 * @reserved:      Reserved, transmit as 0xFF (3 bytes)
 *
 * Transmission repetition rate: In response to Write File Request message
 * Data length: 8 bytes
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_write_file_response {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	__le16 count;
	uint8_t reserved[3];
};

/**
 * C.3.7.1 Close File Request
 * struct isobusfs_close_file_request - Close File Request structure
 * @fs_function:   Function and command (1 byte)
 *                 Bits 7-4: 0b0010 (Command: File Access, see B.1)
 *                 Bits 3-0: 0b0100 (Function: Close File, see B.2)
 * @tan:           Transaction number (1 byte) (see B.8)
 * @handle:        Handle (1 byte) (see B.10)
 * @reserved:      Reserved, transmit as 0xFF (5 bytes)
 *
 * Transmission repetition rate: On request
 * Data length: 8 bytes
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_close_file_request {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t handle;
	uint8_t reserved[5];
};

/**
 * C.3.7.2 Close File Response
 * struct isobusfs_close_file_response - Close File Response structure
 * @fs_function:   Function and command (1 byte)
 *                 Bits 7-4: 0b0010 (Command: File Access, see B.1)
 *                 Bits 3-0: 0b0100 (Function: Close File, see B.2)
 * @tan:           Transaction number (1 byte) (see B.8)
 * @error_code:    Error code (1 byte) (see B.9)
 *                 0: Success
 *                 1: Access denied
 *                 5: Invalid Handle
 *                 8: Volume out of space
 *                 9: Failure during a write operation
 *                 43: Out of memory
 *                 44: Any other error
 * @reserved:      Reserved, transmit as 0xFF (6 bytes)
 */
struct isobusfs_close_file_res {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	uint8_t reserved[6];
};


#endif /* _LINUX_ISOBUS_FS_H */
