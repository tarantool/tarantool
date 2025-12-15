struct sz12_t {
	float f1;
	float f2;
	float f3;
};

struct sz12_t retsz12(struct sz12_t a)
{
	return a;
}

struct sz12_t sum2sz12(struct sz12_t a, struct sz12_t b)
{
	struct sz12_t res = {0};
	res.f1 = a.f1 + b.f1;
	res.f2 = a.f2 + b.f2;
	res.f3 = a.f3 + b.f3;
	return res;
}

struct sz12_t sum3sz12(struct sz12_t a, struct sz12_t b, struct sz12_t c)
{
	struct sz12_t res = {0};
	res.f1 = a.f1 + b.f1 + c.f1;
	res.f2 = a.f2 + b.f2 + c.f2;
	res.f3 = a.f3 + b.f3 + c.f3;
	return res;
}
