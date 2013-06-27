#ifndef TS_REGION_H_
#define TS_REGION_H_

struct ts_region {
	char *buf;
	unsigned int bottom;
	unsigned int top;
};

static inline void
ts_region_init(struct ts_region *r) {
	r->buf = NULL;
	r->bottom = 0;
	r->top = 0;
}

static inline void
ts_region_free(struct ts_region *r) {
	if (r->buf) {
		free(r->buf);
		r->buf = NULL;
	}
}

static inline void
ts_region_reset(struct ts_region *r) {
	r->bottom = 0;
}

static inline void*
ts_region_alloc(struct ts_region *r, unsigned int size) {
	if ((r->bottom + size) > r->top) {
		int top = r->top;
		if (top < size)
			top = size;
		top *= 2;
		void *p = realloc(r->buf, top);
		if (p == NULL)
			return NULL;
		r->top = top;
		r->buf = (char*)p;
	}
	void *ret = r->buf + r->bottom;
	r->bottom += size;
	return ret;
}

#endif
