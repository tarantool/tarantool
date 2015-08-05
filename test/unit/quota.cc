#include "small/quota.h"

#include <pthread.h>
#include <sched.h>

#include "unit.h"

struct quota quota;

const size_t THREAD_CNT = 10;
const size_t RUN_CNT = 128 * 1024;

struct thread_data {
	size_t use_change;
	size_t last_lim_set;
	long use_change_success;
	long lim_change_success;
};

pthread_t threads[THREAD_CNT];
thread_data datum[THREAD_CNT];

void *thread_routine(void *vparam)
{
	struct thread_data *data = (struct thread_data *)vparam;
	size_t check_fail_count = 0;
	ssize_t allocated_size = 0;
	for (size_t i = 0; i < RUN_CNT; i++) {
		{
			size_t total, used;
			quota_get_total_and_used(&quota, &total, &used);
			if (used > total)
				check_fail_count++;
		}
		ssize_t max = rand() % QUOTA_MAX;
		max = quota_set(&quota, max);
		sched_yield();
		if (max > 0) {
			data->last_lim_set = max;
			data->lim_change_success++;
		}
		if (allocated_size > 0) {
			quota_release(&quota, allocated_size);
			allocated_size = -1;
			data->use_change = 0;
			data->use_change_success++;
			sched_yield();
		} else {
			allocated_size = rand() % max + 1;
			allocated_size = quota_use(&quota, allocated_size);
			if (allocated_size > 0) {
				data->use_change = allocated_size;
				data->use_change_success++;
			}
			sched_yield();
		}
	}
	return (void *)check_fail_count;
}

int
main(int n, char **a)
{
	(void)n;
	(void)a;
	quota_init(&quota, 0);
	srand(time(0));

	plan(5);

	for (size_t i = 0; i < THREAD_CNT; i++) {
		pthread_create(threads + i, 0, thread_routine, (void *)(datum + i));
	}

	size_t check_fail_count = 0;
	for (size_t i = 0; i < THREAD_CNT; i++) {
		void *ret;
		check_fail_count += (size_t)pthread_join(threads[i], &ret);
	}

	bool one_set_successed = false;
	size_t total_alloc = 0;
	long set_success_count = 0;
	long use_success_count = 0;
	for (size_t i = 0; i < THREAD_CNT; i++) {
		if (datum[i].last_lim_set == quota_total(&quota))
			one_set_successed = true;
		total_alloc += datum[i].use_change;
		use_success_count += datum[i].use_change_success;
		set_success_count += datum[i].lim_change_success;
	}

	ok(check_fail_count == 0, "no fails detected");
	ok(one_set_successed, "one of thread limit set is final");
	ok(total_alloc == quota_used(&quota), "total alloc match");
	ok(use_success_count > THREAD_CNT * RUN_CNT * .1, "uses are mosly successful");
	ok(set_success_count > THREAD_CNT * RUN_CNT * .1, "sets are mosly successful");

	return check_plan();
}
