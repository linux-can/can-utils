// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2024 Oleksij Rempel <linux@rempel-privat.de>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <gps.h>
#include <linux/kernel.h>
#include <math.h>
#include <net/if.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "j1939_vehicle_position_cmn.h"

#define J1939_VP_SRV_MAX_EPOLL_EVENTS	10

#define PROFILE_J1939			BIT(0)
#define PROFILE_NMEA2000		BIT(1)

struct j1939_vp_srv_priv;

struct j1939_pgn_handler {
	uint32_t pgn;
	int (*prepare_data)(struct j1939_vp_srv_priv *priv, void *data);
	int sock;
	int sock_priority;
	struct timespec last_time;
	struct timespec next_time;
	int repetition_rate_ms;
	int jitter_ms;
	size_t data_size;
	uint8_t profile;
};

struct j1939_vp_srv_priv {
	struct sockaddr_can sockname;
	struct j1939_vp_stats stats;
	struct libj1939_cmn cmn;
	bool sim_mode;
	struct gps_data_t gps_data;
	uint8_t sid;
	struct j1939_pgn_handler *handlers;
	size_t num_handlers;
	uint8_t profile;
};


/**
 * timespec_to_nmea2000_datetime - Convert timespec_t to NMEA 2000 date and time
 *
 * @ts: Pointer to the timespec_t structure representing the time
 * @date: Pointer to store the converted NMEA 2000 date (days since 1970-01-01)
 *	  or NULL
 * @time: Pointer to store the converted NMEA 2000 time (0.0001 seconds since
 *	  midnight) or NULL
 *
 * This function converts the provided timespec_t structure to NMEA 2000 date
 * and time formats. The date is calculated as the number of days since
 * 1970-01-01, and the time is calculated as 0.0001 seconds since midnight.
 */
static void timespec_to_nmea2000_datetime(const struct timespec *ts,
					  uint16_t *date, uint32_t *time)
{
	time_t time_secs = ts->tv_sec;
	long gps_nsec = ts->tv_nsec;

	if (date) {
		/* Calculate the number of days since January 1, 1970 */
		*date = time_secs / 86400;
	}

	if (time) {
		/* Calculate the time of day in 0.0001 seconds since midnight */
		*time = (time_secs % 86400) * 10000 + gps_nsec / 100000;
	}
}

/**
 * update_real_gps_data - Update the GPS data from the GPS device.
 * @priv: Pointer to the private data structure of the J1939 VP server.
 *
 * This function checks if there is new data available from the GPS device.
 * It then attempts to read the data and verifies if the GPS mode is set
 * properly. The function returns 0 on success and propagates original error
 * codes from gpsd library functions on failure.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int update_real_gps_data(struct j1939_vp_srv_priv *priv)
{
	static time_t last_warn_time;
	static bool gps_waiting_error;
	int ret;

	if (!gps_waiting(&priv->gps_data, 0)) {
		if (errno) {
			ret = -errno;
			pr_warn("gps_waiting() error: %s\n", strerror(errno));
			return ret;
		} else {
			time_t now = time(NULL);

			/* Warn only once every 10 seconds */
			if (!gps_waiting_error) {
				last_warn_time = now;
				gps_waiting_error = true;
				return -EAGAIN;
			} else if (now - last_warn_time > 10) {
				last_warn_time = now;
				pr_warn("No GPS data available\n");
				return -ENODATA;
			} else {
				return -EAGAIN;
			}
		}
	}

	gps_waiting_error = false;

	ret = gps_read(&priv->gps_data, NULL, 0);
	if (ret == -1) {
		if (errno) {
			ret = -errno;
			pr_warn("gps_read() Unix-level error: %s\n", strerror(errno));
			return ret;
		} else {
			pr_warn("gps_read() returned -1 without setting errno, possibly connection closed or shared memory unavailable.\n");
			return -EIO;
		}
	} else if (ret == 0) {
		pr_warn("gps_read() returned 0, no data available.\n");
		return -ENODATA;
	}

	if (MODE_SET != (MODE_SET & priv->gps_data.set)) {
		pr_warn("GPS mode not set\n");
		return -EINVAL;
	}

	priv->sid++;

	return 0;
}

/**
 * update_simulation_gps_data - Simulate GPS data for testing purposes.
 * @priv: Pointer to the private data structure of the J1939 VP server.
 *
 * This function generates simulated GPS data to mimic real-world GPS signals.
 * It increments the latitude and longitude slightly with each call, loops the
 * number of visible satellites, and adjusts DOP values. This simulation mode
 * is useful for testing and debugging when real GPS hardware is not available.
 * The data is updated in the priv->gps_data structure, which can be used by
 * other parts of the application.
 */
static void simulate_gps_data(struct j1939_vp_srv_priv *priv)
{
	int ret;

	/* The initial coordinates (15.1205, 18.0513) are a fun easter egg,
	 * based on the author's name, Oleksij Rempel. "ole" from Oleksij sets
	 * the latitude, and "rem" from Rempel sets the longitude. It's a little
	 * personal touch that makes the simulation mode unique.
	 */
	static double sim_latitude = 15.1205;
	static double sim_longitude = 18.0513;
	static uint8_t sim_satellites = 5;
	static double sim_hdop = 0.8;
	static double sim_vdop = 1.0;
	static double sim_pdop = 1.2;
	static double sim_tdop = 1.5;

	/* Increment the simulated data for variability */
	sim_latitude += 0.0001;
	sim_longitude += 0.0001;
	sim_satellites = (sim_satellites + 1) % 16;  // Loop from 0 to 15
	sim_hdop += 0.01;
	sim_vdop += 0.01;
	sim_pdop += 0.01;
	sim_tdop += 0.01;

	/* Ensure the values stay within reasonable bounds */
	if (sim_latitude > 90.0)
		sim_latitude = -90.0;
	if (sim_longitude > 180.0)
		sim_longitude = -180.0;
	if (sim_hdop > 2.0)
		sim_hdop = 0.8;
	if (sim_vdop > 2.5)
		sim_vdop = 1.0;
	if (sim_pdop > 3.0)
		sim_pdop = 1.2;
	if (sim_tdop > 3.5)
		sim_tdop = 1.5;

	priv->gps_data.fix.latitude = sim_latitude;
	priv->gps_data.fix.longitude = sim_longitude;
	priv->gps_data.satellites_visible = sim_satellites;
	priv->gps_data.dop.hdop = sim_hdop;
	priv->gps_data.dop.vdop = sim_vdop;
	priv->gps_data.dop.pdop = sim_pdop;
	priv->gps_data.dop.tdop = sim_tdop;
	priv->gps_data.set = MODE_SET | LATLON_SET | DOP_SET | SATELLITE_SET;
	priv->gps_data.fix.mode = MODE_2D;

	/* Set the time to the current system time */
	ret = clock_gettime(CLOCK_REALTIME, &priv->gps_data.fix.time);
	if (ret < 0) {
		pr_warn("Failed to get current time: %s\n", strerror(errno));
	} else {
		priv->gps_data.set |= TIME_SET;
	}

	/* Set the speed and track to 0 */
	priv->gps_data.fix.speed = 0.0;
	priv->gps_data.fix.track = 0.0;
	priv->gps_data.set |= TRACK_SET | SPEED_SET;

	priv->sid++;
}

