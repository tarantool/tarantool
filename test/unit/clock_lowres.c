#include "unit.h"

#include <math.h>
#include "clock_lowres.h"
#include "clock.h"

#define CLOCK_LOWRES_RESOLUTION 0.03
#define TEST_LEN 1

int
main(void)
{
	plan(1);
	clock_lowres_signal_init();

	bool success = true;
	double start = clock_monotonic();
	double clock = start;
	while (clock < start + TEST_LEN) {
		double lowres = clock_monotonic_lowres();
		clock = clock_monotonic();
		if (fabs(clock - lowres) > CLOCK_LOWRES_RESOLUTION) {
			success = false;
			break;
		}
	}
	ok(success, "Check that monotonic lowres clock does not diverge "
		    "too much from monotonic clock");

	clock_lowres_signal_reset();
	return check_plan();
}
