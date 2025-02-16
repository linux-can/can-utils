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

/**
 * j1939_vp1_get_latitude - Get the latitude from the packet
 * @packet: Pointer to the J1939 VP1 packet
 *
 * Return: Latitude in degrees as a signed 32-bit integer
 */
static inline int32_t
j1939_vp1_get_latitude(const struct j1939_vp1_packet *packet)
{
	return le32toh(packet->latitude);
}

/**
 * j1939_vp1_set_latitude - Set the latitude in the packet
 * @packet: Pointer to the J1939 VP1 packet
 * @latitude: Latitude in degrees as a signed 32-bit integer
 */
static inline void
j1939_vp1_set_latitude(struct j1939_vp1_packet *packet, int32_t latitude)
{
	packet->latitude = htole32(latitude);
}

/**
 * j1939_vp1_get_longitude - Get the longitude from the packet
 * @packet: Pointer to the J1939 VP1 packet
 *
 * Return: Longitude in degrees as a signed 32-bit integer
 */
static inline int32_t
j1939_vp1_get_longitude(const struct j1939_vp1_packet *packet)
{
	return le32toh(packet->longitude);
}

/**
 * j1939_vp1_set_longitude - Set the longitude in the packet
 * @packet: Pointer to the J1939 VP1 packet
 * @longitude: Longitude in degrees as a signed 32-bit integer
 */
static inline void
j1939_vp1_set_longitude(struct j1939_vp1_packet *packet, int32_t longitude)
{
	packet->longitude = htole32(longitude);
}

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

/**
 * j1939_vp2_get_total_satellites - Get the total number of satellites
 * @packet: Pointer to the J1939 VP2 packet
 *
 * Return: Total number of satellites as an 8-bit integer
 */
static inline uint8_t j1939_vp2_get_total_satellites(const struct j1939_vp2_packet *packet)
{
	return packet->total_satellites;
}

/**
 * j1939_vp2_set_total_satellites - Set the total number of satellites
 * @packet: Pointer to the J1939 VP2 packet
 * @total_satellites: Total number of satellites as an 8-bit integer
 */
static inline void j1939_vp2_set_total_satellites(struct j1939_vp2_packet *packet,
                                                  uint8_t total_satellites)
{
	packet->total_satellites = total_satellites;
}

/**
 * j1939_vp2_get_hdop - Get the horizontal dilution of precision (HDOP)
 * @packet: Pointer to the J1939 VP2 packet
 *
 * Return: HDOP as an 8-bit integer
 */
static inline uint8_t j1939_vp2_get_hdop(const struct j1939_vp2_packet *packet)
{
	return packet->hdop;
}

/**
 * j1939_vp2_set_hdop - Set the horizontal dilution of precision (HDOP)
 * @packet: Pointer to the J1939 VP2 packet
 * @hdop: HDOP as an 8-bit integer
 */
static inline void j1939_vp2_set_hdop(struct j1939_vp2_packet *packet,
				      uint8_t hdop)
{
	packet->hdop = hdop;
}

/**
 * j1939_vp2_get_vdop - Get the vertical dilution of precision (VDOP)
 * @packet: Pointer to the J1939 VP2 packet
 *
 * Return: VDOP as an 8-bit integer
 */
static inline uint8_t j1939_vp2_get_vdop(const struct j1939_vp2_packet *packet)
{
	return packet->vdop;
}

/**
 * j1939_vp2_set_vdop - Set the vertical dilution of precision (VDOP)
 * @packet: Pointer to the J1939 VP2 packet
 * @vdop: VDOP as an 8-bit integer
 */
static inline void j1939_vp2_set_vdop(struct j1939_vp2_packet *packet,
				      uint8_t vdop)
{
	packet->vdop = vdop;
}

/**
 * j1939_vp2_get_pdop - Get the positional dilution of precision (PDOP)
 * @packet: Pointer to the J1939 VP2 packet
 *
 * Return: PDOP as an 8-bit integer
 */
static inline uint8_t j1939_vp2_get_pdop(const struct j1939_vp2_packet *packet)
{
	return packet->pdop;
}

/**
 * j1939_vp2_set_pdop - Set the positional dilution of precision (PDOP)
 * @packet: Pointer to the J1939 VP2 packet
 * @pdop: PDOP as an 8-bit integer
 */
static inline void j1939_vp2_set_pdop(struct j1939_vp2_packet *packet,
				      uint8_t pdop)
{
	packet->pdop = pdop;
}

/**
 * j1939_vp2_get_tdop - Get the time dilution of precision (TDOP)
 * @packet: Pointer to the J1939 VP2 packet
 *
 * Return: TDOP as an 8-bit integer
 */
static inline uint8_t j1939_vp2_get_tdop(const struct j1939_vp2_packet *packet)
{
	return packet->tdop;
}

/**
 * j1939_vp2_set_tdop - Set the time dilution of precision (TDOP)
 * @packet: Pointer to the J1939 VP2 packet
 * @tdop: TDOP as an 8-bit integer
 */
static inline void j1939_vp2_set_tdop(struct j1939_vp2_packet *packet,
				      uint8_t tdop)
{
	packet->tdop = tdop;
}

/* NMEA 2000 specific definitions */

/* NMEA 2000 - PGN 126992 - System Time */
#define NMEA2000_PGN_SYS_TIME				0x1F010 /* 126992 */

#define NMEA2000_SYS_TIME_PRIO_DEFAULT			6
#define NMEA2000_SYS_TIME_MAX_TRANSFER_LENGTH \
		sizeof(struct nmea2000_sys_time_packet)