static int update_gps_data(struct j1939_vp_srv_priv *priv)
{
	if (priv->sim_mode) {
		simulate_gps_data(priv);
		return 0;
	}

	return update_real_gps_data(priv);
}

/* ----------------- PGN handlers start ----------------- */
/* ----------------- SAE J1939 specific ----------------- */
/**
 * j1939_vp2_get_data - Fills the VP2 packet with current GPS data.
 * @priv: Pointer to the server's private data structure.
 * @vp2p: Pointer to the VP2 packet structure to populate.
 *
 * This function retrieves GPS data from the server's private data and fills
 * the provided VP2 packet structure with information such as the number of
 * visible satellites and various DOP values. The values are converted using
 * a scale factor based on assumptions, as the exact specification is not
 * defined.
 *
 * Return: 0 on success.
 */
static int j1939_vp2_get_data(struct j1939_vp_srv_priv *priv,
			      struct j1939_vp2_packet *vp2p)
{
	uint8_t hdop, vdop, pdop, tdop;

	if (priv->gps_data.set & DOP_SET) {
		hdop = priv->gps_data.dop.hdop * 10;
		vdop = priv->gps_data.dop.vdop * 10;
		pdop = priv->gps_data.dop.pdop * 10;
		tdop = priv->gps_data.dop.tdop * 10;
	} else {
		hdop = UINT8_MAX;
		vdop = UINT8_MAX;
		pdop = UINT8_MAX;
		tdop = UINT8_MAX;
	}

	j1939_vp2_set_total_satellites(vp2p, priv->gps_data.satellites_visible);
	j1939_vp2_set_hdop(vp2p, hdop);
	j1939_vp2_set_vdop(vp2p, vdop);
	j1939_vp2_set_pdop(vp2p, pdop);
	j1939_vp2_set_tdop(vp2p, tdop);

	return 0;
}

static int j1939_vp2_prepare_data(struct j1939_vp_srv_priv *priv, void *data)
{
	struct j1939_vp2_packet *vp2p = data;

	return j1939_vp2_get_data(priv, vp2p);
}

/**
 * j1939_vp1_get_data - Populates the VP1 packet with GPS coordinates.
 * @priv: Pointer to the server's private data structure.
 * @vp1p: Pointer to the VP1 packet structure to populate.
 *
 * This function retrieves the current GPS coordinates (latitude and longitude)
 * from the server's data and populates the provided VP1 packet. It checks for
 * a valid GPS fix and converts the coordinates to a format suitable for
 * transmission.
 *
 * Return: 0 on success, or -ENODATA if the GPS data is invalid or unavailable.
 */
static int j1939_vp1_get_data(struct j1939_vp_srv_priv *priv,
			      struct j1939_vp1_packet *vp1p)
{
	uint32_t latitude, longitude;

	if (priv->gps_data.set & LATLON_SET) {
		latitude = (priv->gps_data.fix.latitude + 210.0) * 1e7;
		longitude = (priv->gps_data.fix.longitude + 210.0) * 1e7;
	} else {
		latitude = UINT32_MAX;
		longitude = UINT32_MAX;
	}

	j1939_vp1_set_latitude(vp1p, latitude);
	j1939_vp1_set_longitude(vp1p, longitude);

	return 0;
}

static int j1939_vp1_prepare_data(struct j1939_vp_srv_priv *priv, void *data)
{
	struct j1939_vp1_packet *vp1p = data;

	return j1939_vp1_get_data(priv, vp1p);
}

/* ----------------- NMEA 2000 specific ----------------- */
/**
 * nmea2000_sys_time_get_data - Fills the System Time packet with current GPS
 *				time data.
 * @priv: Pointer to the server's private data structure.
 * @stp: Pointer to the System Time packet structure to populate.
 *
 * This function retrieves the current UTC date and time from the GPS data
 * provided by gpsd and fills the provided System Time packet structure. The
 * date is given as the number of days since January 1, 1970, and the time is
 * in 0.0001 seconds  since midnight. The sequence identifier (sid) is managed
 * by the server's private data structure.
 *
 * Return: 0 on success, or a negative error code on failure.
 */

static int nmea2000_sys_time_get_data(struct j1939_vp_srv_priv *priv,
				      struct nmea2000_sys_time_packet *stp)
{
	enum nmea2000_sys_time_source source;
	uint16_t nmea2000_date;
	uint32_t nmea2000_time;

	nmea2000_sys_time_set_sid(stp, priv->sid);

	if (priv->sim_mode)
		source = NMEA2000_SYS_TIME_SOURCE_LOCAL_CRYSTAL;
	else
		source = NMEA2000_SYS_TIME_SOURCE_GPS;

	nmea2000_sys_time_set_source_reserved(stp, source, 0xf);

	if (priv->gps_data.set & TIME_SET) {
		timespec_to_nmea2000_datetime(&priv->gps_data.fix.time,
					      &nmea2000_date, &nmea2000_time);

	} else {
		nmea2000_date = UINT16_MAX;
		nmea2000_time = UINT32_MAX;
	}

	nmea2000_sys_time_set_date(stp, nmea2000_date);
	nmea2000_sys_time_set_time(stp, nmea2000_time);

	return 0;
}

/**
 * nmea2000_sys_time_prepare_data - Prepares the data for the System Time packet.
 * @priv: Pointer to the server's private data structure.
 * @data: Pointer to the buffer where the packet data will be stored.
 *
 * This function calls the nmea2000_sys_time_get_data function to populate the
 * System Time packet structure with the current time data and stores it in the
 * provided buffer.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int
nmea2000_sys_time_prepare_data(struct j1939_vp_srv_priv *priv, void *data)
{
	struct nmea2000_sys_time_packet *stp = data;

	return nmea2000_sys_time_get_data(priv, stp);
}

/**
 * nmea2000_mag_var_get_data - Fills the Magnetic Variation packet with current
 *			       data.
 * @priv: Pointer to the server's private data structure.
 * @mvp: Pointer to the Magnetic Variation packet structure to populate.
 *
 * This function retrieves the current magnetic variation data, including the
 * source, age of service, and the magnetic variation itself. The values are
 * filled into the provided Magnetic Variation packet structure.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int nmea2000_mag_var_get_data(struct j1939_vp_srv_priv *priv,
				     struct nmea2000_mag_var_packet *mvp)
{
	nmea2000_mag_var_set_sid(mvp, priv->sid);

	/* FIXME: provide valid values */
	nmea2000_mag_var_set_source_reserved(mvp, MAGNETIC_VARIATION_MANUAL,
					     0xf);
	nmea2000_mag_var_set_age_of_service(mvp, UINT32_MAX);
	nmea2000_mag_var_set_variation(mvp, UINT16_MAX);

	return 0;
}

