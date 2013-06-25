#ifndef TS_KEY_H_INCLUDED
#define TS_KEY_H_INCLUDED

#define TS_KEY_WITH_DATA 1

struct ts_key {
	uint32_t file;
	uint32_t offset;
	uint8_t flags;
	unsigned char key[];
} __attribute__((packed));

#endif
