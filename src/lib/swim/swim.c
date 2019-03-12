/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "swim.h"
#include "swim_io.h"
#include "swim_proto.h"
#include "swim_ev.h"
#include "uri/uri.h"
#include "fiber.h"
#include "msgpuck.h"
#include "info/info.h"
#include "assoc.h"
#include "sio.h"

/**
 * SWIM - Scalable Weakly-consistent Infection-style Process Group
 * Membership Protocol. It consists of 2 components: events
 * dissemination and failure detection, and stores in memory a
 * table of known remote hosts - members. Also some SWIM
 * implementations have an additional component: anti-entropy -
 * periodical broadcast of a random subset of the member table.
 *
 * Each SWIM component is different in both protocol payload and
 * goals, and could even use different messages to send data. But
 * SWIM describes piggybacking of messages: a ping message can
 * piggyback a dissemination's one.
 *
 * SWIM has a main operating cycle during which it randomly
 * chooses members from a member table and sends to them events +
 * ping. Replies are processed out of the main cycle,
 * asynchronously.
 *
 * Random selection provides even network load of ~1 message on
 * each member per one protocol step regardless of the cluster
 * size. Without randomness each member would receive a network
 * load of N messages in each protocol step, where N is the
 * cluster size.
 *
 * To speed up propagation of new information by means of a few
 * random messages SWIM proposes a kind of fairness: when
 * selecting a next random member to ping, the protocol prefers
 * LRU members. In code it would be too complicated, so
 * Tarantool's implementation is slightly different, easier:
 *
 * Tarantool splits protocol operation into rounds. At the
 * beginning of a round all members are randomly reordered and
 * linked into a list. At each round step a member is popped from
 * the list head, a message is sent to it, and then it waits for
 * the next round. In such implementation all random selection of
 * the original SWIM is executed once per round. The round is
 * 'planned', actually. A list is used instead of an array since
 * new members can be added to its tail without realloc, and dead
 * members can be removed easily as well.
 *
 * Also Tarantool implements the third SWIM component -
 * anti-entropy. Why is it needed and even vital? Consider the
 * example: two SWIM nodes, both are alive. Nothing happens, so
 * the events list is empty, only pings are being sent
 * periodically. Then a third node appears. It knows about one of
 * the existing nodes. How can it learn about the rest? Sure,
 * its known counterpart can try to notify its peer, but it is
 * UDP, so this event can be lost. Anti-entropy is an extra simple
 * component, it just piggybacks random part of the member table
 * with each regular message. In the example above the new node
 * will learn about the third one via anti-entropy messages from
 * the second one sooner or later.
 *
 * Surprisingly, original SWIM does not describe any addressing,
 * how to uniquely identify a member. IP/port fallaciously could
 * be considered as a good unique identifier, but some arguments
 * below demolish this belief:
 *
 *     - if instances work in separate containers, they can have
 *       the same IP/port inside a container NATed to a unique
 *       IP/port outside the container;
 *
 *     - IP/port are likely to change during instance lifecycle.
 *       Once IP/port are changed, a ghost of the old member's
 *       configuration still lives for a while until it is
 *       suspected, dead and GC-ed. Taking into account that ACK
 *       timeout can be tens of seconds, 'Dead Souls' can exist
 *       unpleasantly long.
 *
 * Tarantool SWIM implementation uses UUIDs as unique identifiers.
 * UUID is much more unlikely to change than IP/port. But even if
 * that happens, dissemination component for a while gossips the
 * new UUID together with the old one.
 *
 * SWIM implementation is split into 3 parts: protocol logic,
 * transport level, protocol structure.
 *
 *     - protocol logic consists of how to react on various
 *       events, failure detection pings/acks, how often to send
 *       messages, handles the logic of the three components
 *       (failure detection, anti-entropy, dissemination);
 *
 *     - transport level handles routing, transport headers,
 *       packet forwarding;
 *
 *     - protocol structure describes how packet looks in
 *       MessagePack, how sections and headers follow each other.
 */

enum {
	/**
	 * How often to send membership messages and pings in
	 * seconds. Nothing special in this concrete default
	 * value.
	 */
	HEARTBEAT_RATE_DEFAULT = 1,
};

