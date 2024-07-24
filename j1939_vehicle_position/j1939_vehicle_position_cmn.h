// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2024 Oleksij Rempel <linux@rempel-privat.de>

#ifndef _J1939_VEHICLE_POSITION_H_
#define _J1939_VEHICLE_POSITION_H_

#include <stdint.h>
#include <endian.h>
#include <stdbool.h>
#include <sys/epoll.h>

#include <linux/can.h>
#include <linux/kernel.h>
#include "../libj1939.h"
#include "../lib.h"

#define J1939_PGN_REQUEST_PGN				0x0ea00 /* 59904 */

/* ISO 11783-3:2018 - 5.4.5 Acknowledgment */
#define ISOBUS_PGN_ACK					0x0e800 /* 59392 */

#define J1939_MAX_TRANSFER_LENGH			1024

struct j1939_vp_stats {
	int err;
	uint32_t tskey_sch;
	uint32_t tskey_ack;
	uint32_t send;
};

struct j1939_vp_msg {
	uint8_t buf[J1939_MAX_TRANSFER_LENGH];
	size_t buf_size;
	size_t len; /* length of received message */
	struct sockaddr_can peername;
	socklen_t peer_addr_len;
	int sock;
};

struct j1939_vp_err_msg {
	struct sock_extended_err *serr;
	struct scm_timestamping *tss;
	struct j1939_vp_stats *stats;
};

/* SAE J1939 specific definitions */

/* SAE J1939-71:2002 - 5.3 pgn65267 - Vehicle Position 1 - VP1 - */
#define J1939_PGN_VP1					0x0fef3 /* 65267 */

#define J1939_VP1_PRIO_DEFAULT				6
#define J1939_VP1_MAX_TRANSFER_LENGH \
	sizeof(struct j1939_vp1_packet)
#define J1939_VP1_REPETITION_RATE_MS			5000
#define J1939_VP1_JITTER_MS				500

/**
 * struct j1939_vp1_packet - Represents the PGN 65267 Vehicle
 *                                        Position packet
 *
 * @latitude: Raw latitude position of the vehicle
 *           - SPN: 584
 *           - Data Length: 4 bytes
 *           - Resolution: 10^-7 deg/bit
 *           - Offset: -210 degrees
 *           - Range: -210 to +211.1008122 degrees
 *           - Operating Range: -210 degrees (SOUTH) to +211.108122 degrees
 *             (NORTH)
 *
 * @longitude: Raw longitude position of the vehicle
 *           - SPN: 585
 *           - Data Length: 4 bytes
 *           - Resolution: 10^-7 deg/bit
 *           - Offset: -210 degrees
 *           - Range: -210 to +211.1008122 degrees
 *           - Operating Range: -210 degrees (WEST) to +211.108122 degrees
 *             (EAST)
 *
 * This structure defines each component of the Vehicle Position as described in
 * PGN 65267.
 */
struct j1939_vp1_packet {
	__le32 latitude;  /* SPN 584 */
	__le32 longitude; /* SPN 585 */
} __attribute__((__packed__));

/* SAE J1939xxxxxxxx - xxx pgn64502 - Vehicle Position 2 - VP2 - */
#define J1939_PGN_VP2					0x0fbf6 /* 64502 */

#define J1939_VP2_PRIO_DEFAULT				6
#define J1939_VP2_MAX_TRANSFER_LENGH \
	sizeof(struct j1939_vp2_packet)
#define J1939_VP2_REPETITION_RATE_MS			5000
#define J1939_VP2_JITTER_MS				500

/**
 * struct j1939_vp2_packet - Represents the PGN 64502 Vehicle
 *                                         Position 2 packet
 * FIXME: current packet layout is guessed based on limited information:
 * https://www.isobus.net/isobus/pGNAndSPN/10801?type=PGN
 *
 * @total_satellites: Total number of satellites in view
 *           - SPN: 8128
 *           - Data Length: 1 byte
 *
 * @hdop: Horizontal dilution of precision
 *           - SPN: 8129
 *           - Data Length: 1 byte
 *           - Resolution: 0.1
 *
 * @vdop: Vertical dilution of precision
 *           - SPN: 8130
 *           - Data Length: 1 byte
 *           - Resolution: 0.1
 *
 * @pdop: Position dilution of precision
 *           - SPN: 8131
 *           - Data Length: 1 byte
 *           - Resolution: 0.1
 *
 * @tdop: Time dilution of precision
 *           - SPN: 8132
 *           - Data Length: 1 byte
 *           - Resolution: 0.1
 *
 * This structure defines each component of the Vehicle Position 2 as described
 * in PGN 64502.
 */
