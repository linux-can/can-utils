// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include "isobusfs_cli.h"
#include "isobusfs_cmn.h"
#include "isobusfs_cmn_fa.h"

size_t current_test;
bool test_running;
struct timespec test_start_time;

struct isobusfs_cli_test_case {
	int (*test_func)(struct isobusfs_priv *priv, bool *complete);
	const char *test_description;
};

static int isobusfs_cli_test_connect(struct isobusfs_priv *priv, bool *complete)
{
	struct timespec current_time;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &current_time);

	switch (priv->state) {
	case ISOBUSFS_CLI_STATE_SELFTEST:
		test_start_time = current_time;
		priv->state = ISOBUSFS_CLI_STATE_CONNECTING;
		/* fall through */
	case ISOBUSFS_CLI_STATE_CONNECTING:
		if (priv->fs_is_active) {
			*complete = true;
			break;
		}

		if (current_time.tv_sec - test_start_time.tv_sec >= 5) {
			ret = -ETIMEDOUT;
			goto test_fail;
		}
		break;
	default:
		pr_err("%s:%i: unknown state: %d", __func__, __LINE__, priv->state);
		ret = -EINVAL;
		goto test_fail;
	}

	return 0;

test_fail:
	/* without server all other tests make no sense */
	priv->run_selftest = false;
	*complete = true;

	return ret;
}

static int isobusfs_cli_test_property_req(struct isobusfs_priv *priv, bool *complete)
{
	struct timespec current_time;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &current_time);

	switch (priv->state) {
	case ISOBUSFS_CLI_STATE_SELFTEST:
		test_start_time = current_time;

		ret = isobusfs_cli_property_req(priv);
		if (ret)
			goto test_fail;

		break;
	case ISOBUSFS_CLI_STATE_WAIT_FS_PROPERTIES:
		if (current_time.tv_sec - test_start_time.tv_sec >= 5) {
			ret = -ETIMEDOUT;
			goto test_fail;
		}
		break;
	case ISOBUSFS_CLI_STATE_GET_FS_PROPERTIES_DONE:
		*complete = true;
		break;
	default:
		pr_err("%s:%i: unknown state: %d", __func__, __LINE__, priv->state);
		ret = -EINVAL;
		goto test_fail;
	}

	return 0;

test_fail:
	*complete = true;

	return ret;
}

static int isobusfs_cli_test_volume_status_req(struct isobusfs_priv *priv, bool *complete)
{
	static const char volume_name[] = "\\\\vol1";
	struct timespec current_time;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &current_time);

	switch (priv->state) {
	case ISOBUSFS_CLI_STATE_SELFTEST:
		test_start_time = current_time;

		ret = isobusfs_cli_volume_status_req(priv, 0,
				       sizeof(volume_name) - 1, volume_name);
		if (ret)
			goto test_fail;

		break;
	case ISOBUSFS_CLI_STATE_WAIT_VOLUME_STATUS:
		if (current_time.tv_sec - test_start_time.tv_sec >= 5) {
			ret = -ETIMEDOUT;
			goto test_fail;
		}
		break;
	case ISOBUSFS_CLI_STATE_VOLUME_STATUS_DONE:
		*complete = true;
		break;
	default:
		pr_err("%s:%i: unknown state: %d", __func__, __LINE__, priv->state);
		ret = -EINVAL;
		goto test_fail;
	}

	return 0;

test_fail:
	*complete = true;

	return ret;
}

static int isobusfs_cli_test_current_dir_req(struct isobusfs_priv *priv,
					     bool *complete)
{
	struct timespec current_time;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &current_time);

	switch (priv->state) {
	case ISOBUSFS_CLI_STATE_SELFTEST:
		test_start_time = current_time;

		ret = isobusfs_cli_get_current_dir_req(priv);
		if (ret)
			goto test_fail;
		break;
	case ISOBUSFS_CLI_STATE_WAIT_CURRENT_DIR:
		if (current_time.tv_sec - test_start_time.tv_sec >= 5) {
			ret = -ETIMEDOUT;
			goto test_fail;
		}
		break;
	case ISOBUSFS_CLI_STATE_GET_CURRENT_DIR_DONE:
		*complete = true;
		break;
	default:
		pr_err("%s:%i: unknown state: %d", __func__, __LINE__, priv->state);
		ret = -EINVAL;
		goto test_fail;
	}

	return 0;

