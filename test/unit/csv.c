#include "csv/csv.h"
#include "unit.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void
print_endl(void *ctx)
{
	fflush(stdout);
	puts("");
}
void
print_field(void *ctx, const char *s, const char *end)
{
	putchar('|');
	for(const char *p = s; p != end && *p; p++) {
		if((*p == '\r' || *p == '\n') && (p + 1 == end || (*(p + 1) != '\r' && *(p + 1) != '\n')))
			putchar('\n');
		else
			putchar(*p);
	}
	putchar('|');
	putchar('\t');
	fflush(stdout);
}

void small_string_test(const char* const s)
{
	struct csv csv;
	csv_create(&csv);
	csv.emit_field = print_field;
	csv.emit_row = print_endl;
	csv_parse_chunk(&csv, s, s + strlen(s));
	csv_finish_parsing(&csv);
	printf("valid: %s\n", csv.csv_invalid ? "NO" : "yes");
	csv_destroy(&csv);
}

void
common_test(const char *data)
{
	header();
	small_string_test(data);
	footer();
}


void test1() {
	header();
	small_string_test("1\n \n1,2,3\n123\n");
	footer();
}
void test2() {
	header();
	small_string_test(
				"123,456,abcac,\'multiword field 4\'\n"
				"none,none,0\n"
				",,\n"
				",,"
				);
	footer();
}

void test3() {
	header();
	small_string_test("1,,2");
	footer();
}

void test4() {
	header();
	small_string_test("123 , 5  ,       92    , 0, 0\n"
			  "1, 12  34, 56, \"quote , \", 66\nok");
	footer();
}
void test5() {
	header();
	const char * const s = "abc\tlonglonglonglonglonglonglonglonglonglonglonglonglonglonglonglonglonglong\t0\n"
			       "123\t456\t\n" "0\t\t\n";
	struct csv csv;
	csv_create(&csv);
	csv.emit_field = print_field;
	csv.emit_row = print_endl;
	csv_setopt(&csv, CSV_OPT_DELIMITER, '\t');
	csv_parse_chunk(&csv, s, s + strlen(s));
	csv_finish_parsing(&csv);
	printf("valid: %s\n", csv.csv_invalid ? "NO" : "yes");
	csv_destroy(&csv);
	footer();
}

void test6() {
	header();
	const char * const s1 = "\n \nabc\nc\"\",\"d\",de\n\nk";
	const char * const s2 = "\ne\n\n \n\" \"\n\"quote isn't closed, sorry\n \noh";
	struct csv csv;
	csv_create(&csv);
	csv.emit_field = print_field;
	csv.emit_row = print_endl;
	csv_parse_chunk(&csv, s1, s1 + strlen(s1));
	csv_parse_chunk(&csv, s2, s2 + 2);
	csv_parse_chunk(&csv, s2 + 2, s2 + strlen(s2));
	csv_finish_parsing(&csv);
	printf("valid: %s\n", csv.csv_invalid ? "NO" : "yes");
	csv_destroy(&csv);
	footer();
}

struct counter {
	size_t line_cnt, fieldsizes_cnt;
};

void
line_counter(void *ctx)
{
	((struct counter*)ctx)->line_cnt++;
}

void
fieldsizes_counter(void *ctx, const char *s, const char *end)
{
	((struct counter*)ctx)->fieldsizes_cnt += end - s;
}

void big_chunk_separated_test() {
	header();
	struct csv csv;
	csv_create(&csv);
	csv_setopt(&csv, CSV_OPT_EMIT_FIELD, fieldsizes_counter);
	csv_setopt(&csv, CSV_OPT_EMIT_ROW, line_counter);

	size_t lines = 10000;
	size_t linelen = 300;
	size_t chunk_size = 1024;

	char *buf = malloc(lines * (linelen+4));
	size_t bufn = 0;

	struct counter cnt;
	cnt.line_cnt = 0;
	cnt.fieldsizes_cnt = 0;
	csv_setopt(&csv, CSV_OPT_CTX, &cnt);

	const char *s = "abc, def, def, cba";
	for(size_t i = 0; i < lines; i++) {
		int k =  linelen / strlen(s);
		for(int i = 0; i < k; i++) {
			memcpy(buf + bufn, s, strlen(s));
			bufn += strlen(s);
		}
		buf[bufn++] = '\n';
	}

	const char *bufp = buf;
	while(bufp < buf + bufn - chunk_size) {
		csv_parse_chunk(&csv, bufp, bufp + chunk_size);
		bufp += chunk_size;
	}
	csv_parse_chunk(&csv, bufp, buf + bufn);
	csv_finish_parsing(&csv);

	//without fieldsizes counts without commas and spaces
	printf("line_cnt=%d, fieldsizes_cnt=%d, %d\n", (int)cnt.line_cnt, (int)cnt.fieldsizes_cnt,
	       (int) (lines * (strlen(s) - 6) * (linelen / strlen(s))));
	assert(lines == cnt.line_cnt);
	assert(lines * (strlen(s) - 6) * (linelen / strlen(s))  == cnt.fieldsizes_cnt);
	csv_destroy(&csv);
	free(buf);
	footer();
}

