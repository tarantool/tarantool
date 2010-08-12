#ifndef PRSCFG_H
#define PRSCFG_H

#include <stdio.h>

typedef struct NameAtom {
	char			*name;
	int				index;
	struct NameAtom *next;
} NameAtom;

typedef struct OptDef {

	enum {
		numberType 	= 0,
		stringType 	= 1,
		structType 	= 2,
		arrayType	= 3 
	} paramType;

	union {
		char			*numberval;
		char			*stringval;
		struct OptDef 	*structval;
		struct OptDef 	*arrayval;
	} paramValue;

	NameAtom		*name;

	struct OptDef	*parent;
	struct OptDef	*next;
} OptDef;

OptDef* parseCfgDef(FILE *fh);
OptDef* parseCfgDefBuffer(char *buffer);
void 	freeCfgDef(OptDef *def);

typedef	enum ConfettyError {
	CNF_OK = 0,
	CNF_MISSED, 
	CNF_WRONGTYPE,
	CNF_WRONGINDEX,
	CNF_RDONLY,
	CNF_WRONGINT,
	CNF_WRONGRANGE,
	CNF_NOMEMORY,
	CNF_SYNTAXERROR,
	CNF_INTERNALERROR
} ConfettyError;

#endif