/**
 * Return a random number within given boundaries.
 *
 * Instead of blindly calculating a modulo, scale the random
 * number down the given boundaries to preserve the original
 * distribution. The result belongs to range [start, end].
 */
static inline int
swim_scaled_rand(int start, int end)
{
	assert(end >= start);
	/*
	 * RAND_MAX is likely to be INT_MAX - hardly SWIM will
	 * ever be used in such a huge cluster.
	 */
	assert(end - start < RAND_MAX);
	return rand() / (RAND_MAX / (end - start + 1) + 1);
}

/** Calculate UUID hash to use as a member table key. */
static inline uint32_t
swim_uuid_hash(const struct tt_uuid *uuid)
{
	return mh_strn_hash((const char *) uuid, UUID_LEN);
}

/**
 * A helper to not call tt_static_buf() in all places where it is
 * used to get string UUID.
 */
static inline const char *
swim_uuid_str(const struct tt_uuid *uuid)
{
	char *buf = tt_static_buf();
	tt_uuid_to_string(uuid, buf);
	return buf;
}

/** Check if two AF_INET addresses are equal. */
static bool
swim_sockaddr_in_eq(const struct sockaddr_in *a1, const struct sockaddr_in *a2)
{
	return a1->sin_port == a2->sin_port &&
	       a1->sin_addr.s_addr == a2->sin_addr.s_addr;
}

/**
 * A cluster member description. This structure describes the
 * last known state of an instance. This state is updated
 * periodically via UDP according to SWIM protocol rules.
 */
struct swim_member {
	/**
	 * Member status. Since the communication goes via UDP,
	 * actual status can be different, as well as different on
	 * other SWIM nodes. But SWIM guarantees that each member
	 * will learn a real status of an instance sometime.
	 */
	enum swim_member_status status;
	/**
	 * Address of the instance to which to send UDP packets.
	 */
	struct sockaddr_in addr;
	/**
	 * A unique identifier of the member. Is used as a key in
	 * the mebmers table.
	 */
	struct tt_uuid uuid;
	/**
	 * Cached hash of the uuid for the member table lookups.
	 */
	uint32_t hash;
	/**
	 * Position in a queue of members in the current round.
	 */
	struct rlist in_round_queue;
};

#define mh_name _swim_table
struct mh_swim_table_key {
	uint32_t hash;
	const struct tt_uuid *uuid;
};
#define mh_key_t struct mh_swim_table_key
#define mh_node_t struct swim_member *
#define mh_arg_t void *
#define mh_hash(a, arg) ((*a)->hash)
#define mh_hash_key(a, arg) (a.hash)
#define mh_cmp(a, b, arg) (tt_uuid_compare(&(*a)->uuid, &(*b)->uuid))
#define mh_cmp_key(a, b, arg) (tt_uuid_compare(a.uuid, &(*b)->uuid))
#define MH_SOURCE 1
#include "salad/mhash.h"

/**
 * SWIM instance. Stores configuration, manages periodical tasks,
 * rounds. Each member has an object of this type on its host,
 * while on others it is represented as a struct swim_member
 * object.
 */
struct swim {
	/**
	 * Global hash of all known members of the cluster. Hash
	 * key is UUID, value is a struct member, describing a
	 * remote instance. Discovered members live here until
	 * they are detected as dead - in such a case they are
	 * removed from the hash after a while.
	 */
	struct mh_swim_table_t *members;
	/**
	 * This node. Is used to not send messages to self, it's
	 * meaningless. Also to refute false gossips about self
	 * status.
	 */
	struct swim_member *self;
	/**
	 * Members to which a message should be sent next during
	 * this round.
	 */
	struct rlist round_queue;
	/** Generator of round step events. */
	struct ev_timer round_tick;
	/**
	 * Single round step task. It is impossible to have
	 * multiple round steps in the same SWIM instance at the
	 * same time, so it is single and preallocated per SWIM
	 * instance.
	 */
	struct swim_task round_step_task;
	/**
	 * Preallocated buffer to store shuffled members here at
	 * the beginning of each round.
	 */
	struct swim_member **shuffled;
	/**
	 * Scheduler of output requests, receiver of incoming
	 * ones.
	 */
	struct swim_scheduler scheduler;
	/**
	 * An offset of this instance in the hash table. Is used
	 * to iterate in the hash table starting from this swim
	 * instance. Such iteration is unstable between yields
	 * (i.e. member positions may change when the table is
	 * resized after an incoming event), but still is useful
	 * for a fast non-yielding scan of the member table
	 * starting from this instance.
	 */
	mh_int_t iterator;
};

