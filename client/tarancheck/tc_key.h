#ifndef TC_KEY_H_INCLUDED
#define TC_KEY_H_INCLUDED

struct tc_key_field {
	int offset;
	int size;
};

struct tc_key {
	uint32_t crc;
	size_t size;
	struct tc_key_field i[];
};

#define TC_KEY_DATA(K, I) ((char*)(K) + sizeof(struct tc_key) + (K)->i[(I)].offset)
#define TC_KEY_SIZE(K, I) ((K)->i[(I)].size)

#endif