#define NMEA2000_SYS_TIME_REPETITION_RATE_MS		1000
#define NMEA2000_SYS_TIME_JITTER_MS			100

/* Bit masks for the source_and_reserved field */
#define NMEA2000_SYS_TIME_SOURCE_MASK			GENMASK(3, 0)
/**
 * enum nmea2000_sys_time_source - Source of time information
 * @NMEA2000_SYS_TIME_SOURCE_GPS: GPS
 * @NMEA2000_SYS_TIME_SOURCE_GLONASS: GLONASS
 * @NMEA2000_SYS_TIME_SOURCE_RADIO_STATION: Radio Station
 * @NMEA2000_SYS_TIME_SOURCE_LOCAL_CESIUM: Local Cesium clock
 * @NMEA2000_SYS_TIME_SOURCE_LOCAL_RUBIDIUM: Local Rubidium clock
 * @NMEA2000_SYS_TIME_SOURCE_LOCAL_CRYSTAL: Local Crystal clock
 */
enum nmea2000_sys_time_source {
	NMEA2000_SYS_TIME_SOURCE_GPS = 0,
	NMEA2000_SYS_TIME_SOURCE_GLONASS = 1,
	NMEA2000_SYS_TIME_SOURCE_RADIO_STATION = 2,
	NMEA2000_SYS_TIME_SOURCE_LOCAL_CESIUM = 3,
	NMEA2000_SYS_TIME_SOURCE_LOCAL_RUBIDIUM = 4,
	NMEA2000_SYS_TIME_SOURCE_LOCAL_CRYSTAL = 5,
};

#define NMEA2000_SYS_TIME_RESERVED_MASK			GENMASK(7, 4)

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
	uint8_t sid;
	uint8_t source_reserved;
	__le16 date;
	__le32 time;
} __attribute__((__packed__));

/**
 * nmea2000_sys_time_get_sid - Get the sequence identifier (SID)
 * @packet: Pointer to the NMEA2000 system time packet
 *
 * Return: Sequence identifier (8 bits)
 */
static inline uint8_t
nmea2000_sys_time_get_sid(const struct nmea2000_sys_time_packet *packet)
{
	return packet->sid;
}

/**
 * nmea2000_sys_time_set_sid - Set the sequence identifier (SID)
 * @packet: Pointer to the NMEA2000 system time packet
 * @sid: Sequence identifier to set (8 bits)
 */
static inline void
nmea2000_sys_time_set_sid(struct nmea2000_sys_time_packet *packet, uint8_t sid)
{
	packet->sid = sid;
}

/**
 * nmea2000_sys_time_get_source - Extract the source of time information
 * @packet: Pointer to the NMEA2000 system time packet
 *
 * Return: Source of time information (4 bits)
 */
static inline uint8_t
nmea2000_sys_time_get_source(const struct nmea2000_sys_time_packet *packet)
{
	return FIELD_GET(NMEA2000_SYS_TIME_SOURCE_MASK, packet->source_reserved);
}

/**
 * nmea2000_sys_time_get_reserved - Extract the reserved field
 * @packet: Pointer to the NMEA2000 system time packet
 *
 * Return: Reserved field (4 bits)
 */
static inline uint8_t
nmea2000_sys_time_get_reserved(const struct nmea2000_sys_time_packet *packet)
{
    return FIELD_GET(NMEA2000_SYS_TIME_RESERVED_MASK, packet->source_reserved);
}

/**
 * nmea2000_sys_time_set_source_reserved - Set the source and reserved fields
 * @packet: Pointer to the NMEA2000 system time packet
 * @source: Source of time information (4 bits)
 * @reserved: Reserved field value (4 bits)
 */
static inline void
nmea2000_sys_time_set_source_reserved(struct nmea2000_sys_time_packet *packet,
				      enum nmea2000_sys_time_source source,
				      uint8_t reserved)
{
	packet->source_reserved = FIELD_PREP(NMEA2000_SYS_TIME_SOURCE_MASK,
					     source) |
				  FIELD_PREP(NMEA2000_SYS_TIME_RESERVED_MASK,
					     reserved);
}

/**
 * nmea2000_sys_time_get_date - Get the UTC date
 * @packet: Pointer to the NMEA2000 system time packet
 *
 * Return: UTC date in days since January 1, 1970
 */
static inline uint16_t
nmea2000_sys_time_get_date(const struct nmea2000_sys_time_packet *packet)
{
	return le16toh(packet->date);
}

/**
 * nmea2000_sys_time_set_date - Set the UTC date
 * @packet: Pointer to the NMEA2000 system time packet
 * @date: UTC date in days since January 1, 1970
 */
static inline void
nmea2000_sys_time_set_date(struct nmea2000_sys_time_packet *packet,
			   uint16_t date)
{
	packet->date = htole16(date);
}

/**
 * nmea2000_sys_time_get_time - Get the UTC time
 * @packet: Pointer to the NMEA2000 system time packet
 *
 * Return: UTC time in 0.0001 seconds since midnight
 */
static inline uint32_t
nmea2000_sys_time_get_time(const struct nmea2000_sys_time_packet *packet)
{
	return le32toh(packet->time);
}

/**
 * nmea2000_sys_time_set_time - Set the UTC time
 * @packet: Pointer to the NMEA2000 system time packet
 * @time: UTC time in 0.0001 seconds since midnight
 */
static inline void
nmea2000_sys_time_set_time(struct nmea2000_sys_time_packet *packet,
			   uint32_t time)
{
	packet->time = htole32(time);
}