/**
 * nmea2000_mag_var_prepare_data - Prepares the data for the Magnetic Variation
 *				   packet.
 * @priv: Pointer to the server's private data structure.
 * @data: Pointer to the buffer where the packet data will be stored.
 *
 * This function calls the nmea2000_mag_var_get_data function to populate the
 * Magnetic Variation packet structure with the current data and stores it in
 * the provided buffer.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int
nmea2000_mag_var_prepare_data(struct j1939_vp_srv_priv *priv, void *data)
{
	struct nmea2000_mag_var_packet *mvp = data;

	return nmea2000_mag_var_get_data(priv, mvp);
}

/**
 * nmea2000_position_rapid_get_data - Fills the Position, Rapid Update packet
 *				      with current GPS data.
 * @priv: Pointer to the server's private data structure.
 * @prp: Pointer to the Position, Rapid Update packet structure to populate.
 *
 * This function retrieves the current latitude and longitude from the GPS data
 * provided by gpsd and fills the provided Position, Rapid Update packet
 * structure. The latitude and longitude are provided in units of 1e-7 degrees,
 * with negative values indicating south and west, and positive values
 * indicating north and east.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int nmea2000_position_rapid_get_data(struct j1939_vp_srv_priv *priv,
				struct nmea2000_position_rapid_packet *prp)
{
	int32_t latitude, longitude;

	if (priv->gps_data.set & LATLON_SET) {
		latitude = priv->gps_data.fix.latitude * 1e7;
		longitude = priv->gps_data.fix.longitude * 1e7;
	} else {
		latitude = INT32_MAX;
		longitude = INT32_MAX;
	}

	nmea2000_position_set_latitude(prp, latitude);
	nmea2000_position_set_longitude(prp, longitude);

	return 0;
}

/**
 * nmea2000_position_rapid_prepare_data - Prepares the data for the Position,
 *					  Rapid Update packet.
 * @priv: Pointer to the server's private data structure.
 * @data: Pointer to the buffer where the packet data will be stored.
 *
 * This function calls the nmea2000_position_rapid_get_data function to populate
 * the Position, Rapid Update packet structure with the current GPS data and
 * stores it in the provided buffer.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int nmea2000_position_rapid_prepare_data(struct j1939_vp_srv_priv *priv,
						void *data)
{
	struct nmea2000_position_rapid_packet *prp = data;

	return nmea2000_position_rapid_get_data(priv, prp);
}

/**
 * nmea2000_cog_sog_rapid_get_data - Fills the COG and SOG, Rapid Update packet
 *				     with current GPS data.
 * @priv: Pointer to the server's private data structure.
 * @csr: Pointer to the COG and SOG, Rapid Update packet structure to populate.
 *
 * This function retrieves the current Course Over Ground (COG) and Speed Over
 * Ground (SOG) from the GPS data provided by gpsd and fills the provided COG
 * and SOG, Rapid Update packet structure. The COG is given in units of 1e-4
 * radians and the SOG in 1e-2 m/s.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int nmea2000_cog_sog_rapid_get_data(struct j1939_vp_srv_priv *priv,
				   struct nmea2000_cog_sog_rapid_packet *csr)
{
	uint16_t cog, sog;

	nmea2000_cog_sog_set_sid(csr, priv->sid);

	/* FIXME: set proper COG reference */
	nmea2000_cog_sog_set_cog_ref_res1(csr, NMEA2000_COG_REFERENCE_ERROR,
					  0x3f);

	csr->reserved2 = UINT16_MAX;

	if (!(priv->gps_data.set & TRACK_SET))
		cog = UINT16_MAX;
	else
		/* COG in 1e-4 radians */
		cog = priv->gps_data.fix.track * 10000;

	if (!(priv->gps_data.set & SPEED_SET))
		sog = UINT16_MAX;
	else
		/* SOG in 1e-2 m/s */
		sog = priv->gps_data.fix.speed * 100;


	nmea2000_cog_sog_set_cog(csr, cog);
	nmea2000_cog_sog_set_sog(csr, sog);

	return 0;
}

/**
 * nmea2000_cog_sog_rapid_prepare_data - Prepares the data for the COG and SOG,
 *					 Rapid Update packet.
 * @priv: Pointer to the server's private data structure.
 * @data: Pointer to the buffer where the packet data will be stored.
 *
 * This function calls the nmea2000_cog_sog_rapid_get_data function to populate
 * the COG and SOG, Rapid Update packet structure with the current GPS data and
 * stores it in the provided buffer.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int nmea2000_cog_sog_rapid_prepare_data(struct j1939_vp_srv_priv *priv,
					       void *data)
{
	struct nmea2000_cog_sog_rapid_packet *csr = data;

	return nmea2000_cog_sog_rapid_get_data(priv, csr);
}

/**
 * gpsd_system_to_nmea2000_gnss_system_type - Converts GPSD system type to
 *					      NMEA 2000 GNSS system type.
 * @system: The GPSD system type identifier.
 *
 * This function maps GPSD system type identifiers to corresponding NMEA 2000
 * GNSS system types.
 *
 * Return: The NMEA 2000 GNSS system type corresponding to the input GPSD
 * system type.
 */
static enum nmea2000_gnss_type
gpsd_system_to_nmea2000_gnss_system_type(int system)
{
	switch (system) {
	case NAVSYSTEM_GPS:
		return GNSS_TYPE_GPS;
	case NAVSYSTEM_GLONASS:
		return GNSS_TYPE_GLONASS;
	case NAVSYSTEM_GALILEO:
		return GNSS_TYPE_GALILEO;
	default:
		return GNSS_TYPE_GPS;
	}
}

/**
 * gpsd_mode_to_nmea2000_gnss_method - Converts GPSD mode to NMEA 2000 GNSS
 *				       method.
 * @mode: The GPSD mode identifier.
 *
 * This function translates the GPSD mode (such as no fix, 2D, 3D) to the
 * corresponding NMEA 2000 GNSS method.
 *
 * Return: The NMEA 2000 GNSS method corresponding to the input GPSD mode.
 */
