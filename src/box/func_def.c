#include "func_def.h"
#include "string.h"

const char *func_language_strs[] = {"LUA", "C"};

int
func_def_cmp(struct func_def *def1, struct func_def *def2)
{
	if (def1->fid != def2->fid)
		return def1->fid - def2->fid;
	if (def1->uid != def2->uid)
		return def1->uid - def2->uid;
	if (def1->setuid != def2->setuid)
		return def1->setuid - def2->setuid;
	if (def1->language != def2->language)
		return def1->language - def2->language;
	if (strcmp(def1->name, def2->name) != 0)
		return strcmp(def1->name, def2->name);
	return 0;
}