int
swim_fd(const struct swim *swim)
{
	return swim->scheduler.transport.fd;
}

/**
 * A helper to get a pointer to a SWIM instance having only a
 * pointer to its scheduler. It is used by task complete
 * functions.
 */
static inline struct swim *
swim_by_scheduler(struct swim_scheduler *scheduler)
{
	return container_of(scheduler, struct swim, scheduler);
}

/** Free member's resources. */
static inline void
swim_member_delete(struct swim_member *member)
{
	assert(rlist_empty(&member->in_round_queue));
	free(member);
}

/**
 * Reserve space for one more member in the member table. Used to
 * execute a non-failing UUID update.
 */
static inline int
swim_reserve_one_member(struct swim *swim)
{
	if (mh_swim_table_reserve(swim->members, mh_size(swim->members) + 1,
				  NULL) != 0) {
		diag_set(OutOfMemory, sizeof(mh_int_t), "malloc", "node");
		return -1;
	}
	return 0;
}

/** Create a new member. It is not registered anywhere here. */
static struct swim_member *
swim_member_new(const struct sockaddr_in *addr, const struct tt_uuid *uuid,
		enum swim_member_status status)
{
	struct swim_member *member =
		(struct swim_member *) calloc(1, sizeof(*member));
	if (member == NULL) {
		diag_set(OutOfMemory, sizeof(*member), "calloc", "member");
		return NULL;
	}
	member->status = status;
	member->addr = *addr;
	member->uuid = *uuid;
	member->hash = swim_uuid_hash(uuid);
	rlist_create(&member->in_round_queue);
	return member;
}

/**
 * Remove the member from all queues, hashes, destroy it and free
 * the memory.
 */
static void
swim_delete_member(struct swim *swim, struct swim_member *member)
{
	say_verbose("SWIM %d: member %s is deleted", swim_fd(swim),
		    swim_uuid_str(&member->uuid));
	struct mh_swim_table_key key = {member->hash, &member->uuid};
	mh_int_t rc = mh_swim_table_find(swim->members, key, NULL);
	assert(rc != mh_end(swim->members));
	mh_swim_table_del(swim->members, rc, NULL);
	rlist_del_entry(member, in_round_queue);
	swim_member_delete(member);
}

/** Find a member by UUID. */
static inline struct swim_member *
swim_find_member(struct swim *swim, const struct tt_uuid *uuid)
{
	struct mh_swim_table_key key = {swim_uuid_hash(uuid), uuid};
	mh_int_t node = mh_swim_table_find(swim->members, key, NULL);
	if (node == mh_end(swim->members))
		return NULL;
	return *mh_swim_table_node(swim->members, node);
}

/**
 * Register a new member with a specified status. It is not added
 * to the round queue here. It waits until the current round is
 * finished, and then is included into a new round. It is done
 * mainly to not add self into the round queue, because self is
 * also created via this function.
 */