static enum nmea2000_gnss_method gpsd_mode_to_nmea2000_gnss_method(int mode)
{
	switch (mode) {
	case MODE_NO_FIX:
		return GNSS_METHOD_NO_GNSS;
	case MODE_2D:
		return GNSS_METHOD_GNSS_FIX;
	case MODE_3D:
		return GNSS_METHOD_PRECISE_GNSS;
	default:
		return GNSS_METHOD_NO_GNSS;
	}
}

/**
 * nmea2000_gnss_position_data_get_data - Fills the GNSS Position Data packet
 *					  with current GPS data.
 * @priv: Pointer to the server's private data structure.
 * @gpdp: Pointer to the GNSS Position Data packet structure to populate.
 *
 * This function retrieves GNSS position data, including date, time, latitude,
 * longitude, altitude, and various other GNSS-related parameters. The data is
 * obtained from the gpsd interface and converted to the appropriate units and
 * formats for the NMEA 2000 protocol.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int
nmea2000_gnss_position_data_get_data(struct j1939_vp_srv_priv *priv,
				struct nmea2000_gnss_position_data_packet *gpdp)
{
	uint64_t latitude, longitude, altitude;
	enum nmea2000_gnss_method gnss_method;
	uint16_t nmea2000_date, hdop, pdop;
	enum nmea2000_gnss_type gnss_type;
	uint32_t nmea2000_time;

	nmea2000_gnss_set_sid(gpdp, priv->sid);

	/* FIXME: find out, what should be used for not supported fields -
	 * UINT*_MAX or INT*_MAX
	 */
	if (priv->gps_data.set & TIME_SET) {
		timespec_to_nmea2000_datetime(&priv->gps_data.fix.time,
					      &nmea2000_date, &nmea2000_time);

	} else {
		nmea2000_date = UINT16_MAX;
		nmea2000_time = UINT32_MAX;
	}

	nmea2000_gnss_set_date(gpdp, nmea2000_date);
	nmea2000_gnss_set_time(gpdp, nmea2000_time);

	if (priv->gps_data.set & LATLON_SET) {
		latitude = priv->gps_data.fix.latitude * 1e16;
		longitude = priv->gps_data.fix.longitude * 1e16;
	} else {
		latitude = INT64_MAX;
		longitude = INT64_MAX;
	}

	nmea2000_gnss_set_latitude(gpdp, latitude);
	nmea2000_gnss_set_longitude(gpdp, longitude);

	if (priv->gps_data.set & ALTITUDE_SET) {
		altitude = priv->gps_data.fix.altitude * 1e6;
	} else {
		altitude = INT64_MAX;
	}

	nmea2000_gnss_set_altitude(gpdp, altitude);

	/* FIXME: This is hardcoded to GPS for now. Need to add support for
	 * other systems.
	 */
	gnss_type = gpsd_system_to_nmea2000_gnss_system_type(NAVSYSTEM_GPS);

	if (priv->sim_mode)
		gnss_method = GNSS_METHOD_SIMULATE_MODE;
	else
		gnss_method =
			gpsd_mode_to_nmea2000_gnss_method(priv->gps_data.fix.mode);

	nmea2000_set_gnss_info(gpdp, gnss_type, gnss_method);

	/* FIXME: no integrity checking is implemented */
	nmea2000_set_status(gpdp, NMEA2000_INTEGRITY_NO_CHECKING, 0xff);

	nmea2000_gnss_set_num_svs(gpdp, priv->gps_data.satellites_visible);

	if (priv->gps_data.set & DOP_SET) {
		hdop = priv->gps_data.dop.hdop * 100;
		pdop = priv->gps_data.dop.pdop * 100;
	} else {
		hdop = INT16_MAX;
		pdop = INT16_MAX;
	}

	nmea2000_gnss_set_hdop(gpdp, hdop);
	nmea2000_gnss_set_pdop(gpdp, pdop);

	/* FIXME: use proper values for following fields: */
	nmea2000_gnss_set_geoidal_separation(gpdp, INT32_MAX);
	nmea2000_gnss_set_num_ref_stations(gpdp, 0);

	return 0;
}

/**
 * nmea2000_gnss_position_data_prepare_data - Prepares the data for the GNSS
 *					      Position Data packet.
 * @priv: Pointer to the server's private data structure.
 * @data: Pointer to the buffer where the packet data will be stored.
 *
 * This function calls nmea2000_gnss_position_data_get_data to populate the
 * GNSS Position Data packet structure with the current GPS data and stores it
 * in the provided buffer.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int
nmea2000_gnss_position_data_prepare_data(struct j1939_vp_srv_priv *priv,
					 void *data)
{
	struct nmea2000_gnss_position_data_packet *gpdp = data;

	return nmea2000_gnss_position_data_get_data(priv, gpdp);
}

/* ----------------- PGN handlers end ----------------- */

/**
 * prepare_and_send_message - Handles data preparation and transmission for a
 *			      PGN handler.
 * @priv: Pointer to the server's private data structure.
 * @handler: Pointer to the PGN handler with the necessary data functions.
 *
 * This function is responsible for preparing the data associated with a specific
 * PGN handler and sending it via the corresponding socket.
 *
 * Return: 0 on success or a negative error code if an issue occurs.
 */
static int prepare_and_send_message(struct j1939_vp_srv_priv *priv,
                                    struct j1939_pgn_handler *handler)
{
	/* Data size is limited to 256 bytes. Probably, it is too much for
	 * most of the cases.
	 */
	uint8_t data[256];
	int ret;

	if (sizeof(data) < handler->data_size) {
		pr_warn("Data buffer too small for PGN %u: %zu < %zu\n",
			handler->pgn, sizeof(data), handler->data_size);
		return -EINVAL;
	}

	memset(data, 0, handler->data_size);
	ret = handler->prepare_data(priv, data);
	if (ret < 0) {
		pr_warn("Failed to prepare data for PGN %u: %i\n",
			handler->pgn, ret);
		return ret;
	}

	ret = send(handler->sock, data, handler->data_size, MSG_DONTWAIT);
	if (ret == -1) {
		ret = -errno;
		pr_warn("Failed to send data for PGN %u: %i (%s)\n",
			handler->pgn, ret, strerror(-ret));
		return ret;
	}

	return 0;
}

/**
 * j1939_vp_srv_process_pgn_request - Processes a PGN request message.
 * @priv: Pointer to the server's private data structure.
 * @msg: Pointer to the received J1939 VP message.
 *
 * This function processes a PGN request message and sends the corresponding
 * PGN message back to the requester. It iterates over the available PGN
 * handlers to find the appropriate one based on the requested PGN.
 *
 * Return: 0 on success or a negative error code if an issue occurs.
 */
