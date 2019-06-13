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
	if (def1->is_deterministic != def2->is_deterministic)
		return def1->is_deterministic - def2->is_deterministic;
	if (def1->is_sandboxed != def2->is_sandboxed)
		return def1->is_sandboxed - def2->is_sandboxed;
	if (strcmp(def1->name, def2->name) != 0)
		return strcmp(def1->name, def2->name);
	if ((def1->body != NULL) != (def2->body != NULL))
		return def1->body - def2->body;
	if (def1->body != NULL && strcmp(def1->body, def2->body) != 0)
		return strcmp(def1->body, def2->body);
	if ((def1->comment != NULL) != (def2->comment != NULL))
		return def1->comment - def2->comment;
	if (def1->comment != NULL && strcmp(def1->comment, def2->comment) != 0)
		return strcmp(def1->comment, def2->comment);
	return 0;
}