static struct swim_member *
swim_new_member(struct swim *swim, const struct sockaddr_in *addr,
		const struct tt_uuid *uuid, enum swim_member_status status)
{
	int new_bsize = sizeof(swim->shuffled[0]) *
			(mh_size(swim->members) + 1);
	struct swim_member **new_shuffled =
		(struct swim_member **) realloc(swim->shuffled, new_bsize);
	if (new_shuffled == NULL) {
		diag_set(OutOfMemory, new_bsize, "realloc", "new_shuffled");
		return NULL;
	}
	swim->shuffled = new_shuffled;
	struct swim_member *member = swim_member_new(addr, uuid, status);
	if (member == NULL)
		return NULL;
	assert(swim_find_member(swim, uuid) == NULL);
	mh_int_t rc = mh_swim_table_put(swim->members,
					(const struct swim_member **) &member,
					NULL, NULL);
	if (rc == mh_end(swim->members)) {
		swim_member_delete(member);
		diag_set(OutOfMemory, sizeof(mh_int_t), "malloc", "node");
		return NULL;
	}
	if (mh_size(swim->members) > 1)
		swim_ev_timer_start(loop(), &swim->round_tick);
	say_verbose("SWIM %d: member %s is added, total is %d", swim_fd(swim),
		    swim_uuid_str(&member->uuid), mh_size(swim->members));
	return member;
}

/**
 * Take all the members from the table and shuffle them randomly.
 * Is used for forthcoming round planning.
 */
static void
swim_shuffle_members(struct swim *swim)
{
	struct mh_swim_table_t *members = swim->members;
	int i = 0;
	/*
	 * This shuffling preserves even distribution of a random
	 * sequence. The distribution properties have been
	 * verified by a longevity test.
	 */
	for (mh_int_t node = mh_first(members), end = mh_end(members);
	     node != end; node = mh_next(members, node), ++i) {
		swim->shuffled[i] = *mh_swim_table_node(members, node);
		int j = swim_scaled_rand(0, i);
		SWAP(swim->shuffled[i], swim->shuffled[j]);
	}
}

/**
 * Shuffle members, build randomly ordered queue of addressees. In
 * other words, do all round preparation work.
 */
static int
swim_new_round(struct swim *swim)
{
	int size = mh_size(swim->members);
	if (size == 1) {
		assert(swim->self != NULL);
		say_verbose("SWIM %d: skip a round - no members",
			    swim_fd(swim));
		return 0;
	}
	/* -1 for self. */
	say_verbose("SWIM %d: start a new round with %d members", swim_fd(swim),
		    size - 1);
	swim_shuffle_members(swim);
	rlist_create(&swim->round_queue);
	for (int i = 0; i < size; ++i) {
		if (swim->shuffled[i] != swim->self) {
			rlist_add_entry(&swim->round_queue, swim->shuffled[i],
					in_round_queue);
		}
	}
	return 0;
}

/**
 * Encode anti-entropy header and random members data as many as
 * possible to the end of the packet.
 * @retval Number of key-values added to the packet's root map.
 */
static int
swim_encode_anti_entropy(struct swim *swim, struct swim_packet *packet)
{
	struct swim_anti_entropy_header_bin ae_header_bin;
	struct swim_member_bin member_bin;
	int size = sizeof(ae_header_bin);
	char *header = swim_packet_reserve(packet, size);
	if (header == NULL)
		return 0;
	char *pos = header;
	swim_member_bin_create(&member_bin);
	struct mh_swim_table_t *t = swim->members;
	int i = 0, member_count = mh_size(t);
	int rnd = swim_scaled_rand(0, member_count - 1);
	for (mh_int_t rc = mh_swim_table_random(t, rnd), end = mh_end(t);
	     i < member_count; ++i) {
		struct swim_member *m = *mh_swim_table_node(t, rc);
		int new_size = size + sizeof(member_bin);
		if (swim_packet_reserve(packet, new_size) == NULL)
			break;
		swim_member_bin_fill(&member_bin, &m->addr, &m->uuid,
				     m->status);
		memcpy(pos + size, &member_bin, sizeof(member_bin));
		size = new_size;
		/*
		 * First random member could be choosen too close
		 * to the hash end. Here the cycle is wrapped, if
		 * a packet still has free memory, but the
		 * iterator has already reached the hash end.
		 */
		rc = mh_next(t, rc);
		if (rc == end)
			rc = mh_first(t);
	}
	if (i == 0)
		return 0;
	swim_packet_advance(packet, size);
	swim_anti_entropy_header_bin_create(&ae_header_bin, i);
	memcpy(header, &ae_header_bin, sizeof(ae_header_bin));
	return 1;
}

/**
 * Encode source UUID.
 * @retval Number of key-values added to the packet's root map.
 */