/* NMEA 2000 - PGN 127258 - Magnetic Variation */
#define NMEA2000_PGN_MAG_VAR				0x1F11A /* 127258 */

#define NMEA2000_MAG_VAR_PRIO_DEFAULT			6
#define NMEA2000_MAG_VAR_MAX_TRANSFER_LENGTH \
	sizeof(struct nmea2000_mag_var_packet)
#define NMEA2000_MAG_VAR_REPETITION_RATE_MS		1000
#define NMEA2000_MAG_VAR_JITTER_MS			100

#define NMEA2000_MAG_VAR_SOURCE_MASK			GENMASK(3, 0)
/**
 * enum magnetic_variation_source - Source of magnetic variation
 * @MAGNETIC_VARIATION_MANUAL: Manual entry
 * @MAGNETIC_VARIATION_AUTOMATIC_CHART: Automatic from chart
 * @MAGNETIC_VARIATION_AUTOMATIC_TABLE: Automatic from table
 * @MAGNETIC_VARIATION_AUTOMATIC_CALCULATION: Automatic calculation
 * @MAGNETIC_VARIATION_WMM_2000: WMM 2000
 * @MAGNETIC_VARIATION_WMM_2005: WMM 2005
 * @MAGNETIC_VARIATION_WMM_2010: WMM 2010
 * @MAGNETIC_VARIATION_WMM_2015: WMM 2015
 * @MAGNETIC_VARIATION_WMM_2020: WMM 2020
 */
enum magnetic_variation_source {
	MAGNETIC_VARIATION_MANUAL = 0,
	MAGNETIC_VARIATION_AUTOMATIC_CHART = 1,
	MAGNETIC_VARIATION_AUTOMATIC_TABLE = 2,
	MAGNETIC_VARIATION_AUTOMATIC_CALCULATION = 3,
	MAGNETIC_VARIATION_WMM_2000 = 4,
	MAGNETIC_VARIATION_WMM_2005 = 5,
	MAGNETIC_VARIATION_WMM_2010 = 6,
	MAGNETIC_VARIATION_WMM_2015 = 7,
	MAGNETIC_VARIATION_WMM_2020 = 8,
};

#define NMEA2000_MAG_VAR_RESERVED_MASK			GENMASK(7, 4)

/**
 * struct nmea2000_mag_var_packet - Represents the PGN 127258 Magnetic Variation
 *				    packet
 *
 * @sid: Sequence identifier for correlating related PGNs.
 * @source_reserved: Encodes the source of magnetic variation and reserved bits.
 *  - Bits 0-3: Source of magnetic variation (4 bits).
 *      - For example, 5 = WMM2005.
 *  - Bits 4-7: Reserved field (4 bits).
 *      - Reserved for future use, typically set to 0xF.
 * @age_of_service: UTC Date in days since January 1, 1970
 * @variation: Magnetic variation (positive = Easterly, negative = Westerly)
 *
 * This structure defines the fields for the Magnetic Variation packet.
 */
struct nmea2000_mag_var_packet {
	uint8_t sid;
	uint8_t source_reserved;
	__le32 age_of_service;
	__le16 variation;
} __attribute__((__packed__));

/**
 * nmea2000_mag_var_get_sid - Get the sequence identifier (SID)
 * @packet: Pointer to the NMEA2000 magnetic variation packet
 *
 * Return: Sequence identifier (8 bits)
 */
static inline uint8_t
nmea2000_mag_var_get_sid(const struct nmea2000_mag_var_packet *packet)
{
	return packet->sid;
}

/**
 * nmea2000_mag_var_set_sid - Set the sequence identifier (SID)
 * @packet: Pointer to the NMEA2000 magnetic variation packet
 * @sid: Sequence identifier to set (8 bits)
 */
static inline void
nmea2000_mag_var_set_sid(struct nmea2000_mag_var_packet *packet, uint8_t sid)
{
	packet->sid = sid;
}

/**
 * nmea2000_mag_var_get_source - Extract the source of magnetic variation
 * @packet: Pointer to the NMEA2000 magnetic variation packet
 *
 * Return: Source of magnetic variation (4 bits)
 */
static inline uint8_t
nmea2000_mag_var_get_source(const struct nmea2000_mag_var_packet *packet)
{
	return FIELD_GET(NMEA2000_MAG_VAR_SOURCE_MASK, packet->source_reserved);
}

/**
 * nmea2000_mag_var_set_source_reserved - Set the source and reserved fields
 * @packet: Pointer to the NMEA2000 magnetic variation packet
 * @source: Source of magnetic variation (4 bits)
 * @reserved: Reserved field value (4 bits)
 */

static inline void
nmea2000_mag_var_set_source_reserved(struct nmea2000_mag_var_packet *packet,
				     enum magnetic_variation_source source,
				     uint8_t reserved)
{
	packet->source_reserved = FIELD_PREP(NMEA2000_MAG_VAR_SOURCE_MASK,
					     source) |
				  FIELD_PREP(NMEA2000_MAG_VAR_RESERVED_MASK,
					     reserved);
}

/**
 * nmea2000_mag_var_get_age_of_service - Get the age of service
 * @packet: Pointer to the NMEA2000 magnetic variation packet
 *
 * Return: UTC date in days since January 1, 1970
 */
static inline uint32_t
nmea2000_mag_var_get_age_of_service(const struct nmea2000_mag_var_packet *packet)
{
	return le32toh(packet->age_of_service);
}

/**
 * nmea2000_mag_var_set_age_of_service - Set the age of service
 * @packet: Pointer to the NMEA2000 magnetic variation packet
 * @age: UTC date in days since January 1, 1970
 */
