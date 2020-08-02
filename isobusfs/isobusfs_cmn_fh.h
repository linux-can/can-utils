#ifndef _ISOBUSFS_CMN_FH_H
#define _ISOBUSFS_CMN_FH_H

#include "isobusfs_cmn.h"

/**
 * C.4.2.2 Move File Request
 * struct isobusfs_move_file_request - Move File Request structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0011 (Command - File Handling, see B.1)
 *               Bits 3-0: 0b0000 (Function - Move File, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @file_handling_mode: File Handling Mode (1 byte) (see B.27)
 * @src_path_name_length: Source Path Name Length (2 bytes) (see B.12)
 * @dst_path_name_length: Destination Path Name Length (2 bytes) (see B.12)
 * @src_path: Source Volume, Path, File and Wildcard Name (variable) (see B.34)
 * @dst_path: Destination Volume, Path, File and Wildcard Name (variable) (see B.34)
 *
 * Transmission repetition rate: On request
 * Data length: Variable
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_move_file_request {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t file_handling_mode;
	__le16 src_path_name_length;
	__le16 dst_path_name_length;
	/* Variable length data follows */
	/* uint8_t src_path[]; */
	/* uint8_t dst_path[]; */
};

/**
 * C.4.2.3 Move File Response
 * struct isobusfs_move_file_response - Move File Response structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0011 (Command - File Handling, see B.1)
 *               Bits 3-0: 0b0000 (Function - Move File, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @error_code:  Error code (1 byte) (see B.9)
 *               0: Success
 *               1: Access denied
 *               2: Invalid access
 *               4: File, path or volume not found
 *               6: Invalid given source name
 *               7: Invalid given destination name
 *               8: Volume out of free space
 *               9: Failure during read operation
 *               13: Volume is possibly not initialized
 *               43: Out of memory
 *               44: Any other error
 * @reserved:    Reserved, transmit as 0xFF (5 bytes)
 *
 * Transmission repetition rate: In response to Move File Request message
 * Data length: 8 bytes
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_move_file_response {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	uint8_t reserved[5];
};

/**
 * C.4.3.1 Delete File Request
 * struct isobusfs_delete_file_request - Delete File Request structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0011 (Command - File Handling, see B.1)
 *               Bits 3-0: 0b0001 (Function - Delete File, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @mode:        File Handling Mode (1 byte) (see B.27)
 * @path_len:    Path Name Length (2 bytes) (see B.12)
 * @path:        Volume, Path, File, and Wildcard Name (variable length) (see B.34)
 *
 * Transmission repetition rate: On request
 * Data length: Variable
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_delete_file_request {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t mode;
	__le16 path_len;
	uint8_t path[];
};


/**
 * C.4.3.2 Delete File Response
 * struct isobusfs_delete_file_response - Delete File Response structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0011 (Command - File Handling, see B.1)
 *               Bits 3-0: 0b0001 (Function - Delete File, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @error_code:  Error code (1 byte) (see B.9)
 *               0: Success
 *               1: Access denied
 *               2: Invalid access
 *               4: File, path, or volume not found
 *               6: Invalid given file name
 *               9: Failure during a write operation
 *               13: Volume is possibly not initialized
 *               43: Out of memory
 *               44: Any other error
 * @reserved:    Reserved, transmit as 0xFF (5 bytes)
 *
 * Transmission repetition rate: In response to Delete File Request message
 * Data length: 8 bytes
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_delete_file_response {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	uint8_t reserved[5];
};

/**
 * C.4.4.2 Get File Attributes Request
 * struct isobusfs_get_file_attributes_request - Get File Attributes Request structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0011 (Command - File Handling, see B.1)
 *               Bits 3-0: 0b0010 (Function - Get File Attributes, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @pathname_len: Pathname Length (2 bytes) (__le16, see B.12)
 * @pathname:    Volume, Path and Filename (variable length) (see B.35)
 *
 * Transmission repetition rate: On request
 * Data length: Variable
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_get_file_attributes_request {
	uint8_t fs_function;
	uint8_t tan;
	__le16 pathname_len;
	uint8_t pathname[];
};

/**
 * C.4.4.3 Get File Attributes Response
 * struct isobusfs_get_file_attributes_response - Get File Attributes Response structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0011 (Command - File Handling, see B.1)
 *               Bits 3-0: 0b0010 (Function - Get File Attributes, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @error_code:  Error code (1 byte) (see B.9)
 *               0: Success
 *               1: Access denied
 *               2: Invalid access
 *               4: File, path or volume not found
 *               6: Invalid given name
 *               11: Failure during a read operation
 *               13: Volume is possibly not initialized
 *               43: Out of memory
 *               44: Any other error
 * @attributes:  File attributes (1 byte) (see B.15)
 * @file_size:   File size (4 bytes) (see B.26)
 *
 * Transmission repetition rate: In response to Get File Attributes Request message
 * Data length: Variable
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_get_file_attributes_response {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	uint8_t attributes;
	__le32 file_size;
};

/**
 * C.4.5.2 Set File Attributes Request
 * struct isobusfs_set_file_attributes_request - Set File Attributes Request structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0011 (Command - File Handling, see B.1)
 *               Bits 3-0: 0b0011 (Function - Set File Attributes, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @attributes:  Set Attributes Command (1 byte) (see B.16)
 * @name_length: Name length (__le16) (see B.12)
 * @name:        Path, File and Wildcard Name (n bytes) (see B.34)
 *
 * Transmission repetition rate: On request
 * Data length: Variable
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_set_file_attributes_request {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t attributes;
	__le16 name_length;
	uint8_t name[]; /* Variable length */
};