static inline int
swim_encode_src_uuid(struct swim *swim, struct swim_packet *packet)
{
	struct swim_src_uuid_bin uuid_bin;
	char *pos = swim_packet_alloc(packet, sizeof(uuid_bin));
	if (pos == NULL)
		return 0;
	swim_src_uuid_bin_create(&uuid_bin, &swim->self->uuid);
	memcpy(pos, &uuid_bin, sizeof(uuid_bin));
	return 1;
}

/** Encode SWIM components into a UDP packet. */
static void
swim_encode_round_msg(struct swim *swim, struct swim_packet *packet)
{
	swim_packet_create(packet);
	char *header = swim_packet_alloc(packet, 1);
	int map_size = 0;
	map_size += swim_encode_src_uuid(swim, packet);
	map_size += swim_encode_anti_entropy(swim, packet);

	assert(mp_sizeof_map(map_size) == 1 && map_size == 2);
	mp_encode_map(header, map_size);
}

/**
 * Once per specified timeout trigger a next round step. In round
 * step a next memeber is taken from the round queue and a round
 * message is sent to him. One member per step.
 */
static void
swim_begin_step(struct ev_loop *loop, struct ev_timer *t, int events)
{
	assert((events & EV_TIMER) != 0);
	(void) events;
	(void) loop;
	struct swim *swim = (struct swim *) t->data;
	if (! rlist_empty(&swim->round_queue)) {
		say_verbose("SWIM %d: continue the round", swim_fd(swim));
	} else if (swim_new_round(swim) != 0) {
		diag_log();
		return;
	}
	/*
	 * Possibly empty, if no members but self are specified.
	 */
	if (rlist_empty(&swim->round_queue))
		return;

	swim_encode_round_msg(swim, &swim->round_step_task.packet);
	struct swim_member *m =
		rlist_first_entry(&swim->round_queue, struct swim_member,
				  in_round_queue);
	swim_task_send(&swim->round_step_task, &m->addr, &swim->scheduler);
}

/**
 * After a round message is sent, the addressee can be popped from
 * the queue, and the next step is scheduled.
 */
static void
swim_complete_step(struct swim_task *task,
		   struct swim_scheduler *scheduler, int rc)
{
	(void) rc;
	(void) task;
	struct swim *swim = swim_by_scheduler(scheduler);
	swim_ev_timer_start(loop(), &swim->round_tick);
	/*
	 * It is possible that the original member was deleted
	 * manually during the task execution.
	 */
	if (rlist_empty(&swim->round_queue))
		return;
	struct swim_member *m =
		rlist_first_entry(&swim->round_queue, struct swim_member,
				  in_round_queue);
	if (swim_sockaddr_in_eq(&m->addr, &task->dst))
		rlist_shift(&swim->round_queue);
}

/**
 * Update member's UUID if it is changed. On UUID change the
 * member is reinserted into the member table with a new UUID.
 * @retval 0 Success.
 * @retval -1 Error. Out of memory or the new UUID is already in
 *         use.
 */
static int
swim_update_member_uuid(struct swim *swim, struct swim_member *member,
			const struct tt_uuid *new_uuid)
{
	if (tt_uuid_is_equal(new_uuid, &member->uuid))
		return 0;
	if (swim_find_member(swim, new_uuid) != NULL) {
		diag_set(SwimError, "duplicate UUID '%s'",
			 swim_uuid_str(new_uuid));
		return -1;
	}
	/*
	 * Reserve before put + delete, because put below can
	 * call rehash, and a reference to the old place in the
	 * hash will taint.
	 */
	if (swim_reserve_one_member(swim) != 0)
		return -1;
	struct mh_swim_table_t *t = swim->members;
	struct tt_uuid old_uuid = member->uuid;
	struct mh_swim_table_key key = {member->hash, &old_uuid};
	mh_int_t old_rc = mh_swim_table_find(t, key, NULL);
	assert(old_rc != mh_end(t));
	member->uuid = *new_uuid;
	member->hash = swim_uuid_hash(new_uuid);
	mh_int_t new_rc =
		mh_swim_table_put(t, (const struct swim_member **) &member,
				  NULL, NULL);
	/* Can not fail - reserved above. */
	assert(new_rc != mh_end(t));
	(void) new_rc;
	/*
	 * Old_rc is still valid, because a possible rehash
	 * happened before put.
	 */
	mh_swim_table_del(t, old_rc, NULL);
	say_verbose("SWIM %d: a member has changed its UUID from %s to %s",
		    swim_fd(swim), swim_uuid_str(&old_uuid),
		    swim_uuid_str(new_uuid));
	return 0;
}