static inline void
nmea2000_mag_var_set_age_of_service(struct nmea2000_mag_var_packet *packet,
				    uint32_t age)
{
	packet->age_of_service = htole32(age);
}

/**
 * nmea2000_mag_var_get_variation - Get the magnetic variation
 * @packet: Pointer to the NMEA2000 magnetic variation packet
 *
 * Return: Magnetic variation in tenths of degrees (positive = Easterly,
 *         negative = Westerly)
 */
static inline uint16_t
nmea2000_mag_var_get_variation(const struct nmea2000_mag_var_packet *packet)
{
	return le16toh(packet->variation);
}

/**
 * nmea2000_mag_var_set_variation - Set the magnetic variation
 * @packet: Pointer to the NMEA2000 magnetic variation packet
 * @variation: Magnetic variation in tenths of degrees (positive = Easterly,
 *             negative = Westerly)
 */
static inline void
nmea2000_mag_var_set_variation(struct nmea2000_mag_var_packet *packet,
			       uint16_t variation)
{
	packet->variation = htole16(variation);
}

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
} __attribute__((__packed__));

/**
 * nmea2000_position_get_latitude - Get the latitude from the packet
 * @packet: Pointer to the NMEA2000 position rapid update packet
 *
 * Return: Latitude in degrees as a signed 32-bit integer
 */
static inline int32_t
nmea2000_position_get_latitude(const struct nmea2000_position_rapid_packet *packet)
{
	return le32toh(packet->latitude);
}

/**
 * nmea2000_position_set_latitude - Set the latitude in the packet
 * @packet: Pointer to the NMEA2000 position rapid update packet
 * @latitude: Latitude in degrees as a signed 32-bit integer
 */
static inline void
nmea2000_position_set_latitude(struct nmea2000_position_rapid_packet *packet,
			       int32_t latitude)
{
	packet->latitude = htole32(latitude);
}

/**
 * nmea2000_position_get_longitude - Get the longitude from the packet
 * @packet: Pointer to the NMEA2000 position rapid update packet
 *
 * Return: Longitude in degrees as a signed 32-bit integer
 */
static inline int32_t
nmea2000_position_get_longitude(const struct nmea2000_position_rapid_packet *packet)
{
	return le32toh(packet->longitude);
}

/**
 * nmea2000_position_set_longitude - Set the longitude in the packet
 * @packet: Pointer to the NMEA2000 position rapid update packet
 * @longitude: Longitude in degrees as a signed 32-bit integer
 */
static inline void
nmea2000_position_set_longitude(struct nmea2000_position_rapid_packet *packet,
				int32_t longitude)
{
	packet->longitude = htole32(longitude);
}

/* NMEA 2000 - PGN 129026 - COG and SOG, Rapid Update */
#define NMEA2000_PGN_COG_SOG_RAPID			0x1F802 /* 129026 */

#define NMEA2000_COG_SOG_RAPID_PRIO_DEFAULT		6
#define NMEA2000_COG_SOG_RAPID_MAX_TRANSFER_LENGTH \
	sizeof(struct nmea2000_cog_sog_rapid_packet)
#define NMEA2000_COG_SOG_RAPID_REPETITION_RATE_MS	250
#define NMEA2000_COG_SOG_RAPID_JITTER_MS		50

#define NMEA2000_COG_SOG_REF_MASK			GENMASK(1, 0)
/**
 * enum nmea2000_cog_reference - Reference for Course Over Ground (COG)
 * @NMEA2000_COG_REFERENCE_TRUE: True reference
 * @NMEA2000_COG_REFERENCE_MAGNETIC: Magnetic reference
 * @NMEA2000_COG_REFERENCE_ERROR: Error
 */
enum nmea2000_cog_reference {
	NMEA2000_COG_REFERENCE_TRUE = 0,
	NMEA2000_COG_REFERENCE_MAGNETIC = 1,
	NMEA2000_COG_REFERENCE_ERROR = 2,
};

#define NMEA2000_COG_SOG_RES1_MASK			GENMASK(7, 2)

/**
 * struct nmea2000_cog_sog_rapid_packet - Represents the PGN 129026 COG and SOG,
 *					  Rapid Update packet
 *
 * @sid: Sequence identifier for correlating related PGNs.
 * @cog_ref_res1: Encodes the COG reference and reserved1 fields (8 bits).
 *  - Bits 0-1: COG reference (2 bits).
 *      - 0: True
 *      - 1: Magnetic
 *      - 2: Error
 *  - Bits 2-7: Reserved1 field (6 bits).
 *      - Reserved for future use, typically set to 0xFF.
 * @cog: Course Over Ground in 1e-4 radians
 * @sog: Speed Over Ground in 1e-2 m/s
 * @reserved2: Reserved field, set to 0xFFFF.
 *
 * This structure defines the fields for the COG and SOG, Rapid Update packet.
 */
struct nmea2000_cog_sog_rapid_packet {
	uint8_t sid;
	uint8_t cog_ref_res1;
	__le16 cog;
	__le16 sog;
	uint16_t reserved2;
} __attribute__((__packed__));

/**
 * nmea2000_cog_sog_get_sid - Get the sequence identifier (SID)
 * @packet: Pointer to the NMEA2000 COG and SOG rapid update packet
 *
 * Return: Sequence identifier (8 bits)
 */
static inline uint8_t
nmea2000_cog_sog_get_sid(const struct nmea2000_cog_sog_rapid_packet *packet)
{
	return packet->sid;
}

