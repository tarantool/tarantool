#include <pthread.h>
#include <stdlib.h>
#define __TOY_DISPATCH__
#include "objc/toydispatch.h"

/**
 * Amount of total space in the ring buffer.  Must be a power of two.
 */
#define RING_BUFFER_SIZE 32
/**
 * Mask for converting a free-running counters into ring buffer indexes.
 */
#define RING_BUFFER_MASK (RING_BUFFER_SIZE - 1)

struct dispatch_queue
{
	/**
	 * Reference count for this queue.
	 */
	int refcount;
	/**
	 * Spin lock value.  Set to 1 when the queue is locked.  This allows
	 * multiple threads to write to the queue but only one to read from it.
	 * Reading and writing can happen concurrently, but writing requires
	 * acquisition of this lock.
	 */
	volatile int spinlock;
	/**
	 * Producer free-running counter.  Incremented every time that a new item
	 * is inserted into the ring buffer.
	 */
	unsigned int producer;
	/**
	 * Consumer free-running counter.  Incremented every time that an item is
	 * removed from the buffer.
	 */
	unsigned int consumer;
	/**
	 * Mutex used to protect the condition variable.
	 */
	pthread_mutex_t mutex;
	/**
	 * Condition variable used in blocking mode.  The consumer thread will
	 * sleep on this condition variable when the queue has been empty for a
	 * little while.  The next producer thread to insert something will poke
	 * the condition variable on any empty->non-empty transition.
	 */
	pthread_cond_t conditionVariable;
	/**
	 * Ring buffer containing functions and data to be executed by the
	 * consumer.
	 */
	struct
	{
		dispatch_function_t function;
		void *data;
	} ring_buffer[RING_BUFFER_SIZE];
};

/**
 * Check how much space is in the queue.  The number of used elements in the
 * queue is always equal to producer - consumer.   Producer will always
 * overflow before consumer (because you can't remove objects that have not
 * been inserted.  In this case, the subtraction will be something along the
 * lines of (0 - (2^32 - 14)).  This will be -(2^32 - 14), however this value
 * can't be represented in a 32-bit integer and so will overflow to 14, giving
 * the correct result, irrespective of overflow.  
 */
#define SPACE(q) (RING_BUFFER_SIZE - (q->producer - q->consumer))
/**
 * The buffer is full if there is no space in it.
 */
#define ISFULL(q) (SPACE(q) == 0)
/**
 * The buffer is empty if there is no data in it.
 */
#define ISEMPTY(q) ((q->producer - q->consumer) == 0)
/**
 * Converting the free running counters to array indexes is a masking
 * operation.  For this to work, the buffer size must be a power of two.
 * RING_BUFFER_MASK = RING_BUFFER_SIZE - 1.  If RING_BUFFER_SIZE is 256, we want the lowest 8
 * bits of the index, which is obtained by ANDing the value with 255.  Any
 * power of two may be selected.  Non power-of-two values could be used if a
 * more complex mapping operation were chosen, but this one is nice and cheap.
 */
#define MASK(index) ((index) & RING_BUFFER_MASK)

/**
 * Lock the queue.  This uses a very lightweight, nonrecursive, spinlock.  It
 * is expected that queue insertions will be relatively uncontended.
 */
inline static void lock_queue(dispatch_queue_t queue)
{
	// Set the spin lock value to 1 if it is 0.
	while(!__sync_bool_compare_and_swap(&queue->spinlock, 0, 1))
	{
		// If it is already 1, let another thread play with the CPU for a bit
		// then try again.
		sched_yield();
	}
}

/**
 * Unlock the queue.  This doesn't need to be an atomic op; that will cause a
 * complete pipeline flush on this thread and not actually buy us anything
 * because at this point only one thread (this one) will do anything that will
 * modify the variable.  The other threads will all be using atomic
 * compare-and-exchange instructions which will fail because we already set it
 * to 1.
 */
inline static void unlock_queue(dispatch_queue_t queue)
{
	queue->spinlock = 0;
}