/** Update member's address.*/
static inline void
swim_update_member_addr(struct swim *swim, struct swim_member *member,
			const struct sockaddr_in *addr)
{
	(void) swim;
	member->addr = *addr;
}

/**
 * Update or create a member by its definition, received from a
 * remote instance.
 * @param swim SWIM instance to upsert into.
 * @param def Member definition to build a new member or update an
 *        existing one.
 * @param[out] result A result member: a new, or an updated, or
 *        NULL in case of nothing has changed. For example, @a def
 *        was too old.
 *
 * @retval 0 Success. Member is added, or updated. Or nothing has
 *         changed but not always it is an error.
 * @retval -1 Error.
 */
static int
swim_upsert_member(struct swim *swim, const struct swim_member_def *def,
		   struct swim_member **result)
{
	struct swim_member *member = swim_find_member(swim, &def->uuid);
	if (member == NULL) {
		*result = swim_new_member(swim, &def->addr, &def->uuid,
					  def->status);
		return *result != NULL ? 0 : -1;
	}
	struct swim_member *self = swim->self;
	if (member != self)
		swim_update_member_addr(swim, member, &def->addr);
	*result = member;
	return 0;
}

/** Decode an anti-entropy message, update member table. */
static int
swim_process_anti_entropy(struct swim *swim, const char **pos, const char *end)
{
	say_verbose("SWIM %d: process anti-entropy", swim_fd(swim));
	const char *prefix = "invalid anti-entropy message:";
	uint32_t size;
	if (swim_decode_array(pos, end, &size, prefix, "root") != 0)
		return -1;
	for (uint64_t i = 0; i < size; ++i) {
		struct swim_member_def def;
		struct swim_member *member;
		if (swim_member_def_decode(&def, pos, end, prefix) != 0)
			return -1;
		if (swim_upsert_member(swim, &def, &member) != 0) {
			/*
			 * Not a critical error. Other members
			 * still can be updated.
			 */
			diag_log();
		}
	}
	return 0;
}

/** Process a new message. */
static void
swim_on_input(struct swim_scheduler *scheduler, const char *pos,
	      const char *end, const struct sockaddr_in *src)
{
	(void) src;
	const char *prefix = "invalid message:";
	struct swim *swim = swim_by_scheduler(scheduler);
	struct tt_uuid uuid;
	uint32_t size;
	if (swim_decode_map(&pos, end, &size, prefix, "root") != 0)
		goto error;
	if (size == 0) {
		diag_set(SwimError, "%s body can not be empty", prefix);
		goto error;
	}
	uint64_t key;
	if (swim_decode_uint(&pos, end, &key, prefix, "a key") != 0)
		goto error;
	if (key != SWIM_SRC_UUID) {
		diag_set(SwimError, "%s first key should be source UUID",
			 prefix);
		goto error;
	}
	if (swim_decode_uuid(&uuid, &pos, end, prefix, "source uuid") != 0)
		goto error;
	--size;
	for (uint32_t i = 0; i < size; ++i) {
		if (swim_decode_uint(&pos, end, &key, prefix, "a key") != 0)
			goto error;
		switch(key) {
		case SWIM_ANTI_ENTROPY:
			if (swim_process_anti_entropy(swim, &pos, end) != 0)
				goto error;
			break;
		default:
			diag_set(SwimError, "%s unexpected key", prefix);
			goto error;
		}
	}
	return;
error:
	diag_log();
}

