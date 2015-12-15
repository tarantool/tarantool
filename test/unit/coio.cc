#include "memory.h"
#include "fiber.h"
#include "coio.h"
#include "fio.h"
#include "unit.h"
#include "unit.h"

int
touch_f(va_list ap)
{
	FILE *f = va_arg(ap, FILE *);
	const char *c = "c";
	while (true) {
		int rc = fwrite(c, strlen(c), 1, f);
		fail_unless(rc == 1);
		fflush(f);
		fiber_sleep(0.01);
		if (fiber_is_cancelled())
			return -1;
	}
	return 0;
}

static void
stat_notify_test(FILE *f, const char *filename)
{
	header();

	struct fiber *touch = fiber_new_xc("touch", touch_f);
	fiber_start(touch, f);
	ev_stat stat;
	note("filename: %s", filename);
	coio_stat_init(&stat, filename);
	coio_stat_stat_timeout(&stat, TIMEOUT_INFINITY);
	fail_unless(stat.prev.st_size < stat.attr.st_size);
	fiber_cancel(touch);

	footer();
}

static void
stat_timeout_test(const char *filename)
{
	header();

	ev_stat stat;
	coio_stat_init(&stat, filename);
	coio_stat_stat_timeout(&stat, 0.01);

	footer();
}

static int
main_f(va_list ap)
{
	const char *filename = "1.out";
	FILE *f = fopen(filename, "w+");
	stat_timeout_test(filename);
	stat_notify_test(f, filename);
	fclose(f);
	remove(filename);
	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int main()
{
	memory_init();
	fiber_init(fiber_cxx_invoke);
	struct fiber *test = fiber_new_xc("coio_stat", main_f);
	fiber_wakeup(test);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return 0;
}
