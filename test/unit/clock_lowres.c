#include <math.h>
#include <unistd.h>
#include "clock_lowres.h"
#include "clock.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

/** Test duration in seconds. */
#define TEST_LEN 1.5

int
main(void)
{
	plan(1);
	clock_lowres_signal_init();

	/* Such a large resolution to pass the test in debug or apple build. */
	double resolution = clock_lowres_resolution() * 2;
	bool success = true;
	double start = clock_monotonic();
	double clock = start;
	while (clock < start + TEST_LEN) {
		/*
		 * Use pause before getting time so that the process
		 * does not use all time and unlikely to be
		 * rescheduled in the middle of check.
		 * The process will wake up after receiving a signal:
		 * the clock receives SIGALRM every resolution seconds.
		 */
		pause();
		double lowres = clock_lowres_monotonic();
		clock = clock_monotonic();
		if (fabs(clock - lowres) > resolution) {
			success = false;
			break;
		}
	}
	ok(success, "Check that monotonic lowres clock does not diverge "
		    "too much from monotonic clock");

	clock_lowres_signal_reset();
	return check_plan();
}