test_fail:
	*complete = true;

	return ret;
}

struct isobusfs_cli_test_dir_path {
	const char *dir_name;
	bool expect_pass;
};

static struct isobusfs_cli_test_dir_path test_dir_patterns[] = {
	/* expected result \\vol1\dir1\ */
	{ "\\\\vol1\\dir1", true },
	/* expected result \\vol1\dir1\dir2\ */
	{ "\\\\vol1\\dir1\\dir2", true },
	/* expected result \\vol1\dir1\dir2\dir3\dir4\ */
	{ ".\\dir3\\dir4", true },
	/* expected result \\vol1\dir1\dir2\dir3\dir5\ */
	{ "..\\dir5", true },
	/* expected result \\vol1\ */
	{ "..\\..\\..\\..\\..\\..\\vol1", true },
	/* expected result \\vol1\~\ */
	{ "~\\", true },
	/* expected result \\vol1\~\msd_dir1\msd_dir2\ */
	{ "~\\msd_dir1\\msd_dir2", true },
	/* expected result \\vol1\~\ */
	{ "\\\\vol1\\~\\", true },
	/* expected result \\vol1\~\msd_dir1\msd_dir2\ */
	{ "\\\\vol1\\~\\msd_dir1\\msd_dir2", true },
	/* expected result \\vol1\~\msd_dir1\msd_dir2\~\ */
	{ ".\\~\\", true },
	/* expected result \\vol1\~\msd_dir1\msd_dir2\~\~tilde_dir */
	{ "~tilde_dir", true },
	/* expected result \\vol1\dir1\~\ */
	{ "\\\\vol1\\dir1\\~", true },
	/* expected result \\vol1\~\ not clear if it is manufacture specific dir */
	{ "\\~\\", true },
	/* expected result \\~\ */
	{ "\\\\~\\", false },
	/* expected result: should fail */
	{ "\\\\\\\\\\\\\\\\", false },
	/* Set back to dir1 for other test. Expected result \\vol1\dir1\ */
	{ "\\\\vol1\\dir1", true },

	/* Initialize server path to root: Expected initial state: root */
	{ "\\\\vol1\\dir1", true }, /* Set server path to \\vol1\dir1\ */

	/* Test absolute paths: Expected state: \\vol1\dir1\ */
	{ "\\\\vol1\\dir1\\dir2", true }, /* Changes to \\vol1\dir1\dir2\ */
	{ "\\\\vol1\\dir1", true }, /* Changes back to \\vol1\dir1\ */

	/* Test relative path .\ : Expected state: \\vol1\dir1\ */
	{ ".\\dir2\\dir3\\dir4", true }, /* Changes to \\vol1\dir1\dir2\dir3\dir4\ */
	{ "..\\dir5", true }, /* Changes to \\vol1\dir1\dir2\dir3\dir5\ */
	{ "..\\..\\..\\..\\..\\..\\vol1", true }, /* Changes to \\vol1\ */
	{ ".\\dir1\\dir2", true }, /* Changes back to \\vol1\dir1\dir2 */

	/* Test relative path ..\ with multiple backslashes:
	 * Expected state: \\vol1\dir1\dir2\
	 */
	{ "..\\\\\\", true }, /* Changes to \\vol1\dir1\ */
	{ ".\\dir2", true }, /* Changes back to \\vol1\dir1\dir2\ */
	{ "..\\\\\\\\\\\\\\", true }, /* Changes to \\vol1\dir1\ */
	{ ".\\dir2", true }, /* Changes back to \\vol1\dir1\dir2 */
	{ "\\\\vol1\\dir1", true }, /* Changes back to \\vol1\dir1\ */

	/* Test relative path .\ with multiple backslashes: Expected state:
	 * \\vol1\dir1\
	 */
	{ ".\\\\\\", true }, /* Remains at \\vol1\dir1\ */
	{ ".\\dir2", true }, /* Changes back to \\vol1\dir1\dir2 */
	{ "..\\", true }, /* Changes to \\vol1\dir1\ */
	{ ".\\\\\\\\\\\\\\", true }, /* Remains at \\vol1\dir1\ */
	{ ".\\dir2", true }, /* Changes back to \\vol1\dir1\dir2 */
	{ "..\\", true }, /* Changes to \\vol1\dir1\ */

	/* Test navigating up and down: Expected state: \\vol1\dir1\ */
	{ "..\\..\\..\\..\\..\\..\\vol1", true }, /* Changes to \\vol1\ */

