#include "memory.h"
#include "fiber.h"
#include "coio.h"
#include "fio.h"
#include "unit.h"
#include "unit.h"

void
touch_f(va_list ap)
{
	FILE *f = va_arg(ap, FILE *);
	const char *c = "c";
	while (true) {
		int rc = fwrite(c, strlen(c), 1, f);
		fail_unless(rc == 1);
		fflush(f);
		fiber_sleep(0.01);
		fiber_testcancel();
	}
}

static void
stat_notify_test(FILE *f, const char *filename)
{
	header();

	struct fiber *touch = fiber_new("touch", touch_f);
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

void
main_f(va_list ap)
{
	const char *filename = "1.out";
	FILE *f = fopen(filename, "w+");
	stat_timeout_test(filename);
	stat_notify_test(f, filename);
	fclose(f);
	remove(filename);
	ev_break(loop(), EVBREAK_ALL);
}

int main()
{
	memory_init();
	fiber_init();
	struct fiber *test = fiber_new("coio_stat", main_f);
	fiber_wakeup(test);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return 0;
}
