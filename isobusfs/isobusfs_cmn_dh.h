#ifndef _ISOBUSFS_CMN_DH_H
#define _ISOBUSFS_CMN_DH_H

#include "isobusfs_cmn.h"

/**
 * C.2.2.2 Get Current Directory Request
 * struct isobusfs_dh_get_cd_req - Get Current Directory Request structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0001 (Command - Directory Access, see B.1)
 *               Bits 3-0: 0b0000 (Function - Get Current Directory, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @reserved:    Reserved, transmit as 0xFF (6 bytes)
 *
 * Transmission repetition rate: On request
 * Data length: 8 bytes
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_dh_get_cd_req {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t reserved[6];
};

/**
 * C.2.2.3 Get Current Directory Response
 * struct isobusfs_dh_get_cd_res - Get Current Directory Response structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0001 (Command - Directory Access, see B.1)
 *               Bits 3-0: 0b0000 (Function - Get Current Directory, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @error_code:  Error code (1 byte) (see B.9)
 * @total_space: Total space (4 bytes) (in units of 512 bytes, see B.11)
 * @free_space:  Free space (4 bytes) (in units of 512 bytes, see B.11)
 * @name_len:    Path Name Length (2 bytes) (__le16, see B.12)
 * @name:        Path Name (variable length) (see B.13)
 *
 * Transmission repetition rate: In response to Get Current Directory Request message
 * Data length: Variable
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_dh_get_cd_res {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	__le32 total_space;
	__le32 free_space;
	__le16 name_len;
	uint8_t name[];
};

/**
 * C.2.3.2 Change Current Directory Request
 * struct isobusfs_dh_ccd_req - Change Current Directory Request structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0001 (Command - Directory Access, see B.1)
 *               Bits 3-0: 0b0001 (Function - Change Current Directory, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @name_len:    Path Name length (2 bytes) (__le16, see B.12)
 * @name:        Path Name (variable length) (see B.13)
 *
 * Transmission repetition rate: On request
 * Data length: Variable
 * Parameter group number: Client to FS, destination-specific
 */
struct isobusfs_dh_ccd_req {
	uint8_t fs_function;
	uint8_t tan;
	__le16 name_len;
	uint8_t name[];
};

/**
 * C.2.3.3 Change Current Directory Response
 * struct isobusfs_dh_ccd_res - Change Current Directory Response structure
 * @fs_function: Function and command (1 byte)
 *               Bits 7-4: 0b0001 (Command - Directory Access, see B.1)
 *               Bits 3-0: 0b0001 (Function - Change Current Directory, see B.2)
 * @tan:         Transaction number (1 byte) (see B.8)
 * @error_code:  Error code (1 byte) (see B.9)
 *               0: Success
 *               1: Access denied
 *               2: Invalid access
 *               4: File, path or volume not found
 *               7: Invalid destination name given
 *               10: Media is not present
 *               13: Volume is possibly not initialized
 *               43: Out of memory
 *               44: Any other error
 * @reserved:    Reserved, transmit as 0xFF (5 bytes)
 *
 * Transmission repetition rate: In response to Change Current Directory Request message
 * Data length: 8 bytes
 * Parameter group number: FS to client, destination-specific
 */
struct isobusfs_dh_ccd_res {
	uint8_t fs_function;
	uint8_t tan;
	uint8_t error_code;
	uint8_t reserved[5];
};


#endif /* _ISOBUSFS_CMN_DH_H */