static int j1939_vp_srv_process_pgn_request(struct j1939_vp_srv_priv *priv,
					    struct j1939_vp_msg *msg)
{
	uint32_t requested_pgn = (msg->buf[2] << 16) | (msg->buf[1] << 8) |
				  msg->buf[0];
	bool gps_data_updated = false;
	int ret = -EINVAL;

	/* Iterate over all handlers to find the appropriate PGN */
	for (size_t i = 0; i < priv->num_handlers; ++i) {
		struct j1939_pgn_handler *handler = &priv->handlers[i];

		if (handler->pgn != requested_pgn)
			continue;

		if (!(priv->profile & handler->profile))
			continue;

		if (!gps_data_updated) {
			ret = update_gps_data(priv);
			if (ret < 0) {
				pr_warn("failed to update GPS data: %i\n", ret);
				return ret;
			}
			gps_data_updated = true;
		}

		ret = prepare_and_send_message(priv, handler);
		if (ret < 0) {
			pr_warn("Handler for PGN %u returned error %d\n",
				handler->pgn, ret);
		}
		return ret;
	}

	pr_warn("No handler found for PGN %u\n", requested_pgn);
	return ret;
}

/**
 * j1939_vp_srv_rx_buf - Processes a received J1939 message.
 * @priv: Pointer to the server's private data structure.
 * @msg: Pointer to the received J1939 VP message.
 *
 * This function processes a received J1939 message by checking the PGN
 * and calling the appropriate handler function.
 * Currently, only the PGN_REQUEST_PGN message is supported, and other
 * messages are ignored with a warning message.
 *
 * Return: 0 on success or a negative error code if an issue occurs.
 */
static int j1939_vp_srv_rx_buf(struct j1939_vp_srv_priv *priv,
			       struct j1939_vp_msg *msg)
{
	pgn_t pgn = msg->peername.can_addr.j1939.pgn;
	int ret = 0;

	switch (pgn) {
	case J1939_PGN_REQUEST_PGN:
		ret = j1939_vp_srv_process_pgn_request(priv, msg);
		break;
	default:
		pr_warn("%s: unsupported PGN: %x\n", __func__, pgn);
		/* Not a critical error */
		break;
	}

	return ret;
}

/**
 * j1939_vp_srv_rx_one - Receives a single J1939 message from a socket.
 * @priv: Pointer to the server's private data structure.
 * @sock: The file descriptor of the socket to receive from.
 *
 * This function receives a single J1939 message from the specified socket
 * and processes it by calling j1939_vp_srv_rx_buf.
 *
 * Return: 0 on success or a negative error code if an issue occurs.
 */
static int j1939_vp_srv_rx_one(struct j1939_vp_srv_priv *priv, int sock)
{
	struct j1939_vp_msg msg = {0};
	int flags = 0;
	int ret;

	msg.buf_size = J1939_VP1_MAX_TRANSFER_LENGH;
	msg.peer_addr_len = sizeof(msg.peername);
	msg.sock = sock;

	ret = recvfrom(sock, &msg.buf[0], msg.buf_size, flags,
		       (struct sockaddr *)&msg.peername, &msg.peer_addr_len);

	if (ret < 0) {
		ret = -errno;
		pr_warn("recvfrom() failed: %i %s\n", ret, strerror(-ret));
		return ret;
	}

	if (ret < 3) {
		pr_warn("received too short message: %i\n", ret);
		return -EINVAL;
	}

	msg.len = ret;

	ret = j1939_vp_srv_rx_buf(priv, &msg);
	if (ret < 0) {
		pr_warn("failed to process rx buf: %i (%s)\n", ret,
			strerror(ret));
		return ret;
	}

	return 0;
}

/**
 * j1939_vp_srv_handle_events - Handles events for the J1939 VP server.
 * @priv: Pointer to the server's private data structure.
 * @nfds: The number of file descriptors to handle.
 *
 * This function processes events for the J1939 VP server by checking for
 * incoming messages on the sockets and calling the appropriate handler
 * functions.
 *
 * Return: 0 on success or a negative error code if an issue occurs.
 */
static int j1939_vp_srv_handle_events(struct j1939_vp_srv_priv *priv,
				      unsigned int nfds)
{
	int ret;
	unsigned int n;

	for (n = 0; n < nfds && n < priv->cmn.epoll_events_size; ++n) {
		struct epoll_event *ev = &priv->cmn.epoll_events[n];

		if (!ev->events) {
			warn("no events");
			continue;
		}

		if (ev->events & POLLIN) {
			ret = j1939_vp_srv_rx_one(priv, ev->data.fd);
			if (ret) {
				warn("recv one");
				return ret;
			}
		}
	}
	return 0;
}

/**
 * determine_earliest_next_send_time - Determines the earliest next send time.
 * @priv: Pointer to the server's private data structure.
 *
 * This function determines the earliest next send time for all PGN handlers
 * based on their repetition rates and last send times.
 *
 * Return: the earliest time as a struct timespec.
 */
static struct timespec
determine_earliest_next_send_time(struct j1939_vp_srv_priv *priv)
{
	struct timespec earliest = {0, 0};

	for (size_t i = 1; i < priv->num_handlers; ++i) {
		if (!(priv->profile & priv->handlers[i].profile))
			continue;

		if (earliest.tv_sec == 0 && earliest.tv_nsec == 0)
			earliest = priv->handlers[i].next_time;

		if ((priv->handlers[i].next_time.tv_sec < earliest.tv_sec) ||
		    (priv->handlers[i].next_time.tv_sec == earliest.tv_sec &&
		     priv->handlers[i].next_time.tv_nsec < earliest.tv_nsec))
			earliest = priv->handlers[i].next_time;
	}

	return earliest;
}

/**
 * send_message_for_handler - Sends a periodic message for a PGN handler.
 * @priv: Pointer to the server's private data structure.
 * @handler: Pointer to the PGN handler structure.
 *
 * This function sends a periodic message for a specific PGN handler based on
 * the repetition rate and jitter. It calculates the time difference between
 * the last and next send times and sends the message if the time is within
 * the jitter range. It updates the last and next send times for the handler.
 *
 * Returns 0 on success or a negative error code if an issue occurs.
 */
static int send_message_for_handler(struct j1939_vp_srv_priv *priv,
				    struct j1939_pgn_handler *handler)
{
	int64_t time_diff;
	int ret;

	if (!(priv->profile & handler->profile))
		return 0;

