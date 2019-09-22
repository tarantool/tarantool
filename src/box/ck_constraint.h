#ifndef INCLUDES_BOX_CK_CONSTRAINT_H
#define INCLUDES_BOX_CK_CONSTRAINT_H
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

#include <stdint.h>
#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct space;
struct space_def;
struct sql_stmt;
struct Expr;
struct trigger;

/** Supported languages of ck constraint. */
enum ck_constraint_language {
  CK_CONSTRAINT_LANGUAGE_SQL,
  ck_constraint_language_MAX,
};

/** The supported languages strings.  */
extern const char *ck_constraint_language_strs[];

/**
 * Check constraint definition.
 * See ck_constraint_def_sizeof() definition for implementation
 * details and memory layout.
 */
struct ck_constraint_def {
	/**
	 * The 0-terminated string that defines check constraint
	 * expression.
	 *
	 * For instance: "field1 + field2 > 2 * 3".
	 */
	char *expr_str;
	/**
	 * The id of the space this check constraint is
	 * defined for.
	 */
	uint32_t space_id;
	/**
	 * Per constraint option regulating its execution: it is
	 * disabled (set to false) contraint won't be fired.
	 */
	bool is_enabled;
	/** The language of ck constraint. */
	enum ck_constraint_language language;
	/**
	 * The 0-terminated string, a name of the check
	 * constraint. Must be unique for a given space.
	 */
	char name[0];
};

/**
 * Structure representing ck constraint.
 * See ck_constraint_new() definition.
 */
struct ck_constraint {
	/** The check constraint definition. */
	struct ck_constraint_def *def;
	/**
	 * Precompiled reusable VDBE program for processing check
	 * constraints and setting bad exitcode and error
	 * message when ck condition unsatisfied.
	 */
	struct sql_stmt *stmt;
	/**
	 * Organize check constraint structs into linked list
	 * with space::ck_constraint.
	 */
	struct rlist link;
};

/**
 * Calculate check constraint definition memory size and fields
 * offsets for given arguments.
 *
 * Alongside with struct ck_constraint_def itself, we reserve
 * memory for string containing its name and expression string.
 *
 * Memory layout:
 * +-----------------------------+ <- Allocated memory starts here
 * |   struct ck_constraint_def  |
 * |-----------------------------|
 * |          name + \0          |
 * |-----------------------------|
 * |        expr_str + \0        |
 * +-----------------------------+
 *
 * @param name_len The length of the name.
 * @param expr_str_len The length of the expr_str.
 * @param[out] expr_str_offset The offset of the expr_str string.
 * @return The size of the ck constraint definition object for
 *         given parameters.
 */
static inline uint32_t
ck_constraint_def_sizeof(uint32_t name_len, uint32_t expr_str_len,
			 uint32_t *expr_str_offset)
{
	*expr_str_offset = sizeof(struct ck_constraint_def) + name_len + 1;
	return *expr_str_offset + expr_str_len + 1;
}

/**
 * Create a new check constraint definition object with given
 * fields.
 *
 * @param name The name string of a new ck constraint definition.
 * @param name_len The length of @a name string.
 * @param expr The check expression string.
 * @param expr_str_len The length of the @a expr string.
 * @param space_id The identifier of the target space.
 * @param language The language of the @a expr string.
 * @param is_enabled Whether this ck constraint should be fired.
 * @retval not NULL Check constraint definition object pointer
 *                  on success.
 * @retval NULL Otherwise. The diag message is set.
*/
struct ck_constraint_def *
ck_constraint_def_new(const char *name, uint32_t name_len, const char *expr,
		      uint32_t expr_str_len, uint32_t space_id,
		      enum ck_constraint_language language, bool is_enabled);

/**
 * Destroy check constraint definition memory, release acquired
 * resources.
 * @param ck_def The check constraint definition object to
 *               destroy.
 */
void
ck_constraint_def_delete(struct ck_constraint_def *ck_def);

/**
 * Create a new check constraint object by given check constraint
 * definition and definition of the space this constraint is
 * related to.
 *
 * @param ck_constraint_def The check constraint definition object
 *                          to use. Expected to be allocated with
 *                          malloc. Ck constraint object manages
 *                          this allocation in case of successful
 *                          creation.
 * @param space_def The space definition of the space this check
 *                  constraint must be constructed for.
 * @retval not NULL Check constraint object pointer on success.
 * @retval NULL Otherwise. The diag message is set.
*/
struct ck_constraint *
ck_constraint_new(struct ck_constraint_def *ck_constraint_def,
		  struct space_def *space_def);

/**
 * Destroy check constraint memory, release acquired resources.
 * @param ck_constraint The check constraint object to destroy.
 */
void
ck_constraint_delete(struct ck_constraint *ck_constraint);

/**
 * Ck constraint trigger function. It is expected to be executed
 * in space::on_replace trigger.
 *
 * It performs all ck constraints defined for a given space
 * running the precompiled bytecode to test a new tuple
 * before it will be inserted in destination space.
 * The trigger data stores space identifier instead of space
 * pointer to make ck constraint independent of specific space
 * object version.
 *
 * Returns error code when some ck constraint is unsatisfied.
 * The diag message is set.
 */
int
ck_constraint_on_replace_trigger(struct trigger *trigger, void *event);

/**
 * Find check constraint object in space by given name and
 * name_len.
 * @param space The space to lookup check constraint.
 * @param name The check constraint name.
 * @param name_len The length of the name.
 * @retval Not NULL Ck constraint pointer if exists.
 * @retval NULL Otherwise.
 */
struct ck_constraint *
space_ck_constraint_by_name(struct space *space, const char *name,
			    uint32_t name_len);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif /* INCLUDES_BOX_CK_CONSTRAINT_H */
