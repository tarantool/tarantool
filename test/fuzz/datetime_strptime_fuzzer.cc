#include <fuzzer/FuzzedDataProvider.h>
#include <stddef.h>

#include "datetime.h"

extern "C" void
cord_on_yield(void) {}

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	FuzzedDataProvider fdp(data, size);

	auto buf = fdp.ConsumeRandomLengthString();
	auto fmt = fdp.ConsumeRandomLengthString();

	struct datetime date_expected;
	datetime_strptime(&date_expected, buf.c_str(), fmt.c_str());

	return 0;
}
