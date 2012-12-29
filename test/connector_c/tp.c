
#include <stdio.h>
#include <tp.h>

int main(void)
{
	char buf[128];

	struct tp req;
	tp_init(&req, buf, sizeof(buf), NULL);

	/*
	tp_insert(&req, 0, 0);
	tp_tuple(&req);
	tp_sz(&req, "key");
	tp_sz(&req, "value");
	*/

	/*
	tp_select(&req, 0, 0, 0, 1);
	tp_tuple(&req);
	tp_sz(&req, "key");
	tp_sz(&req, "key");
	*/

	/*
	tp_update(&req, 0, 0);
	tp_tuple(&req);
	tp_sz(&req, "key");
	tp_updatebegin(&req);
	tp_op(&req, 1, TP_OPEQ, "VALUE", 5);
	*/

	tp_call(&req, 0, "hello_proc");
	tp_tuple(&req);

	fwrite(buf, tp_used(&req), 1, stdout);
	return 0;
}