	/* prepare for tilde tests */
	{ "\\\\vol1\\", true }, /* Set server path to \\vol1\ */
	/* Tilde used correctly at the beginning of a path */
	{ "~\\", true }, /* Replace with the manufacturer-specific directory on
			  * the current volume
			  */
	/* Tilde used correctly after a volume name */
	{ "\\\\vol1\\~\\", true }, /* Replace ~ with the manufacturer-specific
				    * directory on vol1
				    */

	/* Tilde used in non-root locations, treated as a regular directory */
	{ "\\\\vol1\\dir1\\~", true }, /* Treated as a regular directory named
					* '~' under \\vol1\dir1\
					*/
	{ ".\\~\\", true }, /* Treated as a regular directory named '~' in the
			     * current directory: \\vol1\dir1\~\
			     */

	/* Tilde used with a specific manufacturer directory at the root */
	{ "~\\msd_dir1\\msd_dir2", true }, /* Replace ~ and append rest of the
					    * path on the current volume
					    */
	{ "\\\\vol1\\~\\msd_dir1\\msd_dir2", true },
					/* Replace ~ and append rest of the
					 * path on vol1
					 */
	{ ".\\~\\", true }, /* Treated as a regular directory named '~' in the
			     * current directory: \\vol1\~\msd_dir1\msd_dir2\~\
			     */
	{ "~tilde_dir", true }, /* Treated as a regular directory named
				 * '~tilde_dir' in the current directory:
				 * \\vol1\~\msd_dir1\msd_dir2\~\~tilde_dir\
				 */
	/* Invalid usage of tilde at non-root locations (as a
	 * manufacturer-specific directory)
	 */
	{ "\\~\\", false }, /* Incorrect usage of tilde at non-root, expected
			     * to fail
			     */ 
	{ "\\\\~\\", false }, /* Incorrect usage of tilde at non-root, expected
			       * to fail
			       */

	/* Test invalid or ambiguous paths: Expected state: \\vol1\dir1\ */
	{ "\\\\\\\\\\\\\\\\", false }, /* Invalid path, should fail */

	/* Set back to dir1 for other tests: Expected state: \\vol1\dir1\ */
	{ "\\\\vol1\\dir1", true }, /* Ensure server path is set to \\vol1\dir1\ */
};

size_t current_dir_pattern_test;

static int isobusfs_cli_test_ccd_req(struct isobusfs_priv *priv, bool *complete)
{
	size_t num_patterns = ARRAY_SIZE(test_dir_patterns);
	struct isobusfs_cli_test_dir_path *tp =
		&test_dir_patterns[current_dir_pattern_test];
	struct timespec current_time;
	bool fail = false;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &current_time);

	switch (priv->state) {
	case ISOBUSFS_CLI_STATE_SELFTEST:
		test_start_time = current_time;
		pr_info("Start pattern test: %s", tp->dir_name);
		ret = isobusfs_cli_ccd_req(priv, tp->dir_name,
					   strlen(tp->dir_name));
		if (ret)
			goto test_fail;
		break;
	case ISOBUSFS_CLI_STATE_WAIT_CCD_RESP:
		if (current_time.tv_sec - test_start_time.tv_sec >= 5) {
			ret = -ETIMEDOUT;
			goto test_fail;
		}
		break;
	case ISOBUSFS_CLI_STATE_CCD_FAIL:
		fail = true;
		/* fallthrough */
	case ISOBUSFS_CLI_STATE_CCD_DONE:
		if (tp->expect_pass && fail) {
			pr_err("pattern test failed: %s", tp->dir_name);
			ret = -EINVAL;
			goto test_fail;
		} else if (!tp->expect_pass && !fail) {
			pr_err("pattern test failed: %s", tp->dir_name);
			ret = -EINVAL;
			goto test_fail;
		}
		current_dir_pattern_test++;

		if (current_dir_pattern_test >= num_patterns) {
			*complete = true;
			break;
		}

		priv->state = ISOBUSFS_CLI_STATE_SELFTEST;
		break;
	default:
		pr_err("%s:%i: unknown state: %d", __func__, __LINE__, priv->state);
		ret = -EINVAL;
		goto test_fail;
	}

	return 0;

test_fail:
	*complete = true;
	return ret;
}

struct isobusfs_cli_test_of_path {
	const char *path_name;
	uint8_t flags;
	bool expect_pass;
};