	time_diff = timespec_diff_ms(&handler->next_time, &priv->cmn.last_time);
	if (time_diff > handler->jitter_ms)
		return 0;

	ret = prepare_and_send_message(priv, handler);
	if (ret < 0)
		return ret;

	handler->last_time = priv->cmn.last_time;
	handler->next_time = priv->cmn.last_time;
	timespec_add_ms(&handler->next_time, handler->repetition_rate_ms);

	return 0;
}

/**
 * send_periodic_messages - Sends periodic messages for all PGN handlers.
 * @priv: Pointer to the server's private data structure.
 *
 * This function sends periodic messages for all PGN handlers based on their
 * repetition rates and jitter. It iterates over all handlers and calls the
 * send_message_for_handler function to send the messages.
 *
 * Return: 0 on success or a negative error code if an issue occurs.
 */
static int send_periodic_messages(struct j1939_vp_srv_priv *priv)
{
	int ret = 0;

	for (size_t i = 0; i < priv->num_handlers; ++i) {
		ret = send_message_for_handler(priv, &priv->handlers[i]);
		if (ret < 0) {
			pr_warn("Failed to send periodic message for handler %zu. Error: %d (%s)\n",
				i, ret, strerror(-ret));
		}
	}

	return ret;
}

/**
 * j1939_vp_srv_process_events_and_tasks - Processes events and tasks for the
 *                                         J1939 VP server.
 * @priv: Pointer to the server's private data structure.
 *
 * This function processes events and tasks for the J1939 VP server by preparing
 * for events, handling events, updating GPS data, and sending periodic
 * messages.
 *
 * Return: 0 on success or a negative error code if an issue occurs.
 */
static int j1939_vp_srv_process_events_and_tasks(struct j1939_vp_srv_priv *priv)
{
	int64_t time_diff;
	int ret, nfds;

	priv->cmn.next_send_time = determine_earliest_next_send_time(priv);
	ret = libj1939_prepare_for_events(&priv->cmn, &nfds, false);
	if (ret)
		pr_err("failed to prepare for events: %i (%s)\n", ret,
		       strerror(-ret));

	if (!ret && nfds > 0) {
		ret = j1939_vp_srv_handle_events(priv, nfds);
		if (ret)
			pr_err("failed to handle events: %i (%s)\n", ret,
			       strerror(-ret));
	}

	/* Test if it is proper time to send next status message. */
	time_diff = timespec_diff_ms(&priv->cmn.next_send_time,
				     &priv->cmn.last_time);
	if (time_diff > 0) {
		/* Too early to send next message */
		return 0;
	}

	ret = update_gps_data(priv);
	if (ret < 0 && ret != -EAGAIN)
		pr_warn("failed to update GPS data: %i\n", ret);

	return send_periodic_messages(priv);
}

static struct j1939_pgn_handler pgn_handlers[] = {
	/* SAE J1939 specific PGNs */
	{
		.pgn = J1939_PGN_VP1,
		.prepare_data = j1939_vp1_prepare_data,
		.sock_priority = J1939_VP1_PRIO_DEFAULT,
		.repetition_rate_ms = J1939_VP1_REPETITION_RATE_MS,
		.jitter_ms = J1939_VP1_JITTER_MS,
		.data_size = sizeof(struct j1939_vp1_packet),
		.profile = PROFILE_J1939,
	},
	{
		.pgn = J1939_PGN_VP2,
		.prepare_data = j1939_vp2_prepare_data,
		.sock_priority = J1939_VP2_PRIO_DEFAULT,
		.repetition_rate_ms = J1939_VP2_REPETITION_RATE_MS,
		.jitter_ms = J1939_VP2_JITTER_MS,
		.data_size = sizeof(struct j1939_vp2_packet),
		.profile = PROFILE_J1939,
	},
	/* NMEA 2000 specific PGNs */
	{
		.pgn = NMEA2000_PGN_SYS_TIME,
		.prepare_data = nmea2000_sys_time_prepare_data,
		.sock_priority = NMEA2000_SYS_TIME_PRIO_DEFAULT,
		.repetition_rate_ms = NMEA2000_SYS_TIME_REPETITION_RATE_MS,
		.jitter_ms = NMEA2000_SYS_TIME_JITTER_MS,
		.data_size = NMEA2000_SYS_TIME_MAX_TRANSFER_LENGTH,
		.profile = PROFILE_NMEA2000,
	},
	{
		.pgn = NMEA2000_PGN_MAG_VAR,
		.prepare_data = nmea2000_mag_var_prepare_data,
		.sock_priority = NMEA2000_MAG_VAR_PRIO_DEFAULT,
		.repetition_rate_ms = NMEA2000_MAG_VAR_REPETITION_RATE_MS,
		.jitter_ms = NMEA2000_MAG_VAR_JITTER_MS,
		.data_size = NMEA2000_MAG_VAR_MAX_TRANSFER_LENGTH,
		.profile = 0, /* currently we can't provide this data */
	},
	{
		.pgn = NMEA2000_PGN_POSITION_RAPID,
		.prepare_data = nmea2000_position_rapid_prepare_data,
		.sock_priority = NMEA2000_POSITION_RAPID_PRIO_DEFAULT,
		.repetition_rate_ms = NMEA2000_POSITION_RAPID_REPETITION_RATE_MS,
		.jitter_ms = NMEA2000_POSITION_RAPID_JITTER_MS,
		.data_size = NMEA2000_POSITION_RAPID_MAX_TRANSFER_LENGTH,
		.profile = PROFILE_NMEA2000,
	},
	{
		.pgn = NMEA2000_PGN_COG_SOG_RAPID,
		.prepare_data = nmea2000_cog_sog_rapid_prepare_data,
		.sock_priority = NMEA2000_COG_SOG_RAPID_PRIO_DEFAULT,
		.repetition_rate_ms = NMEA2000_COG_SOG_RAPID_REPETITION_RATE_MS,
		.jitter_ms = NMEA2000_COG_SOG_RAPID_JITTER_MS,
		.data_size = NMEA2000_COG_SOG_RAPID_MAX_TRANSFER_LENGTH,
		.profile = PROFILE_NMEA2000,
	},
	{
		.pgn = NMEA2000_PGN_GNSS_POSITION_DATA,
		.prepare_data = nmea2000_gnss_position_data_prepare_data,
		.sock_priority = NMEA2000_GNSS_POSITION_DATA_PRIO_DEFAULT,
		.repetition_rate_ms = NMEA2000_GNSS_POSITION_DATA_REPETITION_RATE_MS,
		.jitter_ms = NMEA2000_GNSS_POSITION_DATA_JITTER_MS,
		.data_size = NMEA2000_GNSS_POSITION_DATA_MAX_TRANSFER_LENGTH,
		.profile = PROFILE_NMEA2000,
	},
};