struct swim *
swim_new(void)
{
	struct swim *swim = (struct swim *) calloc(1, sizeof(*swim));
	if (swim == NULL) {
		diag_set(OutOfMemory, sizeof(*swim), "calloc", "swim");
		return NULL;
	}
	swim->members = mh_swim_table_new();
	if (swim->members == NULL) {
		free(swim);
		diag_set(OutOfMemory, sizeof(*swim->members),
			 "mh_swim_table_new", "members");
		return NULL;
	}
	rlist_create(&swim->round_queue);
	swim_ev_timer_init(&swim->round_tick, swim_begin_step,
			   HEARTBEAT_RATE_DEFAULT, 0);
	swim->round_tick.data = (void *) swim;
	swim_task_create(&swim->round_step_task, swim_complete_step, NULL,
			 "round packet");
	swim_scheduler_create(&swim->scheduler, swim_on_input);
	return swim;
}

/**
 * Parse URI, filter out everything but IP addresses and ports,
 * and fill a struct sockaddr_in.
 */
static inline int
swim_uri_to_addr(const char *uri, struct sockaddr_in *addr,
		 const char *prefix)
{
	struct sockaddr_storage storage;
	if (sio_uri_to_addr(uri, (struct sockaddr *) &storage) != 0)
		return -1;
	if (storage.ss_family != AF_INET) {
		diag_set(IllegalParams, "%s only IP sockets are supported",
			 prefix);
		return -1;
	}
	*addr = *((struct sockaddr_in *) &storage);
	if (addr->sin_addr.s_addr == 0) {
		diag_set(IllegalParams, "%s INADDR_ANY is not supported",
			 prefix);
		return -1;
	}
	return 0;
}

int
swim_cfg(struct swim *swim, const char *uri, double heartbeat_rate,
	 const struct tt_uuid *uuid)
{
	const char *prefix = "swim.cfg:";
	struct sockaddr_in addr;
	if (uri != NULL && swim_uri_to_addr(uri, &addr, prefix) != 0)
		return -1;
	bool is_first_cfg = swim->self == NULL;
	if (is_first_cfg) {
		if (uuid == NULL || tt_uuid_is_nil(uuid) || uri == NULL) {
			diag_set(SwimError, "%s UUID and URI are mandatory in "\
				 "a first config", prefix);
			return -1;
		}
		swim->self = swim_new_member(swim, &addr, uuid, MEMBER_ALIVE);
		if (swim->self == NULL)
			return -1;
	} else if (uuid == NULL || tt_uuid_is_nil(uuid)) {
		uuid = &swim->self->uuid;
	} else if (! tt_uuid_is_equal(uuid, &swim->self->uuid)) {
		if (swim_find_member(swim, uuid) != NULL) {
			diag_set(SwimError, "%s a member with such UUID "\
				 "already exists", prefix);
			return -1;
		}
		/*
		 * Reserve one cell for reinsertion of self with a
		 * new UUID. Reserve is necessary right here, not
		 * later, for atomic reconfiguration. Without
		 * reservation in that place it is possible that
		 * the instance is bound to a new URI, but failed
		 * to update UUID due to memory issues.
		 */
		if (swim_reserve_one_member(swim) != 0)
			return -1;
	}
	if (uri != NULL) {
		/*
		 * Bind is smart - it does nothing if the address
		 * was not changed.
		 */
		if (swim_scheduler_bind(&swim->scheduler, &addr) != 0) {
			if (is_first_cfg) {
				swim_delete_member(swim, swim->self);
				swim->self = NULL;
			}
			return -1;
		}
		/*
		 * A real address can be different from a one
		 * passed by user. For example, if 0 port was
		 * specified.
		 */
		addr = swim->scheduler.transport.addr;
	} else {
		addr = swim->self->addr;
	}
	if (swim->round_tick.at != heartbeat_rate && heartbeat_rate > 0)
		swim_ev_timer_set(&swim->round_tick, heartbeat_rate, 0);

	swim_update_member_addr(swim, swim->self, &addr);
	int rc = swim_update_member_uuid(swim, swim->self, uuid);
	/* Reserved above. */
	assert(rc == 0);
	(void) rc;
	return 0;
}

