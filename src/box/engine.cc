/*
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
#include "tuple.h"
#include "engine.h"
#include "space.h"
#include "exception.h"
#include "salad/rlist.h"
#include <stdlib.h>
#include <string.h>

static RLIST_HEAD(engines);

EngineFactory::EngineFactory(const char *engine_name)
	:name(engine_name),
	 link(RLIST_INITIALIZER(link))
{}

void EngineFactory::init()
{}

void EngineFactory::shutdown()
{}

void EngineFactory::close(Engine*)
{}

Engine::Engine(EngineFactory *f)
	:host(f)
{
	/* derive recovery state from engine factory */
	recover_derive();
}

Index*
Engine::createIndex(struct key_def *key_def)
{
	return host->createIndex(key_def);
}

/** Register engine factory instance. */
void engine_register(EngineFactory *engine)
{
	rlist_add_entry(&engines, engine, link);
}

/** Find factory engine by name. */
EngineFactory*
engine_find(const char *name)
{
	EngineFactory *e;
	rlist_foreach_entry(e, &engines, link) {
		if (strcmp(e->name, name) == 0)
			return e;
	}
	tnt_raise(LoggedError, ER_NO_SUCH_ENGINE, name);
}

/** Call a visitor function on every registered engine. */
void engine_foreach(void (*func)(EngineFactory *engine, void *udata),
                    void *udata)
{
	EngineFactory *e;
	rlist_foreach_entry(e, &engines, link)
		func(e, udata);
}

/** Shutdown all engine factories. */
void engine_shutdown()
{
	EngineFactory *e, *tmp;
	rlist_foreach_entry_safe(e, &engines, link, tmp) {
		e->shutdown();
		delete e;
	}
}