struct j1939_vp2_packet {
	uint8_t total_satellites; /* SPN 8128 */
	uint8_t hdop;			  /* SPN 8129 */
	uint8_t vdop;			  /* SPN 8130 */
	uint8_t pdop;			  /* SPN 8131 */
	uint8_t tdop;			  /* SPN 8132 */
} __attribute__((__packed__));

/* NMEA 2000 specific definitions */

/* NMEA 2000 - PGN 126992 - System Time */
#define NMEA2000_PGN_SYS_TIME				0x1F010 /* 126992 */

#define NMEA2000_SYS_TIME_PRIO_DEFAULT			6
#define NMEA2000_SYS_TIME_MAX_TRANSFER_LENGTH \
		sizeof(struct nmea2000_sys_time_packet)
#define NMEA2000_SYS_TIME_REPETITION_RATE_MS		1000
#define NMEA2000_SYS_TIME_JITTER_MS			100

/* Bit masks for the source_and_reserved field */
#define NMEA2000_SYS_TIME_SOURCE_GPS			0
#define NMEA2000_SYS_TIME_SOURCE_GLONASS		1
#define NMEA2000_SYS_TIME_SOURCE_RADIO_STATION		2
#define NMEA2000_SYS_TIME_SOURCE_LOCAL_CESIUM		3
#define NMEA2000_SYS_TIME_SOURCE_LOCAL_RUBIDIUM		4
#define NMEA2000_SYS_TIME_SOURCE_LOCAL_CRYSTAL		5

/**
 * struct nmea2000_sys_time_packet - Represents the PGN 126992 System Time packet
 *
 * @sid: Sequence identifier for correlating related PGNs.
 * @source: Source of the time information. Possible values are:
 *	    0 = GPS, 1 = GLONASS, 2 = Radio Station, 3 = Local Cesium clock,
 *	    4 = Local Rubidium clock, 5 = Local Crystal clock).
 * @reserved: Reserved field, set to 0xF.
 * @date: UTC Date in days since January 1, 1970.
 * @time: UTC Time in 0.0001 seconds since midnight.
 */
struct nmea2000_sys_time_packet {
	uint8_t sid; /* Sequence identifier */
	uint8_t source : 4,
		reserved : 4;
	__le16 date; /* Date in days since January 1, 1970 */
	__le32 time; /* Time in 0.0001 seconds since midnight */
};

/* NMEA 2000 - PGN 127258 - Magnetic Variation */
#define NMEA2000_PGN_MAG_VAR				0x1F11A /* 127258 */

#define NMEA2000_MAG_VAR_PRIO_DEFAULT			6
#define NMEA2000_MAG_VAR_MAX_TRANSFER_LENGTH \
	sizeof(struct nmea2000_mag_var_packet)
#define NMEA2000_MAG_VAR_REPETITION_RATE_MS		1000
#define NMEA2000_MAG_VAR_JITTER_MS			100

#define MAGNETIC_VARIATION_MANUAL			0
#define MAGNETIC_VARIATION_AUTOMATIC_CHART		1
#define MAGNETIC_VARIATION_AUTOMATIC_TABLE		2
#define MAGNETIC_VARIATION_AUTOMATIC_CALCULATION	3
#define MAGNETIC_VARIATION_WMM_2000			4
#define MAGNETIC_VARIATION_WMM_2005			5
#define MAGNETIC_VARIATION_WMM_2010			6
#define MAGNETIC_VARIATION_WMM_2015			7
#define MAGNETIC_VARIATION_WMM_2020			8

