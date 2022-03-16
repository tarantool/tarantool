/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <stdio.h>

#include "interval.h"
#include "tt_static.h"

void
interval_to_string(const struct interval *itv, char *out, size_t size)
{
	char buf[INTERVAL_STR_MAX_LEN];
	int len = 0;
	if (itv->year != 0) {
		len += snprintf(buf, INTERVAL_STR_MAX_LEN, ", %d years",
				 itv->year);
	}
	if (itv->month != 0) {
		len += snprintf(&buf[len], INTERVAL_STR_MAX_LEN - len,
				 ", %d months", itv->month);
	}
	if (itv->week != 0) {
		len += snprintf(&buf[len], INTERVAL_STR_MAX_LEN - len,
				 ", %d weeks", itv->week);
	}
	if (itv->day != 0) {
		len += snprintf(&buf[len], INTERVAL_STR_MAX_LEN - len,
				 ", %d days", itv->day);
	}
	if (itv->hour != 0) {
		len += snprintf(&buf[len], INTERVAL_STR_MAX_LEN - len,
				 ", %d hours", itv->hour);
	}
	if (itv->min != 0) {
		len += snprintf(&buf[len], INTERVAL_STR_MAX_LEN - len,
				 ", %d minutes", itv->min);
	}
	if (itv->sec != 0) {
		len += snprintf(&buf[len], INTERVAL_STR_MAX_LEN - len,
				 ", %d seconds", itv->sec);
	}
	if (itv->nsec != 0) {
		len += snprintf(&buf[len], INTERVAL_STR_MAX_LEN - len,
				 ", %d nanoseconds", itv->nsec);
	}
	if (len == 0)
		len = snprintf(buf, INTERVAL_STR_MAX_LEN, ", 0 seconds");
	if (itv->adjust == INTERVAL_ADJUST_LIMIT) {
		snprintf(&buf[len], INTERVAL_STR_MAX_LEN - len,
			 ", LIMIT adjust");
	} else if (itv->adjust == INTERVAL_ADJUST_SNAP) {
		snprintf(&buf[len], INTERVAL_STR_MAX_LEN - len,
			 ", SNAP adjust");
	}
	snprintf(out, size, "%s", buf + 2);
}

char *
interval_str(const struct interval *itv)
{
	char *buf = tt_static_buf();
	interval_to_string(itv, buf, TT_STATIC_BUF_LEN);
	return buf;
}