/**
 * C.4.5.3 Set File Attributes Response
 * struct isobusfs_set_file_attributes_response - Set File Attributes Response structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0011 (Command - File Handling, see B.1)
 *               Bits 3-0: 0b0011 (Function - Set File Attributes, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @error_code:  Error code (1 byte) (see B.9)
 *               0: Success
 *               1: Access denied
 *               4: File, path or volume not found
 *               6: Invalid given name
 *               8: Volume out of free space
 *               9: Failure during a write operation
 *               10: Media is not present
 *               13: Volume is possibly not initialized
 *               43: Out of memory
 *               44: Any other error
 * @reserved:    Reserved, transmit as 0xFF (5 bytes)
 *
 * Transmission repetition rate: In response to Get File Attributes Request message
 * Data length: Variable
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_set_file_attributes_response {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	uint8_t reserved[5];
};

/**
 * C.4.6.2 Get File Date & Time Request
 * struct isobusfs_get_file_date_time_request - Get File Date & Time Request structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0011 (Command - File Handling, see B.1)
 *               Bits 3-0: 0b0100 (Function - Get File Date & Time, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @name_length: Path Name length (2 bytes) (see B.12)
 * @name:        Path, File and Name (n bytes) (see B.35)
 *
 * Transmission repetition rate: On request
 * Data length: Variable
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_get_file_date_time_request {
	uint8_t fs_function;
	uint8_t tan;
	__le16 name_length;
	uint8_t name[]; /* Variable length */
};

/**
 * C.4.6.2 Get File Date & Time Response
 * struct isobusfs_get_file_date_time_response - Get File Date & Time Response structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0011 (Command - File Handling, see B.1)
 *               Bits 3-0: 0b0100 (Function - Get File Date & Time, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @error_code:  Error code (1 byte) (see B.9)
 *               0: Success
 *               1: Access denied
 *               4: File, path, or volume not found
 *               6: Invalid given name
 *               10: Media is not present
 *               11: Failure during read operation
 *               13: Volume is possibly not initialized
 *               43: Out of memory
 *               44: Any other error
 * @date:        File date (2 bytes) (see B.24)
 * @time:        File time (2 bytes) (see B.25)
 *
 * Transmission repetition rate: In response to Get File Date & Time Request message
 * Data length: 7 bytes
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_get_file_date_time_response {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	__le16 date;
	__le16 time;
};

#endif /* _LINUX_ISOBUS_FS_H */