/**
 * struct nmea2000_mag_var_packet - Represents the PGN 127258 Magnetic Variation
 *				    packet
 *
 * @sid: Sequence identifier for correlating related PGNs.
 * @variation_source: Source of magnetic variation (5 = WMM2005).
 * @reserved: Reserved field, set to 0xF.
 * @age_of_service: UTC Date in days since January 1, 1970
 * @variation: Magnetic variation (positive = Easterly, negative = Westerly)
 *
 * This structure defines the fields for the Magnetic Variation packet.
 */
struct nmea2000_mag_var_packet {
	uint8_t sid;
	uint8_t variation_source : 4,
		reserved : 4;
	__le32 age_of_service;
	__le16 variation;
} __attribute__((__packed__));

/* NMEA 2000 - PGN 129025 - Position, Rapid Update */
#define NMEA2000_PGN_POSITION_RAPID			0x1F801 /* 129025 */

#define NMEA2000_POSITION_RAPID_PRIO_DEFAULT		6
#define NMEA2000_POSITION_RAPID_MAX_TRANSFER_LENGTH \
	sizeof(struct nmea2000_position_rapid_packet)
#define NMEA2000_POSITION_RAPID_REPETITION_RATE_MS	200
#define NMEA2000_POSITION_RAPID_JITTER_MS		50

/**
 * struct nmea2000_position_rapid_packet - Represents the PGN 129025 Position,
 *                                         Rapid Update packet
 *
 * @latitude: Latitude in 1e-7 degrees ("-" = south, "+" = north)
 * @longitude: Longitude in 1e-7 degrees ("-" = west, "+" = east)
 *
 * This structure defines the fields for the Position, Rapid Update packet.
 */
struct nmea2000_position_rapid_packet {
	__le32 latitude;  /* SPN 263 */
	__le32 longitude; /* SPN 264 */
};


/* NMEA 2000 - PGN 129026 - COG and SOG, Rapid Update */
#define NMEA2000_PGN_COG_SOG_RAPID			0x1F802 /* 129026 */

#define NMEA2000_COG_SOG_RAPID_PRIO_DEFAULT		6
#define NMEA2000_COG_SOG_RAPID_MAX_TRANSFER_LENGTH \
	sizeof(struct nmea2000_cog_sog_rapid_packet)
#define NMEA2000_COG_SOG_RAPID_REPETITION_RATE_MS	250
#define NMEA2000_COG_SOG_RAPID_JITTER_MS		50

#define NMEA2000_COG_REFERENCE_TRUE			0
#define NMEA2000_COG_REFERENCE_MAGNETIC			1
#define NMEA2000_COG_REFERENCE_ERROR			2

/**
 * struct nmea2000_cog_sog_rapid_packet - Represents the PGN 129026 COG and SOG, Rapid Update packet
 *
 * @sid: Sequence identifier for correlating related PGNs.
 * @cog_reference: Course Over Ground reference. Possible values are:
 *                 0 = True, 1 = Magnetic, 2 = Error.
 * @reserved1: Reserved field, set to 0xFF.
 * @cog: Course Over Ground in 1e-4 radians
 * @sog: Speed Over Ground in 1e-2 m/s
 * @reserved2: Reserved field, set to 0xFFFF.
 *
 * This structure defines the fields for the COG and SOG, Rapid Update packet.
 */
struct nmea2000_cog_sog_rapid_packet {
	uint8_t sid;
	uint8_t cog_reference : 2,
		reserved1 : 6;
	__le16 cog;
	__le16 sog;
	uint16_t reserved2;
};

/* NMEA 2000 - PGN 129029 - GNSS Position Data */
#define NMEA2000_PGN_GNSS_POSITION_DATA				0x1F805 /* 129029 */

#define NMEA2000_GNSS_POSITION_DATA_PRIO_DEFAULT		6
#define NMEA2000_GNSS_POSITION_DATA_MAX_TRANSFER_LENGTH	\
	sizeof(struct nmea2000_gnss_position_data_packet)
#define NMEA2000_GNSS_POSITION_DATA_REPETITION_RATE_MS		1000
#define NMEA2000_GNSS_POSITION_DATA_JITTER_MS			100

#define GNSS_TYPE_GPS					0
#define GNSS_TYPE_GLONASS				1
#define GNSS_TYPE_GPS_GLONASS				2
#define GNSS_TYPE_GPS_SBAS_WAAS				3
#define GNSS_TYPE_GPS_SBAS_WAAS_GLONASS			4
#define GNSS_TYPE_CHAYKA				5
#define GNSS_TYPE_INTEGRATED				6
#define GNSS_TYPE_SURVEYED				7
#define GNSS_TYPE_GALILEO				8

