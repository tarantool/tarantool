#ifndef TS_KEY_H_INCLUDED
#define TS_KEY_H_INCLUDED

#define TS_KEY_WITH_DATA 1

struct ts_key {
	uint16_t file;
	uint64_t offset;
	uint8_t flags;
	unsigned char key[];
} __attribute__((packed));

#endif