static struct isobusfs_cli_test_of_path test_of_patterns[] = {
	/* expected result \\vol1\dir1\dir2\ */
	{ "\\\\vol1\\dir1\\dir2", 0, false },
	{ "\\\\vol1\\dir1\\dir2\\file0", 0, true },
};

size_t current_of_pattern_test;

static int isobusfs_cli_test_of_req(struct isobusfs_priv *priv, bool *complete)
{
	size_t num_patterns = ARRAY_SIZE(test_of_patterns);
	struct isobusfs_cli_test_of_path *tp =
		&test_of_patterns[current_of_pattern_test];
	struct timespec current_time;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &current_time);

	switch (priv->state) {
	case ISOBUSFS_CLI_STATE_SELFTEST:
		test_start_time = current_time;
		pr_info("Start pattern test: %s", tp->path_name);
		ret = isobusfs_cli_fa_of_req(priv, tp->path_name,
					     strlen(tp->path_name), tp->flags);
		if (ret)
			goto test_fail;

		break;
	case ISOBUSFS_CLI_STATE_WAIT_OF_RESP:
		if (current_time.tv_sec - test_start_time.tv_sec >= 5) {
			ret = -ETIMEDOUT;
			goto test_fail;
		}
		break;
	case ISOBUSFS_CLI_STATE_OF_FAIL:
		if (tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		priv->state = ISOBUSFS_CLI_STATE_TEST_DONE;
		break;
	case ISOBUSFS_CLI_STATE_OF_DONE:
		if (!tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		if (priv->handle != ISOBUSFS_FILE_HANDLE_ERROR)
			isobusfs_cli_fa_cf_req(priv, priv->handle);
		else
			priv->state = ISOBUSFS_CLI_STATE_TEST_DONE;

		break;
	case ISOBUSFS_CLI_STATE_CF_FAIL:
		pr_err("failed to close file: %s", tp->path_name);
		ret = -EINVAL;
		goto test_fail;
	case ISOBUSFS_CLI_STATE_CF_DONE:
	case ISOBUSFS_CLI_STATE_TEST_DONE:
		current_of_pattern_test++;

		if (current_of_pattern_test >= num_patterns)
			*complete = true;
		else
			priv->state = ISOBUSFS_CLI_STATE_SELFTEST;

		break;
	default:
		pr_err("%s:%i: unknown state: %d", __func__, __LINE__, priv->state);
		ret = -EINVAL;
		goto test_fail;
	}

	return 0;

test_fail:
	*complete = true;
	return ret;
}

struct isobusfs_cli_test_sf_path {
	const char *path_name;
	uint8_t flags;
	uint32_t offset;
	uint32_t read_size;
	bool expect_pass;
};

static struct isobusfs_cli_test_sf_path test_sf_patterns[] = {
	/* expected result \\vol1\dir1\dir2\ */
	{ "\\\\vol1\\dir1\\dir2\\file1k", 0, 0, 0, true },
	{ "\\\\vol1\\dir1\\dir2\\file1k", 0, 10, 0, true },
};

size_t current_sf_pattern_test;

static int isobusfs_cli_test_sf_req(struct isobusfs_priv *priv, bool *complete)
{
	size_t num_patterns = ARRAY_SIZE(test_sf_patterns);
	struct isobusfs_cli_test_sf_path *tp =
		&test_sf_patterns[current_sf_pattern_test];
	struct timespec current_time;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &current_time);

	switch (priv->state) {
	case ISOBUSFS_CLI_STATE_SELFTEST:
		test_start_time = current_time;
		pr_info("Start pattern test: %s", tp->path_name);
		ret = isobusfs_cli_fa_of_req(priv, tp->path_name,
					     strlen(tp->path_name), tp->flags);
		if (ret)
			goto test_fail;

		break;
	case ISOBUSFS_CLI_STATE_WAIT_OF_RESP:
		if (current_time.tv_sec - test_start_time.tv_sec >= 5) {
			ret = -ETIMEDOUT;
			goto test_fail;
		}
		break;
	case ISOBUSFS_CLI_STATE_OF_FAIL:
		if (tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		priv->state = ISOBUSFS_CLI_STATE_TEST_DONE;
		break;
	case ISOBUSFS_CLI_STATE_OF_DONE:
		if (!tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		ret = isobusfs_cli_fa_sf_req(priv, priv->handle,
					     ISOBUSFS_FA_SEEK_SET, tp->offset);
		if (ret)
			goto test_fail;
		break;
	case ISOBUSFS_CLI_STATE_SF_FAIL:
		if (tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		priv->state = ISOBUSFS_CLI_STATE_TEST_DONE;
		break;
	case ISOBUSFS_CLI_STATE_SF_DONE:
		if (!tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		if (priv->read_offset != tp->offset) {
			pr_err("Not expected read offset: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		ret = isobusfs_cli_fa_rf_req(priv, priv->handle,
			   tp->read_size);
		if (ret)
			goto test_fail;
		break;
	case ISOBUSFS_CLI_STATE_RF_FAIL:
		if (tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		priv->state = ISOBUSFS_CLI_STATE_TEST_CLEANUP;
		break;
	case ISOBUSFS_CLI_STATE_RF_DONE:
		if (!tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		/* fall troth */
	case ISOBUSFS_CLI_STATE_TEST_CLEANUP:
		if (priv->handle != ISOBUSFS_FILE_HANDLE_ERROR)
			isobusfs_cli_fa_cf_req(priv, priv->handle);
		else
			priv->state = ISOBUSFS_CLI_STATE_TEST_DONE;

		break;
	case ISOBUSFS_CLI_STATE_CF_FAIL:
		pr_err("failed to close file: %s", tp->path_name);
		ret = -EINVAL;
		goto test_fail;
	case ISOBUSFS_CLI_STATE_CF_DONE:
	case ISOBUSFS_CLI_STATE_TEST_DONE:
		current_of_pattern_test++;

		if (current_of_pattern_test >= num_patterns)
			*complete = true;
		else
			priv->state = ISOBUSFS_CLI_STATE_SELFTEST;
		break;
	default:
		pr_err("%s:%i: unknown state: %d", __func__, __LINE__, priv->state);
		ret = -EINVAL;
		goto test_fail;
	}

	return 0;

test_fail:
	*complete = true;
	return ret;
}

struct isobusfs_cli_test_rf_path {
	const char *path_name;
	uint8_t flags;
	uint32_t offset;
	uint32_t read_size;
	bool expect_pass;
};

static struct isobusfs_cli_test_rf_path test_rf_patterns[] = {
	/* expected result \\vol1\dir1\dir2\ */
	{ "\\\\vol1\\dir1\\dir2\\file1k", 0, 0, 0, true },
	{ "\\\\vol1\\dir1\\dir2\\file1k", 0, 0, 1, true },
	{ "\\\\vol1\\dir1\\dir2\\file1k", 0, 1, 1, true },
	{ "\\\\vol1\\dir1\\dir2\\file1k", 0, 2, 1, true },
	{ "\\\\vol1\\dir1\\dir2\\file1k", 0, 3, 1, true },
	{ "\\\\vol1\\dir1\\dir2\\file1m", 0, 0, 8, true },
	{ "\\\\vol1\\dir1\\dir2\\file1m", 0, 0, 8 * 100, true },
	{ "\\\\vol1\\dir1\\dir2\\file1m", 0, 100, 8 * 100, true },
	{ "\\\\vol1\\dir1\\dir2\\file1m", 0, 0, ISOBUSFS_MAX_DATA_LENGH, true },
	{ "\\\\vol1\\dir1\\dir2\\file1m", 0, 0, (ISOBUSFS_MAX_DATA_LENGH & ~3) + 16, true },
	{ "\\\\vol1\\dir1\\dir2\\file1m", 0, 0, ISOBUSFS_MAX_DATA_LENGH + 1, true },
	{ "\\\\vol1\\dir1\\dir2\\file1m", 0, 0, -1, true },
};

size_t current_rf_pattern_test;

static uint32_t isobusfs_cli_calculate_sum(uint8_t *data, size_t size,
					   uint32_t offset)
{
	const uint8_t xor_pattern[] = {0xde, 0xad, 0xbe, 0xef};
	uint32_t actual_sum = 0;
	uint32_t current_value = 0;
	uint8_t byte_offset = 0;
	size_t idx;

	byte_offset = offset % 4;

	for (idx = 0; idx < size; idx++) {
		uint8_t byte;

		if (data) {
			byte = data[idx] ^ xor_pattern[byte_offset];
			current_value |= (byte << ((3 - byte_offset) * 8));
		} else {
			uint32_t value_at_offset;

			/* if no data is provided, generate the data based on
			 * offset
			 */

			value_at_offset = (offset + idx) / 4;
			byte_offset = (offset + idx) % 4;
			byte = (value_at_offset >> ((3 - byte_offset) * 8)) & 0xff;
			current_value |= (byte << ((3 - byte_offset) * 8));
		}

		if (byte_offset == 3) {
			actual_sum += current_value;
			current_value = 0;
		}

		byte_offset = (byte_offset + 1) % 4;

		/* if this is the last byte in the buffer but it's not aligned,
		 * add the partial uint32_t to the sum
		 */
		if (idx == size - 1 && byte_offset != 0)
			actual_sum += current_value;
	}

	return actual_sum;
}

static int isobusfs_cli_test_rf_req(struct isobusfs_priv *priv, bool *complete)
{
	size_t num_patterns = ARRAY_SIZE(test_rf_patterns);
	struct isobusfs_cli_test_rf_path *tp =
		&test_rf_patterns[current_rf_pattern_test];
	uint32_t actual_sum, expected_sum;
	struct timespec current_time;
	ssize_t remaining_size, read_size;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &current_time);

	switch (priv->state) {
	case ISOBUSFS_CLI_STATE_SELFTEST:
		test_start_time = current_time;
		pr_info("Start read test. Path: %s, size: %d, offset: %d, flags: %x",
			tp->path_name, tp->read_size, tp->offset, tp->flags);
		ret = isobusfs_cli_fa_of_req(priv, tp->path_name,
					     strlen(tp->path_name), tp->flags);
		if (ret)
			goto test_fail;

		break;
	case ISOBUSFS_CLI_STATE_WAIT_OF_RESP:
		if (current_time.tv_sec - test_start_time.tv_sec >= 5) {
			ret = -ETIMEDOUT;
			goto test_fail;
		}
		break;
	case ISOBUSFS_CLI_STATE_OF_FAIL:
		if (tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		priv->state = ISOBUSFS_CLI_STATE_TEST_DONE;
		break;
	case ISOBUSFS_CLI_STATE_OF_DONE:
		if (!tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		ret = isobusfs_cli_fa_sf_req(priv, priv->handle,
					     ISOBUSFS_FA_SEEK_SET, tp->offset);
		if (ret)
			goto test_fail;
		break;
	case ISOBUSFS_CLI_STATE_SF_FAIL:
		if (tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		priv->state = ISOBUSFS_CLI_STATE_TEST_DONE;
		break;
	case ISOBUSFS_CLI_STATE_SF_DONE:
		if (!tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		if (priv->read_offset != tp->offset) {
			pr_err("Not expected read offset: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		if (tp->read_size > 0xffff)
			read_size = 0xffff;
		else
			read_size = tp->read_size;

		ret = isobusfs_cli_fa_rf_req(priv, priv->handle,
			   read_size);
		if (ret)
			goto test_fail;
		test_start_time = current_time;
		break;
	case ISOBUSFS_CLI_STATE_WAIT_RF_RESP:
		if (current_time.tv_sec - test_start_time.tv_sec >= 5) {
			ret = -ETIMEDOUT;
			goto test_fail;
		}
		break;
	case ISOBUSFS_CLI_STATE_RF_FAIL:
		if (tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		priv->state = ISOBUSFS_CLI_STATE_TEST_CLEANUP;
		break;
	case ISOBUSFS_CLI_STATE_RF_DONE:
		if (!tp->expect_pass) {
			pr_err("pattern test failed: %s", tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		}

		pr_info("read file size: %zu", priv->read_data_len);
		actual_sum = isobusfs_cli_calculate_sum(priv->read_data,
							priv->read_data_len,
							priv->read_offset);
		expected_sum = isobusfs_cli_calculate_sum(NULL,
							  priv->read_data_len,
							  priv->read_offset);
		if (actual_sum != expected_sum) {
			pr_err("pattern test failed: incorrect sum in %s. Sum got: %d, expected: %d",
			       tp->path_name, actual_sum, expected_sum);
			isobusfs_cmn_dump_last_x_bytes(priv->read_data,
						       priv->read_data_len, 16);

			ret = -EINVAL;
			goto test_fail;
		} else {
			pr_info("pattern test passed: %s. Sum got: %d, expected: %d",
				tp->path_name, actual_sum, expected_sum);
			isobusfs_cmn_dump_last_x_bytes(priv->read_data,
						       priv->read_data_len, 16);
		}

		free(priv->read_data);
		priv->read_data = NULL;

		remaining_size = (tp->offset + tp->read_size) -
				(priv->read_offset + priv->read_data_len);
		pr_debug("remaining_size: %zd, read_offset: %zu, read_data_len: %zu, test read size: %zu, test offset %zu",
			 remaining_size, priv->read_offset, priv->read_data_len,
			 tp->read_size, tp->offset);
		if (remaining_size < 0) {
			pr_err("pattern test failed: %s. Read size is too big",
			       tp->path_name);
			ret = -EINVAL;
			goto test_fail;
		} else if (remaining_size > 0 && priv->read_data_len != 0) {
			priv->read_offset += priv->read_data_len;

			if (remaining_size > 0xffff)
				read_size = 0xffff;
			else
				read_size = remaining_size;

			ret = isobusfs_cli_fa_rf_req(priv, priv->handle,
						     read_size);
			if (ret)
				goto test_fail;
			test_start_time = current_time;
			break;
		} else if (remaining_size > 0 && priv->read_data_len == 0 && tp->expect_pass) {
			pr_err("read test failed: %s. Read size is zero, but expected more data: %zd",
			       tp->path_name, remaining_size);
			ret = -EINVAL;
			goto test_fail;
		}

		/* fall troth */
	case ISOBUSFS_CLI_STATE_TEST_CLEANUP:
		if (priv->handle != ISOBUSFS_FILE_HANDLE_ERROR) {
			isobusfs_cli_fa_cf_req(priv, priv->handle);
			test_start_time = current_time;
		} else {
			priv->state = ISOBUSFS_CLI_STATE_TEST_DONE;
		}

		break;
	case ISOBUSFS_CLI_STATE_WAIT_CF_RESP:
		if (current_time.tv_sec - test_start_time.tv_sec >= 5) {
			ret = -ETIMEDOUT;
			goto test_fail;
		}
		break;
	case ISOBUSFS_CLI_STATE_CF_FAIL:
		pr_err("failed to close file: %s", tp->path_name);
		ret = -EINVAL;
		goto test_fail;
	case ISOBUSFS_CLI_STATE_CF_DONE:
	case ISOBUSFS_CLI_STATE_TEST_DONE:
		current_rf_pattern_test++;

		if (current_rf_pattern_test >= num_patterns)
			*complete = true;
		else
			priv->state = ISOBUSFS_CLI_STATE_SELFTEST;

		break;
	default:
		pr_err("%s:%i: unknown state: %d", __func__, __LINE__,
		       priv->state);
		ret = -EINVAL;
		goto test_fail;
	}

	return 0;

test_fail:
	*complete = true;
	return ret;
}

struct isobusfs_cli_test_case test_cases[] = {
	{ isobusfs_cli_test_connect, "Server connection" },
	{ isobusfs_cli_test_property_req, "Server property request" },
	{ isobusfs_cli_test_volume_status_req, "Volume status request" },
	{ isobusfs_cli_test_current_dir_req, "Get current dir request" },
	{ isobusfs_cli_test_ccd_req, "Change current dir request" },
	{ isobusfs_cli_test_of_req, "Open File request" },
	{ isobusfs_cli_test_sf_req, "Seek File request" },
	{ isobusfs_cli_test_rf_req, "Read File request" },
};

void isobusfs_cli_run_self_tests(struct isobusfs_priv *priv)
{
	if (priv->run_selftest) {
		size_t num_tests = ARRAY_SIZE(test_cases);

		if (current_test < num_tests) {
			bool test_complete = false;
			int ret;

			if (!test_running) {
				pr_int("Executing test %zu: %s\n", current_test + 1, test_cases[current_test].test_description);
				test_running = true;
				priv->state = ISOBUSFS_CLI_STATE_SELFTEST;
			}

			ret = test_cases[current_test].test_func(priv, &test_complete);

			if (test_complete) {
				test_running = false;
				pr_int("Test %zu: %s.\n", current_test + 1, ret ? "FAILED" : "PASSED");
				current_test++;
				priv->state = ISOBUSFS_CLI_STATE_SELFTEST;
			}
		} else {
			pr_int("All tests completed.\n");
			priv->run_selftest = false;
			current_test = 0;
			priv->state = ISOBUSFS_CLI_STATE_IDLE;
		}
	}
}
