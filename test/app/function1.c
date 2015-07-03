#include "tarantool.h"

int
function1(struct request *request, struct port *port)
{
	say_info("-- function1 -  called --");
	return 0;
}