/**
 * Inserting an element into the queue involves the following steps:
 *
 * 1) Check that there is space in the buffer.
 *     Spin if there isn't any.
 * 2) Add the invocation and optionally the proxy containing the return value
 * (nil for none) to the next two elements in the ring buffer.
 * 3) Increment the producer counter (by two, since we are adding two elements).
 * 4) If the queue was previously empty, we need to transition back to lockless
 * mode.  This is done by signalling the condition variable that the other
 * thread will be waiting on if it is in blocking mode.
 */
inline static void insert_into_queue(dispatch_queue_t queue,
		dispatch_function_t function, 
		void *data)
{
	/* Wait for space in the buffer */
	lock_queue(queue);
	while (ISFULL(queue))
	{
		sched_yield();
	}
	unsigned int idx = MASK(queue->producer);
	queue->ring_buffer[idx].function = function;
	queue->ring_buffer[idx].data = data;
	// NOTE: This doesn't actually need to be atomic on a strongly-ordered
	// architecture like x86.
	__sync_fetch_and_add(&queue->producer, 1);
	unsigned int space = queue->producer - queue->consumer;
	unlock_queue(queue);
	// If we've just transitioned from empty to full, wake up the consumer thread.
	// Note: We do this after unlocking the queue, because it is much more
	// expensive than anything else that we do in this function and we don't
	// want to hold the spinlock for any longer than possible.  We need to
	// calculate the space first, however, because otherwise another thread may
	// increment producer, while consumer stays the same (with the consumer
	// thread sleeping), preventing the wakeup.
	if (space == 1)
	{
		pthread_mutex_lock(&queue->mutex);
		pthread_cond_signal(&queue->conditionVariable);
		pthread_mutex_unlock(&queue->mutex);
	}
}
/**
 * Removing an element from the queue involves the following steps:
 *
 * 1) Wait until the queue has messages waiting.  If there are none, enter
 * blocking mode.  The additional test inside the mutex ensures that a
 * transition from blocking to non-blocking mode will not be missed, since the
 * condition variable can only be signalled when the producer thread has the
 * mutex.  
 * 2) Read the invocation and return proxy from the buffer.
 * 3) Incrememt the consumer counter.
 */
static inline void read_from_queue(dispatch_queue_t queue, 
		dispatch_function_t *function, void **data)
{
	while (ISEMPTY(queue))
	{
		pthread_mutex_lock(&queue->mutex);
		if (ISEMPTY(queue))
		{
			pthread_cond_wait(&queue->conditionVariable, &queue->mutex);
		}
		pthread_mutex_unlock(&queue->mutex);
	}
	unsigned int idx = MASK(queue->consumer);
	*function = queue->ring_buffer[idx].function;
	*data = queue->ring_buffer[idx].data;
	__sync_fetch_and_add(&queue->consumer, 1);
}

static void *runloop(void *q)
{
	dispatch_queue_t queue = q;
	dispatch_function_t function;
	void *data;
	while (queue->refcount > 0)
	{
		read_from_queue(queue, &function, &data);
		function(data);
	}
	pthread_cond_destroy(&queue->conditionVariable);
	pthread_mutex_destroy(&queue->mutex);
	free(queue);
	return NULL;
}


dispatch_queue_t dispatch_queue_create(const char *label,
		void *attr)
{
	dispatch_queue_t queue = calloc(1, sizeof(struct dispatch_queue));
	queue->refcount = 1;
	pthread_cond_init(&queue->conditionVariable, NULL);
	pthread_mutex_init(&queue->mutex, NULL);
	pthread_t thread;
	pthread_create(&thread, NULL, runloop, queue);
	pthread_detach(thread);
	return queue;
}

void dispatch_async_f(dispatch_queue_t queue, void *context,
		dispatch_function_t work)
{
	insert_into_queue(queue, work, context);
}

static void release(void *queue)
{
	((dispatch_queue_t)queue)->refcount--;
}

void dispatch_release(dispatch_queue_t queue)
{
	// Asynchronously release the queue, so that we don't delete it before all
	// of the work is finished.
	insert_into_queue(queue, release, queue);
}

void dispatch_retain(dispatch_queue_t queue)
{
	queue->refcount++;
}
