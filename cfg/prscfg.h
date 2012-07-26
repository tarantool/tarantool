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
		scalarType 	= 0,
		structType 	= 1,
		arrayType	= 2 
	} paramType;

	int optional;

	union {
		char		*scalarval;
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
	CNF_NOTSET,
	CNF_OPTIONAL,
	CNF_INTERNALERROR
} ConfettyError;

#endif