/**
 * nmea2000_cog_sog_set_sid - Set the sequence identifier (SID)
 * @packet: Pointer to the NMEA2000 COG and SOG rapid update packet
 * @sid: Sequence identifier to set (8 bits)
 */
static inline void
nmea2000_cog_sog_set_sid(struct nmea2000_cog_sog_rapid_packet *packet,
			 uint8_t sid)
{
	packet->sid = sid;
}

/**
 * nmea2000_cog_sog_get_cog_reference - Extract the COG reference
 * @packet: Pointer to the NMEA2000 COG and SOG rapid update packet
 *
 * Return: COG reference (2 bits)
 */
static inline uint8_t
nmea2000_cog_sog_get_cog_reference(const struct nmea2000_cog_sog_rapid_packet *packet)
{
	return FIELD_GET(NMEA2000_COG_SOG_REF_MASK, packet->cog_ref_res1);
}

/**
 * nmea2000_cog_sog_set_cog_ref_res1 - Set the COG reference and reserved1 fields
 * @packet: Pointer to the NMEA2000 COG and SOG rapid update packet
 * @cog_reference: COG reference value (2 bits)
 * @reserved1: Reserved1 value (6 bits)
 */
static inline void
nmea2000_cog_sog_set_cog_ref_res1(struct nmea2000_cog_sog_rapid_packet *packet,
                                  enum nmea2000_cog_reference cog_reference,
				  uint8_t reserved1)
{
	packet->cog_ref_res1 = FIELD_PREP(NMEA2000_COG_SOG_REF_MASK,
					  cog_reference) |
			       FIELD_PREP(NMEA2000_COG_SOG_RES1_MASK,
					  reserved1);
}

/**
 * nmea2000_cog_sog_get_cog - Get the Course Over Ground (COG)
 * @packet: Pointer to the NMEA2000 COG and SOG rapid update packet
 *
 * Return: COG in 1e-4 radians
 */
static inline uint16_t
nmea2000_cog_sog_get_cog(const struct nmea2000_cog_sog_rapid_packet *packet)
{
	return le16toh(packet->cog);
}

/**
 * nmea2000_cog_sog_set_cog - Set the Course Over Ground (COG)
 * @packet: Pointer to the NMEA2000 COG and SOG rapid update packet
 * @cog: COG value in 1e-4 radians
 */
static inline void
nmea2000_cog_sog_set_cog(struct nmea2000_cog_sog_rapid_packet *packet,
                         uint16_t cog)
{
	packet->cog = htole16(cog);
}

/**
 * nmea2000_cog_sog_get_sog - Get the Speed Over Ground (SOG)
 * @packet: Pointer to the NMEA2000 COG and SOG rapid update packet
 *
 * Return: SOG in 1e-2 m/s
 */
static inline uint16_t
nmea2000_cog_sog_get_sog(const struct nmea2000_cog_sog_rapid_packet *packet)
{
	return le16toh(packet->sog);
}

/**
 * nmea2000_cog_sog_set_sog - Set the Speed Over Ground (SOG)
 * @packet: Pointer to the NMEA2000 COG and SOG rapid update packet
 * @sog: SOG value in 1e-2 m/s
 */
static inline void
nmea2000_cog_sog_set_sog(struct nmea2000_cog_sog_rapid_packet *packet,
                         uint16_t sog)
{
	packet->sog = htole16(sog);
}

/* NMEA 2000 - PGN 129029 - GNSS Position Data */
#define NMEA2000_PGN_GNSS_POSITION_DATA				0x1F805 /* 129029 */

#define NMEA2000_GNSS_POSITION_DATA_PRIO_DEFAULT		6
#define NMEA2000_GNSS_POSITION_DATA_MAX_TRANSFER_LENGTH	\
	sizeof(struct nmea2000_gnss_position_data_packet)
#define NMEA2000_GNSS_POSITION_DATA_REPETITION_RATE_MS		1000
#define NMEA2000_GNSS_POSITION_DATA_JITTER_MS			100

#define NMEA2000_GNSS_TYPE_MASK				GENMASK(3, 0)
/**
 * enum nmea2000_gnss_type - Types of GNSS systems
 * @GNSS_TYPE_GPS: GPS
 * @GNSS_TYPE_GLONASS: GLONASS
 * @GNSS_TYPE_GPS_GLONASS: Combined GPS and GLONASS
 * @GNSS_TYPE_GPS_SBAS_WAAS: GPS with SBAS/WAAS
 * @GNSS_TYPE_GPS_SBAS_WAAS_GLONASS: Combined GPS, SBAS/WAAS, and GLONASS
 * @GNSS_TYPE_CHAYKA: Chayka navigation system
 * @GNSS_TYPE_INTEGRATED: Integrated navigation
 * @GNSS_TYPE_SURVEYED: Surveyed position
 * @GNSS_TYPE_GALILEO: Galileo navigation system
 */
enum nmea2000_gnss_type {
	GNSS_TYPE_GPS = 0,
	GNSS_TYPE_GLONASS = 1,
	GNSS_TYPE_GPS_GLONASS = 2,
	GNSS_TYPE_GPS_SBAS_WAAS = 3,
	GNSS_TYPE_GPS_SBAS_WAAS_GLONASS = 4,
	GNSS_TYPE_CHAYKA = 5,
	GNSS_TYPE_INTEGRATED = 6,
	GNSS_TYPE_SURVEYED = 7,
	GNSS_TYPE_GALILEO = 8,
};

