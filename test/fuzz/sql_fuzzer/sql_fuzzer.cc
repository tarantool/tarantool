#include <string>
#include <iostream>

#include <libprotobuf-mutator/port/protobuf.h>
#include <libprotobuf-mutator/src/libfuzzer/libfuzzer_macro.h>
#include "sql_query.pb.h"
#include "sql_query_proto_to_string.h"

#include "event.h"
#include "fiber.h"
#include "memory.h"
#include "coll/coll.h"
#include "box/box.h"
#include "box/sql.h"

#define SUCCESS 0

__attribute__((constructor))
static void
setup()
{
	memory_init();
	fiber_init(fiber_cxx_invoke);
	coll_init();
	event_init();
	box_init();
	sql_init();
}

__attribute__((destructor))
static void
teardown()
{
	box_free();
	event_free();
	coll_free();
	fiber_free();
	memory_free();
}

DEFINE_PROTO_FUZZER(const sql_query::SQLQuery &query) {
	std::string query_str = sql_fuzzer::SQLQueryToString(query);

	if (::getenv("LPM_DUMP_NATIVE_INPUT") && query_str.size() != 0) {
		std::cout << "_________________________" << std::endl;
		std::cout << query_str << std::endl;
		std::cout << "-------------------------" << std::endl;
	}

	if (query_str.length() == 0)
		return;

	sql_fuzz(query_str.data(), query_str.size());
}