/**
 * initialize_socket_for_handler - Initializes a socket for a PGN handler.
 * @priv: Pointer to the server's private data structure.
 * @handler: Pointer to the PGN handler structure.
 *
 * This function initializes a socket for a specific PGN handler by opening,
 * binding, and connecting the socket. It sets the socket priority, enables
 * broadcast, and adds the socket to the epoll instance for event handling.
 *
 * Return: 0 on success or a negative error code if an issue occurs.
 */
static int initialize_socket_for_handler(struct j1939_vp_srv_priv *priv,
					 struct j1939_pgn_handler *handler)
{
	struct sockaddr_can addr = priv->sockname;
	int ret;

	ret = libj1939_open_socket();
	if (ret < 0) {
		pr_err("Failed to open socket for PGN %u: %d\n",
		       handler->pgn, ret);
		return ret;
	}

	handler->sock = ret;

	ret = libj1939_bind_socket(handler->sock, &addr);
	if (ret < 0) {
		pr_err("Failed to bind socket for PGN %u: %d\n",
		       handler->pgn, ret);
		return ret;
	}

	ret = libj1939_socket_prio(handler->sock, handler->sock_priority);
	if (ret < 0) {
		pr_err("Failed to set socket priority for PGN %u: %d\n",
		       handler->pgn, ret);
		return ret;
	}

	ret = libj1939_set_broadcast(handler->sock);
	if (ret < 0) {
		pr_err("Failed to set broadcast for PGN %u: %d\n",
		       handler->pgn, ret);
		return ret;
	}

	addr.can_addr.j1939.name = J1939_NO_NAME;
	addr.can_addr.j1939.addr = J1939_NO_ADDR;
	addr.can_addr.j1939.pgn = handler->pgn;
	ret = libj1939_connect_socket(handler->sock, &addr);
	if (ret < 0) {
		pr_err("Failed to connect socket for PGN %u: %d\n",
		       handler->pgn, ret);
		return ret;
	}

	ret = libj1939_add_socket_to_epoll(priv->cmn.epoll_fd, handler->sock,
					   EPOLLIN);
	if (ret < 0) {
		pr_err("Failed to add socket to epoll for PGN %u: %d\n",
		       handler->pgn, ret);
		return ret;
	}

	return 0;
}

/**
 * j1939_vp_srv_init - Initializes the J1939 VP server.
 * @priv: Pointer to the server's private data structure.
 *
 * This function sets up the necessary resources for the J1939 VP server,
 * including creating an epoll instance, allocating memory for epoll events,
 * and initializing sockets for each PGN handler. It registers the handlers,
 * assigns their sockets, and sets up initial timing information.
 *
 * Return: 0 on success or a negative error code if an issue occurs.
 */
static int j1939_vp_srv_init(struct j1939_vp_srv_priv *priv)
{
	struct timespec ts;
	int ret;

	ret = libj1939_create_epoll();
	if (ret < 0) {
		pr_err("Failed to create epoll: %d\n", ret);
		return ret;
	}

	priv->cmn.epoll_fd = ret;
	priv->cmn.epoll_events = calloc(J1939_VP_SRV_MAX_EPOLL_EVENTS,
					sizeof(struct epoll_event));
	if (!priv->cmn.epoll_events) {
		pr_err("Failed to allocate memory for epoll events\n");
		return -ENOMEM;
	}
	priv->cmn.epoll_events_size = J1939_VP_SRV_MAX_EPOLL_EVENTS;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret < 0) {
		ret = -errno;
		pr_err("Failed to get current time: %d (%s)\n", ret,
		       strerror(-ret));
		return ret;
	}

	priv->handlers = pgn_handlers;
	priv->num_handlers = sizeof(pgn_handlers) / sizeof(pgn_handlers[0]);

	for (size_t i = 0; i < priv->num_handlers; ++i) {
		struct j1939_pgn_handler *handler = &priv->handlers[i];

		handler->sock = -1;
		if (!(priv->profile & handler->profile))
			continue;

		ret = initialize_socket_for_handler(priv, handler);
		if (ret < 0) {
			pr_err("Failed to initialize socket for handler %zu: %d\n",
			       i, ret);
			return ret;
		}

		handler->next_time = ts;
	}

	return 0;
}

static void j1939_vp_srv_print_help(void)
{
	printf("j1939-vehicle-position-srv - J1939 Vehicle Position Server\n");
	printf("\n");
	printf("This program acts as a J1939 Vehicle Position Server, sending J1939 or NMEA 2000\n");
	printf("messages with vehicle position data. It reads GPS data from gpsd and sends it\n");
	printf("periodically to the specified CAN interface.\n");
	printf("\n");
	printf("Supported PGNs:\n");
	printf("  J1939:\n");
	printf("    - Vehicle Position 1 (PGN 65265)\n");
	printf("    - Vehicle Position 2 (PGN 65266)\n");
	printf("  NMEA 2000:\n");
	printf("    - System Time (PGN 126992)\n");
	printf("    - Position, Rapid Update (PGN 129025)\n");
	printf("    - COG and SOG, Rapid Update (PGN 129026)\n");
	printf("    - GNSS Position Data (PGN 129029)\n");
	printf("\n");
	printf("Usage: j1939-vehicle-position-srv [options]\n");
	printf("Options:\n");
	printf("  --interface <interface_name> or -i <interface_name>\n");
	printf("      Specifies the CAN interface to use (mandatory).\n");
	printf("  --local-address <local_address_hex> or -a <local_address_hex>\n");
	printf("      Specifies the local address in hexadecimal (mandatory if\n");
	printf("      local name is not provided).\n");
	printf("  --local-name <local_name_hex> or -n <local_name_hex>\n");
	printf("      Specifies the local NAME in hexadecimal (mandatory if\n");
	printf("      local address is not provided).\n");
	printf("\n");
	printf("Note: Local address and local name are mutually exclusive and one\n");
	printf("      must be provided.\n");
	printf("\n");
	printf("  --sim-mode or -s\n");
	printf("    Enables simulation mode to generate position data instead of using real GPSd data.\n");
	printf("\n");
	printf("  --profile <profile_name> or -p <profile_name>\n");
	printf("    Selects the profile for protocol-specific behavior. Available profiles:\n");
	printf("    - 'j1939': Configures for J1939 protocol, used in heavy-duty vehicles.\n");
	printf("    - 'nmea2000': Configures for NMEA 2000 protocol, used in marine electronics.\n");
	printf("\n");
	printf("Usage Examples:\n");
	printf("  Using local address:\n");
	printf("    j1939-vehicle-position-srv -i vcan0 -a 0x90\n");
	printf("\n");
	printf("  Using local NAME:\n");
	printf("    j1939acd -r 64-95 -c /tmp/1122334455667789.jacd 1122334455667789 vcan0 &\n");
	printf("    j1939-vehicle-position-srv -i vcan0 -n 0x1122334455667789\n");
}