bool
swim_is_configured(const struct swim *swim)
{
	return swim->self != NULL;
}

int
swim_add_member(struct swim *swim, const char *uri, const struct tt_uuid *uuid)
{
	const char *prefix = "swim.add_member:";
	assert(swim_is_configured(swim));
	if (uri == NULL || uuid == NULL || tt_uuid_is_nil(uuid)) {
		diag_set(SwimError, "%s URI and UUID are mandatory", prefix);
		return -1;
	}
	struct sockaddr_in addr;
	if (swim_uri_to_addr(uri, &addr, prefix) != 0)
		return -1;
	struct swim_member *member = swim_find_member(swim, uuid);
	if (member == NULL) {
		member = swim_new_member(swim, &addr, uuid, MEMBER_ALIVE);
		return member == NULL ? -1 : 0;
	}
	diag_set(SwimError, "%s a member with such UUID already exists",
		 prefix);
	return -1;
}

int
swim_remove_member(struct swim *swim, const struct tt_uuid *uuid)
{
	assert(swim_is_configured(swim));
	const char *prefix = "swim.remove_member:";
	if (uuid == NULL || tt_uuid_is_nil(uuid)) {
		diag_set(SwimError, "%s UUID is mandatory", prefix);
		return -1;
	}
	struct swim_member *member = swim_find_member(swim, uuid);
	if (member == NULL)
		return 0;
	if (member == swim->self) {
		diag_set(SwimError, "%s can not remove self", prefix);
		return -1;
	}
	swim_delete_member(swim, member);
	return 0;
}

void
swim_info(struct swim *swim, struct info_handler *info)
{
	info_begin(info);
	for (mh_int_t node = mh_first(swim->members),
	     end = mh_end(swim->members); node != end;
	     node = mh_next(swim->members, node)) {
		struct swim_member *m =
			*mh_swim_table_node(swim->members, node);
		info_table_begin(info,
				 sio_strfaddr((struct sockaddr *) &m->addr,
					      sizeof(m->addr)));
		info_append_str(info, "status",
				swim_member_status_strs[m->status]);
		info_append_str(info, "uuid", swim_uuid_str(&m->uuid));
		info_table_end(info);
	}
	info_end(info);
}

void
swim_delete(struct swim *swim)
{
	swim_scheduler_destroy(&swim->scheduler);
	swim_ev_timer_stop(loop(), &swim->round_tick);
	swim_task_destroy(&swim->round_step_task);
	mh_int_t node;
	mh_foreach(swim->members, node) {
		struct swim_member *m =
			*mh_swim_table_node(swim->members, node);
		rlist_del_entry(m, in_round_queue);
		swim_member_delete(m);
	}
	mh_swim_table_delete(swim->members);
	free(swim->shuffled);
}

const struct swim_member *
swim_self(struct swim *swim)
{
	assert(swim_is_configured(swim));
	return swim->self;
}

const struct swim_member *
swim_member_by_uuid(struct swim *swim, const struct tt_uuid *uuid)
{
	assert(swim_is_configured(swim));
	return swim_find_member(swim, uuid);
}

struct swim_iterator *
swim_iterator_open(struct swim *swim)
{
	assert(swim_is_configured(swim));
	swim->iterator = mh_first(swim->members);
	return (struct swim_iterator *) swim;
}

const struct swim_member *
swim_iterator_next(struct swim_iterator *iterator)
{
	struct swim *swim = (struct swim *) iterator;
	assert(swim_is_configured(swim));
	struct mh_swim_table_t *t = swim->members;
	if (swim->iterator == mh_end(t))
		return NULL;
	struct swim_member *m = *mh_swim_table_node(t, swim->iterator);
	swim->iterator = mh_next(t, swim->iterator);
	return m;
}

void
swim_iterator_close(struct swim_iterator *iterator)
{
	(void) iterator;
}

const char *
swim_member_uri(const struct swim_member *member)
{
	return sio_strfaddr((const struct sockaddr *) &member->addr,
			    sizeof(member->addr));
}

const struct tt_uuid *
swim_member_uuid(const struct swim_member *member)
{
	return &member->uuid;
}
