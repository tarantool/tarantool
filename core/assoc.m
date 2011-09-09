#define MH_SOURCE 1
#include <assoc.h>


int XX__ac_X31_hash_str(void *s)
{
	int l;
	l = strlen(s);
	int h = 0;
	if (l)
		for (; l--; s++)
			h = (h << 5) - h + *(u8 *)s;
	return h;
}