#define GNSS_METHOD_NO_GNSS				0
#define GNSS_METHOD_GNSS_FIX				1
#define GNSS_METHOD_DGNSS_FIX				2
#define GNSS_METHOD_PRECISE_GNSS			3
#define GNSS_METHOD_RTK_FIXED_INT			4
#define GNSS_METHOD_RTK_FLOAT				5
#define GNSS_METHOD_ESTIMATED				6
#define GNSS_METHOD_MANUAL_INPUT			7
#define GNSS_METHOD_SIMULATE_MODE			8

/**
 * struct nmea2000_gnss_position_data_packet - Represents the PGN 129029 GNSS
 *					       Position Data packet
 *
 * @sid: Sequence identifier for correlating related PGNs (8 bits).
 * @date: UTC Date in days since January 1, 1970 (16 bits).
 * @time: UTC Time in 0.0001 seconds since midnight (32 bits).
 * @latitude: Latitude in 1e-16 degrees ("-" = south, "+" = north) (64 bits).
 * @longitude: Longitude in 1e-16 degrees ("-" = west, "+" = east) (64 bits).
 * @altitude: Altitude in 1e-6 meters above WGS-84 (64 bits).
 * @gnss_type: GNSS system type (4 bits). Possible values:
 *  - 0: GPS
 *  - 1: GLONASS
 *  - 2: GPS+GLONASS
 *  - 3: GPS+SBAS/WAAS
 *  - 4: GPS+SBAS/WAAS+GLONASS
 *  - 5: Chayka
 *  - 6: Integrated
 *  - 7: Surveyed
 *  - 8: Galileo
 * @gnss_method: GNSS method (4 bits). Possible values:
 *  - 0: No GNSS
 *  - 1: GNSS fix
 *  - 2: DGNSS fix
 *  - 3: Precise GNSS
 *  - 4: RTK Fixed Integer
 *  - 5: RTK float
 *  - 6: Estimated (DR) mode
 *  - 7: Manual Input
 *  - 8: Simulate mode
 * @integrity: Integrity status (2 bits). Possible values:
 *  - 0: No integrity checking
 *  - 1: Safe
 *  - 2: Caution
 * @reserved: Reserved field (6 bits).
 * @num_svs: Number of satellites used in the solution (8 bits).
 * @hdop: Horizontal Dilution of Precision (1e-2) (16 bits).
 * @pdop: Positional Dilution of Precision (1e-2) (16 bits).
 * @geoidal_separation: Geoidal Separation in 0.01 meters (32 bits).
 * @num_ref_stations: Number of reference stations (8 bits).
 *
 * This structure defines the fields for the GNSS Position Data packet.
 */
struct nmea2000_gnss_position_data_packet {
	uint8_t sid;
	__le16 date;
	__le32 time;
	__le64 latitude;
	__le64 longitude;
	__le64 altitude;
	uint8_t gnss_type : 4,
		gnss_method : 4;
	uint8_t integrity : 2,
		reserved : 6;
	uint8_t num_svs;
	__le16 hdop;
	__le16 pdop;
	__le32 geoidal_separation;
	uint8_t num_ref_stations;
} __attribute__((__packed__));

/**
 * struct nmea2000_reference_station - Represents the reference station fields
 *				       in PGN 129029
 *
 * @type: Type of reference station (4 bits).
 *  - Values range from 0 to 13, indicating different types of reference
 *    stations.
 * @id: Reference Station ID (12 bits).
 *  - Unique identifier for the reference station.
 * @dgnss_age: Age of DGNSS corrections in 0.01 seconds (16 bits).
 *  - Indicates the age of the differential GNSS corrections.
 *
 * This structure defines the optional repeating fields for reference stations
 * in the GNSS Position Data packet.
 */
struct nmea2000_reference_station {
	uint8_t type : 4;
	uint16_t id : 12;
	__le16 dgnss_age;
} __attribute__((__packed__));


#endif /* !_J1939_VEHICLE_POSITION_H_ */
