#include "tarantool.h"
#include <stdio.h>

int
function1(struct request *request, struct port *port)
{
	say_info("-- function1 -  called --");
	printf("ok - function1\n");
	return 0;
}

int
test(struct request *request, struct port *port)
{
	say_info("-- test  -  called --");
	printf("ok - test\n");
	return 0;
}