#define NMEA2000_GNSS_METHOD_MASK			GENMASK(7, 4)
/**
 * enum nmea2000_gnss_method - GNSS methods
 * @GNSS_METHOD_NO_GNSS: No GNSS
 * @GNSS_METHOD_GNSS_FIX: GNSS fix
 * @GNSS_METHOD_DGNSS_FIX: Differential GNSS (DGNSS) fix
 * @GNSS_METHOD_PRECISE_GNSS: Precise GNSS fix
 * @GNSS_METHOD_RTK_FIXED_INT: RTK fixed integer solution
 * @GNSS_METHOD_RTK_FLOAT: RTK float solution
 * @GNSS_METHOD_ESTIMATED: Estimated (Dead Reckoning) mode
 * @GNSS_METHOD_MANUAL_INPUT: Manual input mode
 * @GNSS_METHOD_SIMULATE_MODE: Simulated GNSS mode
 */
enum nmea2000_gnss_method {
	GNSS_METHOD_NO_GNSS = 0,
	GNSS_METHOD_GNSS_FIX = 1,
	GNSS_METHOD_DGNSS_FIX = 2,
	GNSS_METHOD_PRECISE_GNSS = 3,
	GNSS_METHOD_RTK_FIXED_INT = 4,
	GNSS_METHOD_RTK_FLOAT = 5,
	GNSS_METHOD_ESTIMATED = 6,
	GNSS_METHOD_MANUAL_INPUT = 7,
	GNSS_METHOD_SIMULATE_MODE = 8,
};

#define NMEA2000_INTEGRITY_MASK				GENMASK(1, 0)
/**
 * enum nmea2000_integrity_status - Integrity status values
 * @NMEA2000_INTEGRITY_NO_CHECKING: No integrity checking
 * @NMEA2000_INTEGRITY_SAFE: Safe integrity status
 * @NMEA2000_INTEGRITY_CAUTION: Caution integrity status
 */
enum nmea2000_integrity_status {
	NMEA2000_INTEGRITY_NO_CHECKING = 0,
	NMEA2000_INTEGRITY_SAFE = 1,
	NMEA2000_INTEGRITY_CAUTION = 2,
};

#define NMEA2000_RESERVED_MASK				GENMASK(7, 2)

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
 * @gnss_info: Encodes GNSS system type and GNSS method in a single byte.
 *  - Bits 0-3: GNSS system type. Possible values:
 *      - 0: GPS
 *      - 1: GLONASS
 *      - 2: GPS+GLONASS
 *      - 3: GPS+SBAS/WAAS
 *      - 4: GPS+SBAS/WAAS+GLONASS
 *      - 5: Chayka
 *      - 6: Integrated
 *      - 7: Surveyed
 *      - 8: Galileo
 *  - Bits 4-7: GNSS method. Possible values:
 *      - 0: No GNSS
 *      - 1: GNSS fix
 *      - 2: DGNSS fix
 *      - 3: Precise GNSS
 *      - 4: RTK Fixed Integer
 *      - 5: RTK float
 *      - 6: Estimated (DR) mode
 *      - 7: Manual Input
 *      - 8: Simulate mode
 *
 * @status: Encodes integrity status and reserved bits in a single byte.
 *  - Bits 0-1: Integrity status. Possible values:
 *      - 0: No integrity checking
 *      - 1: Safe
 *      - 2: Caution
 *  - Bits 2-7: Reserved field.
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
	uint8_t gnss_info;
	uint8_t status;
	uint8_t num_svs;
	__le16 hdop;
	__le16 pdop;
	__le32 geoidal_separation;
	uint8_t num_ref_stations;
} __attribute__((__packed__));

/**
 * nmea2000_gnss_get_sid - Get the sequence identifier
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: Sequence identifier (8 bits)
 */
static inline uint8_t
nmea2000_gnss_get_sid(const struct nmea2000_gnss_position_data_packet *packet)
{
	return packet->sid;
}

/**
 * nmea2000_gnss_set_sid - Set the sequence identifier
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @sid: Sequence identifier to set
 */
static inline void
nmea2000_gnss_set_sid(struct nmea2000_gnss_position_data_packet *packet,
		      uint8_t sid)
{
	packet->sid = sid;
}

/**
 * nmea2000_gnss_get_date - Get the UTC date
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: UTC date in days since January 1, 1970
 */
static inline uint16_t
nmea2000_gnss_get_date(const struct nmea2000_gnss_position_data_packet *packet)
{
	return le16toh(packet->date);
}

/**
 * nmea2000_gnss_set_date - Set the UTC date
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @date: UTC date in days since January 1, 1970
 */
static inline void
nmea2000_gnss_set_date(struct nmea2000_gnss_position_data_packet *packet,
		       uint16_t date)
{
	packet->date = htole16(date);
}

/**
 * nmea2000_gnss_get_time - Get the UTC time
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: UTC time in 0.0001 seconds since midnight
 */
static inline uint32_t
nmea2000_gnss_get_time(const struct nmea2000_gnss_position_data_packet *packet)
{
	return le32toh(packet->time);
}

/**
 * nmea2000_gnss_set_time - Set the UTC time
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @time: UTC time in 0.0001 seconds since midnight
 */
static inline void
nmea2000_gnss_set_time(struct nmea2000_gnss_position_data_packet *packet,
		       uint32_t time)
{
	packet->time = htole32(time);
}

/**
 * nmea2000_gnss_get_latitude - Get the latitude
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: Latitude in 1e-16 degrees (int64_t)
 */
static inline int64_t
nmea2000_gnss_get_latitude(const struct nmea2000_gnss_position_data_packet *packet)
{
	return le64toh(packet->latitude);
}

