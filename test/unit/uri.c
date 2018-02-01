#include "unit.h"
#include <uri.h>
#include <string.h>

int
test(const char *s, const char *scheme, const char *login, const char *password,
     const char *host, const char *service, const char *path,
     const char *query, const char *fragment, int host_hint)
{
	plan(19);

	struct uri uri;
	is(uri_parse(&uri, s), 0, "%s: parse", s);
	/* fprintf(stdout, #key ": %p %d %.*s\n", uri.key,
	   (int) uri.key ## _len, (int) uri.key ## _len, uri.key); */

#define chk(key) do { \
	ok((key && uri.key && strlen(key) == uri.key ## _len && \
	   memcmp(key, uri.key, uri.key ## _len) == 0) || \
	   (!key && !uri.key), "%s: " #key, s); } while (0);

	chk(scheme);
	chk(login);
	chk(password);
	chk(host);
	chk(service);
	chk(path);
	chk(query);
	chk(fragment);
	is(uri.host_hint, host_hint, "%s: host_hint", s);

	char str1[1024];
	uri_format(str1, sizeof(str1), &uri, true);
	is(uri_parse(&uri, s), 0, "%s: parse", s);
	chk(scheme);
	chk(login);
	chk(password);
	chk(host);
	chk(service);
	chk(path);
	chk(query);
	chk(fragment);

#undef chk

	return check_plan();
}

int
test_invalid()
{
	plan(2);

	/* Invalid */
	struct uri u;
	isnt(uri_parse(&u, ""), 0 , "empty is invalid");
	isnt(uri_parse(&u, "://"), 0 , ":// is invalid");

	return check_plan();
}

int
main(void)
{
	plan(63);

	/* General */
	test("host", NULL, NULL, NULL, "host", NULL, NULL, NULL, NULL, 0);
	test("host/", NULL, NULL, NULL, "host", NULL, "/", NULL, NULL, 0);
	test("host/path1/path2/path3", NULL, NULL, NULL, "host", NULL,
	     "/path1/path2/path3", NULL, NULL, 0);
	test("host/path1/path2/path3?q1=v1&q2=v2#fragment", NULL, NULL,
	     NULL, "host", NULL, "/path1/path2/path3",
	     "q1=v1&q2=v2", "fragment", 0);

	test("host:service", NULL, NULL, NULL, "host", "service", NULL, NULL,
	     NULL, 0);

	test("host:service/", NULL, NULL, NULL, "host", "service", "/", NULL,
	     NULL, 0);

	test("host:service/path1/path2/path3", NULL, NULL, NULL, "host",
	     "service", "/path1/path2/path3", NULL, NULL, 0);
	test("host:service/path1/path2/path3?q1=v1&q2=v2#fragment", NULL, NULL,
	     NULL, "host", "service", "/path1/path2/path3",
	     "q1=v1&q2=v2", "fragment", 0);

	test("login@host", NULL, "login", NULL, "host", NULL, NULL, NULL,
	     NULL, 0);
	test("login@host/", NULL, "login", NULL, "host", NULL, "/", NULL,
	     NULL, 0);
	test("login@host/path1/path2/path3", NULL, "login", NULL, "host", NULL,
	     "/path1/path2/path3", NULL, NULL, 0);
	test("login@host/path1/path2/path3?q1=v1&q2=v2#fragment", NULL, "login",
	     NULL, "host", NULL, "/path1/path2/path3",
	     "q1=v1&q2=v2", "fragment", 0);

	test("login:password@host", NULL, "login", "password", "host", NULL,
	     NULL, NULL, NULL, 0);
	test("login:@host", NULL, "login", "", "host", NULL,
	     NULL, NULL, NULL, 0);
	test("login:password@host/", NULL, "login", "password", "host", NULL,
	     "/", NULL, NULL, 0);
	test("login:password@host/path1/path2/path3", NULL, "login", "password",
	     "host", NULL, "/path1/path2/path3", NULL, NULL, 0);
	test("login:password@host/path1/path2/path3?q1=v1&q2=v2#fragment",
	     NULL, "login", "password", "host", NULL, "/path1/path2/path3",
	     "q1=v1&q2=v2", "fragment", 0);

	test("login:password@host:service", NULL, "login", "password", "host",
	     "service", NULL, NULL, NULL, 0);
	test("login:password@host:service/", NULL, "login", "password", "host",
	     "service", "/", NULL, NULL, 0);
	test("login:password@host:service/path1/path2/path3", NULL, "login",
	     "password", "host", "service", "/path1/path2/path3", NULL,
	     NULL, 0);
	test("login:password@host:service/path1/path2/path3?q1=v1&q2=v2"
	     "#fragment", NULL, "login", "password", "host", "service",
	     "/path1/path2/path3", "q1=v1&q2=v2", "fragment", 0);

	test("scheme://login:password@host:service", "scheme", "login",
	     "password", "host", "service", NULL, NULL, NULL, 0);
	test("scheme://login:password@host:service/", "scheme", "login",
	     "password", "host", "service", "/", NULL, NULL, 0);
	test("scheme://login:password@host:service/path1/path2/path3", "scheme",
	     "login", "password", "host", "service", "/path1/path2/path3",
	     NULL, NULL, 0);
	test("scheme://login:password@host:service/path1/path2/path3?"
	     "q1=v1&q2=v2#fragment", "scheme", "login", "password", "host",
	     "service", "/path1/path2/path3", "q1=v1&q2=v2", "fragment", 0);

	test("host/path", NULL, NULL, NULL, "host", NULL, "/path", NULL,
	     NULL, 0);
	test("host//", NULL, NULL, NULL, "host", NULL, "//", NULL, NULL, 0);
	test("host//path", NULL, NULL, NULL, "host", NULL, "//path", NULL,
	     NULL, 0);
	test("host/;abc?q", NULL, NULL, NULL, "host", NULL, "/;abc", "q",
	     NULL, 0);

	test("scheme://login:password@host:service/@path1/:path2?"
	     "q1=v1&q2=v2#fragment", "scheme", "login", "password", "host",
	     "service", "/@path1/:path2", "q1=v1&q2=v2", "fragment", 0);
	test("host/~user", NULL, NULL, NULL, "host", NULL, "/~user", NULL,
	     NULL, 0);

	/* Host */
	test("try.tarantool.org", NULL, NULL, NULL, "try.tarantool.org", NULL,
	     NULL, NULL, NULL, 0);

	test("try.tarantool.org", NULL, NULL, NULL, "try.tarantool.org", NULL,
	     NULL, NULL, NULL, 0);

	test("www.llanfairpwllgwyngyllgogerychwyrndrobwyll-"
	     "llantysiliogogogoch.com", NULL, NULL, NULL,
	     "www.llanfairpwllgwyngyllgogerychwyrndrobwyll-"
	     "llantysiliogogogoch.com", NULL, NULL, NULL, NULL, 0);

	/* IPv4 / IPv6 addreses */
	test("0.0.0.0", NULL, NULL, NULL, "0.0.0.0", NULL, NULL, NULL, NULL, 1);
	test("127.0.0.1", NULL, NULL, NULL, "127.0.0.1", NULL, NULL, NULL,
	     NULL, 1);
	test("127.0.0.1:3313", NULL, NULL, NULL, "127.0.0.1", "3313", NULL,
	     NULL, NULL, 1);

	test("scheme://login:password@127.0.0.1:3313", "scheme", "login",
	     "password", "127.0.0.1", "3313", NULL, NULL, NULL, 1);

	test("[2001::11a3:09d7::1]", NULL, NULL, NULL, "2001::11a3:09d7::1",
	     NULL, NULL, NULL, NULL, 2);
	test("scheme://login:password@[2001::11a3:09d7::1]:3313", "scheme",
	     "login", "password", "2001::11a3:09d7::1", "3313", NULL, NULL,
	     NULL, 2);
	test("scheme://[2001:0db8:11a3:09d7::1]", "scheme", NULL, NULL,
	     "2001:0db8:11a3:09d7::1", NULL, NULL, NULL, NULL, 2);

	test("[::ffff:11.2.3.4]", NULL, NULL, NULL, "::ffff:11.2.3.4",
	     NULL, NULL, NULL, NULL, 2);
	test("scheme://login:password@[::ffff:11.2.3.4]:3313", "scheme",
	     "login", "password", "::ffff:11.2.3.4", "3313", NULL, NULL,
	     NULL, 2);

	/* Port */
	test("1", NULL, NULL, NULL, NULL, "1", NULL, NULL, NULL, 0);
	test("10", NULL, NULL, NULL, NULL, "10", NULL, NULL, NULL, 0);
	test("331", NULL, NULL, NULL, NULL, "331", NULL, NULL, NULL,0);
	test("3313", NULL, NULL, NULL, NULL, "3313", NULL, NULL, NULL, 0);

	/* Unix */
	test("/", NULL, NULL, NULL, "unix/", "/", NULL, NULL, NULL, 3);
	test("/path1/path2/path3", NULL, NULL, NULL, "unix/",
	     "/path1/path2/path3", NULL, NULL, NULL, 3);
	test("login:password@/path1/path2/path3", NULL, "login", "password",
	     "unix/", "/path1/path2/path3", NULL, NULL, NULL, 3);
	test("unix/:/path1/path2/path3", NULL, NULL, NULL, "unix/",
	     "/path1/path2/path3", NULL, NULL, NULL, 3);
	test("unix/:/path1/path2/path3:", NULL, NULL, NULL, "unix/",
	     "/path1/path2/path3", NULL, NULL, NULL, 3);
	test("unix/:/path1/path2/path3:/", NULL, NULL, NULL, "unix/",
	     "/path1/path2/path3", "/", NULL, NULL, 3);
	test("unix/:/path1/path2/path3?q1=v1&q2=v2#fragment", NULL, NULL, NULL,
	     "unix/", "/path1/path2/path3", NULL, "q1=v1&q2=v2", "fragment", 3);
	test("unix/:/path1/path2/path3:/p1/p2?q1=v1&q2=v2#fragment",
	     NULL, NULL, NULL, "unix/", "/path1/path2/path3", "/p1/p2",
	     "q1=v1&q2=v2", "fragment", 3);
	/* fixed grammar #2933 */
	test("login:password@unix/:/path1/path2/path3", NULL, "login",
	     "password", "unix/", "/path1/path2/path3", NULL, NULL, NULL, 3);
	test("login:password@unix/:/path1/path2/path3:", NULL, "login",
	     "password", "unix/", "/path1/path2/path3", NULL, NULL, NULL, 3);

	test("scheme://login:password@unix/:/tmp/unix.sock:/path1/path2/path3",
	     "scheme", "login", "password", "unix/", "/tmp/unix.sock",
	     "/path1/path2/path3", NULL, NULL, 3);
	test("unix/:./relative/path.sock:/test", NULL, NULL, NULL, "unix/",
	     "./relative/path.sock", "/test", NULL, NULL, 3);
	test("scheme://unix/:./relative/path.sock:/test", "scheme", NULL, NULL,
	     "unix/", "./relative/path.sock", "/test", NULL, NULL, 3);

	/* Web */
	test("http://tarantool.org/dist/master/debian/pool/main/t/tarantool/"
	     "tarantool_1.6.3+314+g91066ee+20140910+1434.orig.tar.gz",
	     "http", NULL, NULL, "tarantool.org", NULL,
	     "/dist/master/debian/pool/main/t/tarantool/"
	     "tarantool_1.6.3+314+g91066ee+20140910+1434.orig.tar.gz",
	     NULL, NULL, 0);

	test("https://www.google.com/search?"
	     "safe=off&site=&tbm=isch&source=hp&biw=1918&bih=1109&q=Tarantool"
	     "&oq=Tarantool&gs_l=img.3..0i24l3j0i10i24j0i24&gws_rd=ssl",
	     "https", NULL, NULL, "www.google.com", NULL, "/search",
	     "safe=off&site=&tbm=isch&source=hp&biw=1918&bih=1109&q=Tarantool"
	     "&oq=Tarantool&gs_l=img.3..0i24l3j0i10i24j0i24&gws_rd=ssl",
	     NULL, 0);

	test_invalid();

	return check_plan();
}
