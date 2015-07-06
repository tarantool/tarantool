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
    for(const char *p = s; p != end && *p; p++)
        putchar(*p);
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
    csv_parse(&csv, s, s + strlen(s));
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

void test_chunk(const char* const s)
{
    header();
    struct csv csv;
    csv_create(&csv);
    csv.emit_field = NULL;
    csv.emit_row = NULL;
    printf("tail: %s\n", csv_parse_chunk(&csv, s, s + strlen(s)));
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
    csv.emit_field = fieldsizes_counter;
    csv.emit_row = line_counter;

    size_t lines = 10000;
    size_t linelen = 10000;
    size_t chunk_size = 1024*1024;

    char *buf = malloc(lines * (linelen+4));
    size_t bufn = 0;

    struct counter cnt;
    cnt.line_cnt = 0;
    cnt.fieldsizes_cnt = 0;
    csv.emit_ctx = &cnt;

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
    while(bufp < buf + bufn - chunk_size)
        bufp = csv_parse_chunk(&csv, bufp, bufp + chunk_size);
    csv_parse(&csv, bufp, buf + bufn);

    //without fieldsizes counts without commas and spaces
    printf("line_cnt=%d, fieldsizes_cnt=%d, %d\n", (int)cnt.line_cnt, (int)cnt.fieldsizes_cnt,
           (int) (lines * (strlen(s) - 6) * (linelen / strlen(s))));
    assert(lines == cnt.line_cnt);
    assert(lines * (strlen(s) - 6) * (linelen / strlen(s))  == cnt.fieldsizes_cnt);
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
                "\nb\rb\"bbba,\" \n\n\n,a,b,a,b,\n\n\n\nb \r"
                );

    footer();
}
int main()
{
    test1();
    test2();
    test3();
    test4();
    test_chunk("123 , 5  ,       92    , 0, "
               " 0\n1, 12  34, 56, \"quote , \", 66\nok");
    big_chunk_separated_test();
    random_generated_test();
    return 0;
}