/**
 * nmea2000_gnss_set_latitude - Set the latitude
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @latitude: Latitude in 1e-16 degrees (int64_t)
 */
static inline void
nmea2000_gnss_set_latitude(struct nmea2000_gnss_position_data_packet *packet,
			   int64_t latitude)
{
	packet->latitude = htole64(latitude);
}

/**
 * nmea2000_gnss_get_longitude - Get the longitude
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: Longitude in 1e-16 degrees (int64_t)
 */
static inline int64_t
nmea2000_gnss_get_longitude(const struct nmea2000_gnss_position_data_packet *packet)
{
	return le64toh(packet->longitude);
}

/**
 * nmea2000_gnss_set_longitude - Set the longitude
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @longitude: Longitude in 1e-16 degrees (int64_t)
 */
static inline void
nmea2000_gnss_set_longitude(struct nmea2000_gnss_position_data_packet *packet,
			    int64_t longitude)
{
	packet->longitude = htole64(longitude);
}

/**
 * nmea2000_gnss_get_altitude - Get the altitude
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: Altitude in 1e-6 meters above WGS-84 (int64_t)
 */
static inline int64_t
nmea2000_gnss_get_altitude(const struct nmea2000_gnss_position_data_packet *packet)
{
	return le64toh(packet->altitude);
}

/**
 * nmea2000_gnss_set_altitude - Set the altitude
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @altitude: Altitude in 1e-6 meters above WGS-84 (int64_t)
 */
static inline void
nmea2000_gnss_set_altitude(struct nmea2000_gnss_position_data_packet *packet,
			   int64_t altitude)
{
	packet->altitude = htole64(altitude);
}

/**
 * nmea2000_get_gnss_type - Extract GNSS type from the packet
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: GNSS type (lower 4 bits of gnss_info field)
 */
static inline uint8_t
nmea2000_get_gnss_type(const struct nmea2000_gnss_position_data_packet *packet)
{
	return FIELD_GET(NMEA2000_GNSS_TYPE_MASK, packet->gnss_info);
}

/**
 * nmea2000_get_gnss_method - Extract GNSS method from the packet
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: GNSS method (upper 4 bits of gnss_info field)
 */
static inline uint8_t
nmea2000_get_gnss_method(const struct nmea2000_gnss_position_data_packet *packet)
{
	return FIELD_GET(NMEA2000_GNSS_METHOD_MASK, packet->gnss_info);
}

/**
 * nmea2000_set_gnss_info - Set GNSS type and method in the packet
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @gnss_type: GNSS type to set
 * @gnss_method: GNSS method to set
 */
static inline void
nmea2000_set_gnss_info(struct nmea2000_gnss_position_data_packet *packet,
		       enum nmea2000_gnss_type gnss_type,
		       enum nmea2000_gnss_method gnss_method)
{
	packet->gnss_info = FIELD_PREP(NMEA2000_GNSS_TYPE_MASK, gnss_type) |
			    FIELD_PREP(NMEA2000_GNSS_METHOD_MASK, gnss_method);
}

/**
 * nmea2000_get_integrity - Extract integrity status from the packet
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: Integrity status (lower 2 bits of status field)
 */
static inline uint8_t
nmea2000_get_integrity(const struct nmea2000_gnss_position_data_packet *packet)
{
	return FIELD_GET(NMEA2000_INTEGRITY_MASK, packet->status);
}

/**
 * nmea2000_set_status - Set integrity and reserved fields in the packet
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @integrity: Integrity status to set
 * @reserved: Reserved value to set
 */
static inline void
nmea2000_set_status(struct nmea2000_gnss_position_data_packet *packet,
		    enum nmea2000_integrity_status integrity, uint8_t reserved)
{
	packet->status = FIELD_PREP(NMEA2000_INTEGRITY_MASK, integrity) |
			 FIELD_PREP(NMEA2000_RESERVED_MASK, reserved);
}

/**
 * nmea2000_gnss_get_num_svs - Get the number of satellites used in the solution
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: Number of satellites used (8 bits)
 */
static inline uint8_t
nmea2000_gnss_get_num_svs(const struct nmea2000_gnss_position_data_packet *packet)
{
	return packet->num_svs;
}

/**
 * nmea2000_gnss_set_num_svs - Set the number of satellites used in the solution
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @num_svs: Number of satellites used (8 bits)
 */
static inline void
nmea2000_gnss_set_num_svs(struct nmea2000_gnss_position_data_packet *packet,
			  uint8_t num_svs)
{
	packet->num_svs = num_svs;
}

/**
 * nmea2000_gnss_get_hdop - Get the Horizontal Dilution of Precision (HDOP)
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: HDOP in 1e-2 units (uint16_t)
 */
static inline uint16_t
nmea2000_gnss_get_hdop(const struct nmea2000_gnss_position_data_packet *packet)
{
	return le16toh(packet->hdop);
}

/**
 * nmea2000_gnss_set_hdop - Set the Horizontal Dilution of Precision (HDOP)
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @hdop: HDOP in 1e-2 units (uint16_t)
 */
static inline void
nmea2000_gnss_set_hdop(struct nmea2000_gnss_position_data_packet *packet,
		       uint16_t hdop)
{
	packet->hdop = htole16(hdop);
}

/**
 * nmea2000_gnss_get_pdop - Get the Positional Dilution of Precision (PDOP)
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: PDOP in 1e-2 units (uint16_t)
 */
