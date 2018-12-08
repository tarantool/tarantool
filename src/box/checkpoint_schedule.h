#ifndef TARANTOOL_BOX_CHECKPOINT_SCHEDULE_H_INCLUDED
#define TARANTOOL_BOX_CHECKPOINT_SCHEDULE_H_INCLUDED
/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct checkpoint_schedule {
	/**
	 * Configured interval between checkpoints, in seconds.
	 * Set to 0 if periodic checkpointing is disabled.
	 */
	double interval;
	/**
	 * Time of the first scheduled checkpoint. It is used
	 * for calculating times of all subsequent checkpoints.
	 */
	double start_time;
};

/**
 * (Re)configure a checkpoint schedule.
 *
 * @now is the current time.
 * @interval is the configured interval between checkpoints.
 */
void
checkpoint_schedule_cfg(struct checkpoint_schedule *sched,
			double now, double interval);

/**
 * Reset a checkpoint schedule.
 *
 * Called when a checkpoint is triggered out of the schedule.
 * Used to adjusts the schedule accordingly.
 *
 * @now is the current time.
 */
void
checkpoint_schedule_reset(struct checkpoint_schedule *sched, double now);

/**
 * Return the time to the next scheduled checkpoint, in seconds.
 * If auto checkpointing is disabled, returns 0.
 *
 * @now is the current time.
 */
double
checkpoint_schedule_timeout(struct checkpoint_schedule *sched, double now);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_CHECKPOINT_SCHEDULE_H_INCLUDED */