void random_generated_test() {
	header();
	small_string_test(
				"\n\r\" ba\r a\ra, \n\"\n\"a\nb\" \raa\rb,\n"
				"\r, \n\",\r\n\"\n,a, ,\"a\n\n\r \"\r ba\r,b"
				"  a,\n,\"\"a\n\r \"b\"   \n,\",a\r,a ,\r\rc"
				"\" a,b\r\n,\"b\r\"aa  \nb \n\r\r\n\n,\rb\nc"
				",\n\n aa\n \"\n ab\rab,\r\" b\n\",   ,,\r\r"
				"bab\rb\na\n\"a\ra,\"\",\n\"a\n\n \"\r \ra\n"
				"a\r\raa a\" ,baab ,a \rbb   ,\r \r,\rb,,  b"
				"\n\r\"\nb\n\nb \n,ab \raa\r\"\nb a\"ba,b, c"
				"\"a\"a \"\r\n\"b \n,b\"\",\nba\n\" \n\na \r"
				"\nb\rb\"bbba,\" \n\n\n,a,b,a,b,\n\n\n\nb\"\r"
				);

	footer();
}

void iter_test1() {
	header();
	struct csv_iterator it;
	struct csv csv;
	csv_create(&csv);
	csv_iter_create(&it, &csv);
	int st = 0;
	const char *buf = ",d ,e\r\n12,42,3\no\n";
	while((st = csv_next(&it)) != CSV_IT_EOF) {
		switch(st) {
		case CSV_IT_NEEDMORE:
			csv_feed(&it, buf);
			buf += strlen(buf);
			break;
		case CSV_IT_EOL:
			printf("\n");
			break;
		case CSV_IT_OK:
			print_field(0, it.field, it.field + it.field_len);
			break;
		case CSV_IT_ERROR:
			printf("\nerror");
			break;
		}
	}
	csv_destroy(&csv);
	footer();
}

void iter_test2() {
	header();
	struct csv_iterator it;
	struct csv csv;
	csv_create(&csv);
	csv_iter_create(&it, &csv);
	int st = 0;
	const char ar[] = {'1', '\n', 0, '2', '3', 0, 0};
	const char *buf = ar;
	while((st = csv_next(&it)) != CSV_IT_EOF) {
		switch(st) {
		case CSV_IT_NEEDMORE:
			csv_feed(&it, buf);
			buf += 3;
			break;
		case CSV_IT_EOL:
			printf("\n");
			break;
		case CSV_IT_OK:
			print_field(0, it.field, it.field + it.field_len);
			break;
		case CSV_IT_ERROR:
			printf("\nerror");
			break;
		}
	}
	csv_destroy(&csv);
	footer();
}

void csv_out() {
	header();

	const char fields[4][36] = { "abc", "with,comma", "\"in quotes\"", "1 \" quote"};
	char buf[18];
	int i;
	struct csv csv;
	csv_create(&csv);
	for(i = 0; i < 4; i++) {
		int len = csv_escape_field(&csv, fields[i], buf);
		printf("%s<len=%d>%c", buf, len, i == 3 ? '\n' : ',');
	}

	footer();
}

int main() {
	test1();
	test2();
	test3();
	test4();
	test5();
	test6(); // blank lines, invalid csv
	big_chunk_separated_test();
	random_generated_test();
	/* comma in quotes */
	common_test(
				"first,last,address,city,zip\n"
				"John,Doe,120 any st.,\"Anytown, WW\",08123\n"
				);

	/* empty fields */
	common_test(
				"a,b,c\n"
				"1,\"\",\"\"\n"
				"2,3,4\n"
				);

	/* escaped quotes */
	common_test(
				"a,b\n"
				"1,\"ha \"\"ha\"\" ha\"\n"
				"3,4\n"
				);

	/* json in csv */
	common_test(
				"key,val\n"
				"1,\"{\"\"type\"\": \"\"Point\"\", \"\"coordinates\"\": [102.0, 0.5]}\"\n"
				);

	/* new lines */
	common_test(
				"a,b,c\n"
				"1,2,3\n"
				"\"Once upon \n"
				"a time\",5,6\n"
				"7,8,9\n"
				);

	/* new lines with quetes */
	common_test(
				"a,b\n"
				"1,\"ha\n"
				"\"\"ha\"\"\n"
				"ha\"\n"
				"3,4\n"
				);

	/* utf8 */
	common_test(
				" a,b,c\n"
				"1,2,3\n"
				"4,5,а нет ли ошибок?\n"
				);
	/* ending spaces */
	common_test("  www  , \"aa\"a , \"tt  \" \n");


	//iterator tests
	iter_test1();
	iter_test2();

	//output test
	csv_out();
	return 0;
}
