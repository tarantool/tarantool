/*
 * Copyright (c) 2024 VK Company Limited. All Rights Reserved.
 *
 * The information and source code contained herein is the exclusive property
 * of VK Company Limited and may not be disclosed, examined, or reproduced in
 * whole or in part without explicit written authorization from the Company.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "field_def.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct ArrowArray;
struct ArrowSchema;
struct ArrowArrayStream;

struct space;
struct field_def;
struct key_def;

/**
 * Returns arrow format string for the given field type, or NULL if it is not
 * supported. For details see:
 * https://arrow.apache.org/docs/format/CDataInterface.html#data-type-description-format-strings
 */
const char *
field_type_to_arrow_type(enum field_type field_type, bool use_view_layout);

/**
 * Validates arrow batch against space:
 * - arrow schema does not contain duplicate names
 * - arrow column type corresponds to space column type
 * - arrow column does not have nulls if space is column is not nullable
 * - space columns not present in the batch are nullable
 *
 * Returns 0 on success and -1 on validation error (diag is set).
 *
 * Additionally, resolves columns in `schema' into field numbers in `space'.
 * The result is saved in `fields', which must have the capacity to store
 * `schema->n_children' entries.
 */
int
arrow_validate_batch(struct ArrowArray *array, struct ArrowSchema *schema,
		     struct space *space, uint32_t *fields);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