static inline uint16_t
nmea2000_gnss_get_pdop(const struct nmea2000_gnss_position_data_packet *packet)
{
	return le16toh(packet->pdop);
}

/**
 * nmea2000_gnss_set_pdop - Set the Positional Dilution of Precision (PDOP)
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @pdop: PDOP in 1e-2 units (uint16_t)
 */
static inline void
nmea2000_gnss_set_pdop(struct nmea2000_gnss_position_data_packet *packet,
		       uint16_t pdop)
{
	packet->pdop = htole16(pdop);
}

/**
 * nmea2000_gnss_get_geoidal_separation - Get the Geoidal Separation
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: Geoidal Separation in 0.01 meters (uint32_t)
 */
static inline uint32_t
nmea2000_gnss_get_geoidal_separation(const struct nmea2000_gnss_position_data_packet *packet)
{
	return le32toh(packet->geoidal_separation);
}

/**
 * nmea2000_gnss_set_geoidal_separation - Set the Geoidal Separation
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @geoidal_separation: Geoidal Separation in 0.01 meters (uint32_t)
 */
static inline void
nmea2000_gnss_set_geoidal_separation(struct nmea2000_gnss_position_data_packet *packet,
				     uint32_t geoidal_separation)
{
	packet->geoidal_separation = htole32(geoidal_separation);
}

/**
 * nmea2000_gnss_get_num_ref_stations - Get the number of reference stations
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 *
 * Return: Number of reference stations (8 bits)
 */
static inline uint8_t
nmea2000_gnss_get_num_ref_stations(const struct nmea2000_gnss_position_data_packet *packet)
{
	return packet->num_ref_stations;
}

/**
 * nmea2000_gnss_set_num_ref_stations - Set the number of reference stations
 * @packet: Pointer to the NMEA2000 GNSS position data packet
 * @num_ref_stations: Number of reference stations (8 bits)
 */
static inline void
nmea2000_gnss_set_num_ref_stations(struct nmea2000_gnss_position_data_packet *packet,
				   uint8_t num_ref_stations)
{
	packet->num_ref_stations = num_ref_stations;
}

#define NMEA2000_REF_STATION_TYPE_MASK		GENMASK(3, 0)
#define NMEA2000_REF_STATION_ID_MASK		GENMASK(15, 4)

/**
 * struct nmea2000_reference_station - Represents the reference station fields
 *                                     in PGN 129029
 *
 * @type_id: Encodes the type and ID of the reference station (16 bits).
 *  - Bits 0-3: Type of reference station (4 bits).
 *      - Values range from 0 to 13, indicating different types of reference
 *        stations.
 *  - Bits 4-15: Reference Station ID (12 bits).
 *      - Unique identifier for the reference station.
 * @dgnss_age: Age of DGNSS corrections in 0.01 seconds (16 bits).
 *  - Indicates the age of the differential GNSS corrections.
 *
 * This structure defines the optional repeating fields for reference stations
 * in the GNSS Position Data packet. Bitfield operations on the type and ID
 * are handled using FIELD_GET and FIELD_PREP macros for clarity and safety.
 */
struct nmea2000_reference_station {
	__le16 type_id;
	__le16 dgnss_age;
} __attribute__((__packed__));

/**
 * nmea2000_ref_station_get_type - Extract the type of reference station
 * @station: Pointer to the NMEA2000 reference station structure
 *
 * Return: Type of the reference station (4 bits)
 */
static inline uint8_t
nmea2000_ref_station_get_type(const struct nmea2000_reference_station *station)
{
	return FIELD_GET(NMEA2000_REF_STATION_TYPE_MASK,
			 le16toh(station->type_id));
}

/**
 * nmea2000_ref_station_get_id - Extract the reference station ID
 * @station: Pointer to the NMEA2000 reference station structure
 *
 * Return: Reference station ID (12 bits)
 */
static inline uint16_t
nmea2000_ref_station_get_id(const struct nmea2000_reference_station *station)
{
	return FIELD_GET(NMEA2000_REF_STATION_ID_MASK,
			 le16toh(station->type_id));
}

/**
 * nmea2000_ref_station_set_type_id - Set type and ID of reference station
 * @station: Pointer to the NMEA2000 reference station structure
 * @type: Type of the reference station (4 bits)
 * @id: Reference station ID (12 bits)
 */
static inline void
nmea2000_ref_station_set_type_id(struct nmea2000_reference_station *station,
				 uint8_t type, uint16_t id)
{
	station->type_id =
		htole16(FIELD_PREP(NMEA2000_REF_STATION_TYPE_MASK, type) |
			FIELD_PREP(NMEA2000_REF_STATION_ID_MASK, id));
}

/**
 * nmea2000_ref_station_get_dgnss_age - Get the age of DGNSS corrections
 * @station: Pointer to the NMEA2000 reference station structure
 *
 * Return: Age of DGNSS corrections in 0.01 seconds (16 bits)
 */
static inline uint16_t
nmea2000_ref_station_get_dgnss_age(const struct nmea2000_reference_station *station)
{
	return le16toh(station->dgnss_age);
}

/**
 * nmea2000_ref_station_set_dgnss_age - Set the age of DGNSS corrections
 * @station: Pointer to the NMEA2000 reference station structure
 * @dgnss_age: Age of DGNSS corrections in 0.01 seconds (16 bits)
 */
static inline void
nmea2000_ref_station_set_dgnss_age(struct nmea2000_reference_station *station,
				   uint16_t dgnss_age)
{
	station->dgnss_age = htole16(dgnss_age);
}

#endif /* !_J1939_VEHICLE_POSITION_H_ */