/**
 * j1939_vp_srv_parse_args - Parses command line arguments for the J1939 VP server.
 * @priv: Pointer to the server's private data structure.
 * @argc: The number of command line arguments.
 * @argv: The array of command line arguments.
 *
 * This function parses the command line arguments for the J1939 VP server,
 * including the interface, local address, local name, and simulation mode.
 * It sets the corresponding values in the private data structure.
 *
 * Return: 0 on success or a negative error code if an issue occurs.
 */
static int j1939_vp_srv_parse_args(struct j1939_vp_srv_priv *priv,
				   int argc, char *argv[])
{
	struct sockaddr_can *local = &priv->sockname;
	bool local_address_set = false;
	bool local_name_set = false;
	bool interface_set = false;
	int long_index = 0;
	int opt;

	static struct option long_options[] = {
		{"interface", required_argument, 0, 'i'},
		{"local-address", required_argument, 0, 'a'},
		{"local-name", required_argument, 0, 'n'},
		{"sim-mode", no_argument, 0, 's'},
		{"profile", required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "a:n:i:sp:", long_options, &long_index)) != -1) {
		switch (opt) {
		case 'a':
			local->can_addr.j1939.addr = strtoul(optarg, NULL, 16);
			local_address_set = true;
			break;
		case 'n':
			local->can_addr.j1939.name = strtoull(optarg, NULL, 16);
			local_name_set = true;
			break;
		case 'i':
			local->can_ifindex = if_nametoindex(optarg);
			if (!local->can_ifindex) {
				pr_err("Interface %s not found. Error: %d (%s)\n",
				       optarg, -errno, strerror(errno));
				return -EINVAL;
			}
			interface_set = true;
			break;
		case 's':
			priv->sim_mode = true;
			break;
		case 'p':
			if (strcmp(optarg, "j1939") == 0) {
				priv->profile |= PROFILE_J1939;
			} else if (strcmp(optarg, "nmea2000") == 0) {
				priv->profile |= PROFILE_NMEA2000;
			} else {
				pr_err("Unknown profile: %s\n", optarg);
				j1939_vp_srv_print_help();
				return -EINVAL;
			}
			break;
		default:
			j1939_vp_srv_print_help();
			return -EINVAL;
		}
	}

	if (priv->profile == 0) {
		pr_info("Profile not specified. Using default profile: j1939\n");
		priv->profile = PROFILE_J1939;
	}

	if (!interface_set) {
		pr_err("interface not specified\n");
		j1939_vp_srv_print_help();
		return -EINVAL;
	}

	if (local_address_set && local_name_set) {
		pr_err("local address and local name or remote address and remote name are mutually exclusive\n");
		j1939_vp_srv_print_help();
		return -EINVAL;
	}

	return 0;
}

/**
 * j1939_vp_close_handler_sockets - Closes the sockets for all PGN handlers.
 * @priv: Pointer to the server's private data structure.
 *
 * This function closes the sockets for all PGN handlers by iterating over
 * the handlers and closing the socket if it is open. It resets the socket
 * descriptor to an invalid value after closing.
 */
static void j1939_vp_close_handler_sockets(struct j1939_vp_srv_priv *priv)
{
	for (size_t i = 0; i < priv->num_handlers; ++i) {
		struct j1939_pgn_handler *handler = &priv->handlers[i];

		if (handler->sock >= 0) {
			close(handler->sock);
			handler->sock = -1;
		}
	}
}

/**
 * j1939_vp_srv_gps_open - Opens a connection to the GPS device.
 * @priv: Pointer to the server's private data structure.
 *
 * This function opens a connection to the GPS device using the GPSD library.
 * It connects to the GPSD daemon running on localhost and port 2947 and
 * enables the GPS stream in JSON format.
 *
 * Return: 0 on success or a negative error code if an issue occurs.
 */
static int j1939_vp_srv_gps_open(struct j1939_vp_srv_priv *priv)
{
	if (priv->sim_mode)
		return 0;

	if (gps_open("localhost", "2947", &priv->gps_data) != 0) {
		pr_err("No GPSD running or connection failed.\n");
		return 1;
	}

	gps_stream(&priv->gps_data, WATCH_ENABLE | WATCH_JSON, NULL);

	return 0;
}

/**
 * j1939_vp_srv_gps_close - Closes the connection to the GPS device.
 * @priv: Pointer to the server's private data structure.
 *
 * This function closes the connection to the GPS device by disabling the GPS
 * stream and closing the GPSD connection.
 */
static void j1939_vp_srv_gps_close(struct j1939_vp_srv_priv *priv)
{
	if (priv->sim_mode)
		return;

	gps_stream(&priv->gps_data, WATCH_DISABLE, NULL);
	gps_close(&priv->gps_data);
}

static void j1939_vp_srv_close(struct j1939_vp_srv_priv *priv)
{
	j1939_vp_close_handler_sockets(priv);

	close(priv->cmn.epoll_fd);
	free(priv->cmn.epoll_events);
}

int main(int argc, char *argv[])
{
	struct j1939_vp_srv_priv *priv;
	int ret;

	priv = malloc(sizeof(*priv));
	if (!priv)
		err(EXIT_FAILURE, "can't allocate priv");

	bzero(priv, sizeof(*priv));

	libj1939_init_sockaddr_can(&priv->sockname, J1939_PGN_REQUEST_PGN);

	ret = j1939_vp_srv_parse_args(priv, argc, argv);
	if (ret)
		return ret;

	ret = j1939_vp_srv_init(priv);
	if (ret) {
		pr_err("failed to initialize: %i (%s)\n", ret, strerror(-ret));
		return ret;
	}

	ret = j1939_vp_srv_gps_open(priv);
	if (ret)
		return ret;

	while (1) {
		ret = j1939_vp_srv_process_events_and_tasks(priv);
		/* Even if error we continue to do our best. But we need to
		 * slow down to avoid busy loop. So, we sleep for a while.
		 */
		if (ret) {
			pr_warn("failed to process events and tasks: %i (%s). Sleeping for a while\n",
				ret, strerror(-ret));
			sleep(1);
		}
	}

	j1939_vp_srv_gps_close(priv);

	j1939_vp_srv_close(priv);
	free(priv);

	return ret;
}

