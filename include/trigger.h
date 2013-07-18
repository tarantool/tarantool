#ifndef INCLUDES_TARANTOOL_TRIGGER_H
#define INCLUDES_TARANTOOL_TRIGGER_H
#
/**
 * Type of the callback which may be invoked
 * on an event.
 */
typedef void (*trigger_f)(void *);

struct trigger
{
	trigger_f trigger;
	void *param;
};

static inline void
trigger_run(struct trigger *trigger)
{
	if (trigger->trigger)
		trigger->trigger(trigger->param);
}

static inline void
trigger_set(struct trigger *trigger, trigger_f func, void *param)
{
	trigger->trigger = func;
	trigger->param = param;
}

#endif /* INCLUDES_TARANTOOL_TRIGGER_H */
