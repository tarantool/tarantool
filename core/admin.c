
#line 1 "core/admin.rl"
/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

#include <fiber.h>
#include <palloc.h>
#include <salloc.h>
#include <say.h>
#include <stat.h>
#include <tarantool.h>
#include <util.h>

static const char help[] =
	"available commands:\r\n"
	"help\r\n"
	"exit\r\n"
	"show info\r\n"
	"show fiber\r\n"
	"show configuration\r\n"
	"show slab\r\n"
	"show palloc\r\n"
	"show stat\r\n"
	"save coredump\r\n"
	"save snapshot\r\n"
	"exec module command\r\n"
	;


static const char unknown_command[] = "unknown command. try typing help.\r\n";


#line 63 "core/admin.c"
static const int admin_start = 1;
static const int admin_first_final = 85;
static const int admin_error = 0;

static const int admin_en_main = 1;


#line 62 "core/admin.rl"


static void
ok(struct tbuf *out)
{
	tbuf_printf(out, "ok\r\n");
}

static void
end(struct tbuf *out)
{
	tbuf_printf(out, "---\r\n");
}

static int
admin_dispatch(void)
{
	struct tbuf *out = tbuf_alloc(fiber->pool);
	int cs;
	char *p, *pe;
	char *strstart, *strend;

	while ((pe = memchr(fiber->rbuf->data, '\n', fiber->rbuf->len)) == NULL) {
		if (fiber_bread(fiber->rbuf, 1) <= 0)
			return 0;
	}

	pe++;
	p = fiber->rbuf->data;

	
#line 103 "core/admin.c"
	{
	cs = admin_start;
	}

#line 108 "core/admin.c"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
case 1:
	switch( (*p) ) {
		case 99: goto st2;
		case 101: goto st13;
		case 104: goto st21;
		case 115: goto st25;
	}
	goto st0;
st0:
cs = 0;
	goto _out;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
	if ( (*p) == 104 )
		goto st3;
	goto st0;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
	switch( (*p) ) {
		case 32: goto st4;
		case 101: goto st10;
	}
	goto st0;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
	if ( (*p) == 115 )
		goto st5;
	goto st0;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	if ( (*p) == 108 )
		goto st6;
	goto st0;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
	switch( (*p) ) {
		case 10: goto tr10;
		case 13: goto tr11;
		case 97: goto st8;
	}
	goto st0;
tr10:
#line 139 "core/admin.rl"
	{slab_validate(); ok(out);}
	goto st85;
tr17:
#line 129 "core/admin.rl"
	{return 0;}
	goto st85;
tr23:
#line 126 "core/admin.rl"
	{strend = p;}
#line 138 "core/admin.rl"
	{ say_info("PRS"); mod_exec(strstart, strend - strstart, out); end(out); }
	goto st85;
tr29:
#line 128 "core/admin.rl"
	{tbuf_append(out, help, sizeof(help));}
	goto st85;
tr41:
#line 136 "core/admin.rl"
	{coredump(60); ok(out);}
	goto st85;
tr50:
#line 137 "core/admin.rl"
	{snapshot(NULL, 0); ok(out);}
	goto st85;
tr67:
#line 93 "core/admin.rl"
	{
			tarantool_cfg_iterator_t *i;
			char *key, *value;

			tbuf_printf(out, "configuration:\n");
			i = tarantool_cfg_iterator_init();
			while ((key = tarantool_cfg_iterator_next(i, &cfg, &value)) != NULL) {
				if (value) {
					tbuf_printf(out, "  %s: \"%s\"\n", key, value);
					free(value);
				} else {
					tbuf_printf(out, "  %s: (null)\n", key);
				}
			}
			end(out);
		}
	goto st85;
tr81:
#line 131 "core/admin.rl"
	{fiber_info(out);end(out);}
	goto st85;
tr87:
#line 130 "core/admin.rl"
	{mod_info(out); end(out);}
	goto st85;
tr92:
#line 134 "core/admin.rl"
	{palloc_stat(out);end(out);}
	goto st85;
tr100:
#line 133 "core/admin.rl"
	{slab_stat(out);end(out);}
	goto st85;
tr104:
#line 135 "core/admin.rl"
	{stat_print(out);end(out);}
	goto st85;
st85:
	if ( ++p == pe )
		goto _test_eof85;
case 85:
#line 234 "core/admin.c"
	goto st0;
tr11:
#line 139 "core/admin.rl"
	{slab_validate(); ok(out);}
	goto st7;
tr18:
#line 129 "core/admin.rl"
	{return 0;}
	goto st7;
tr24:
#line 126 "core/admin.rl"
	{strend = p;}
#line 138 "core/admin.rl"
	{ say_info("PRS"); mod_exec(strstart, strend - strstart, out); end(out); }
	goto st7;
tr30:
#line 128 "core/admin.rl"
	{tbuf_append(out, help, sizeof(help));}
	goto st7;
tr42:
#line 136 "core/admin.rl"
	{coredump(60); ok(out);}
	goto st7;
tr51:
#line 137 "core/admin.rl"
	{snapshot(NULL, 0); ok(out);}
	goto st7;
tr68:
#line 93 "core/admin.rl"
	{
			tarantool_cfg_iterator_t *i;
			char *key, *value;

			tbuf_printf(out, "configuration:\n");
			i = tarantool_cfg_iterator_init();
			while ((key = tarantool_cfg_iterator_next(i, &cfg, &value)) != NULL) {
				if (value) {
					tbuf_printf(out, "  %s: \"%s\"\n", key, value);
					free(value);
				} else {
					tbuf_printf(out, "  %s: (null)\n", key);
				}
			}
			end(out);
		}
	goto st7;
tr82:
#line 131 "core/admin.rl"
	{fiber_info(out);end(out);}
	goto st7;
tr88:
#line 130 "core/admin.rl"
	{mod_info(out); end(out);}
	goto st7;
tr93:
#line 134 "core/admin.rl"
	{palloc_stat(out);end(out);}
	goto st7;
tr101:
#line 133 "core/admin.rl"
	{slab_stat(out);end(out);}
	goto st7;
tr105:
#line 135 "core/admin.rl"
	{stat_print(out);end(out);}
	goto st7;
st7:
	if ( ++p == pe )
		goto _test_eof7;
case 7:
#line 305 "core/admin.c"
	if ( (*p) == 10 )
		goto st85;
	goto st0;
st8:
	if ( ++p == pe )
		goto _test_eof8;
case 8:
	switch( (*p) ) {
		case 10: goto tr10;
		case 13: goto tr11;
		case 98: goto st9;
	}
	goto st0;
st9:
	if ( ++p == pe )
		goto _test_eof9;
case 9:
	switch( (*p) ) {
		case 10: goto tr10;
		case 13: goto tr11;
	}
	goto st0;
st10:
	if ( ++p == pe )
		goto _test_eof10;
case 10:
	switch( (*p) ) {
		case 32: goto st4;
		case 99: goto st11;
	}
	goto st0;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	switch( (*p) ) {
		case 32: goto st4;
		case 107: goto st12;
	}
	goto st0;
st12:
	if ( ++p == pe )
		goto _test_eof12;
case 12:
	if ( (*p) == 32 )
		goto st4;
	goto st0;
st13:
	if ( ++p == pe )
		goto _test_eof13;
case 13:
	switch( (*p) ) {
		case 10: goto tr17;
		case 13: goto tr18;
		case 32: goto st14;
		case 120: goto st16;
	}
	goto st0;
st14:
	if ( ++p == pe )
		goto _test_eof14;
case 14:
	switch( (*p) ) {
		case 10: goto st0;
		case 13: goto st0;
	}
	goto tr21;
tr21:
#line 126 "core/admin.rl"
	{strstart = p;}
	goto st15;
st15:
	if ( ++p == pe )
		goto _test_eof15;
case 15:
#line 381 "core/admin.c"
	switch( (*p) ) {
		case 10: goto tr23;
		case 13: goto tr24;
	}
	goto st15;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
	switch( (*p) ) {
		case 10: goto tr17;
		case 13: goto tr18;
		case 32: goto st14;
		case 101: goto st17;
		case 105: goto st19;
	}
	goto st0;
st17:
	if ( ++p == pe )
		goto _test_eof17;
case 17:
	switch( (*p) ) {
		case 32: goto st14;
		case 99: goto st18;
	}
	goto st0;
st18:
	if ( ++p == pe )
		goto _test_eof18;
case 18:
	if ( (*p) == 32 )
		goto st14;
	goto st0;
st19:
	if ( ++p == pe )
		goto _test_eof19;
case 19:
	switch( (*p) ) {
		case 10: goto tr17;
		case 13: goto tr18;
		case 116: goto st20;
	}
	goto st0;
st20:
	if ( ++p == pe )
		goto _test_eof20;
case 20:
	switch( (*p) ) {
		case 10: goto tr17;
		case 13: goto tr18;
	}
	goto st0;
st21:
	if ( ++p == pe )
		goto _test_eof21;
case 21:
	switch( (*p) ) {
		case 10: goto tr29;
		case 13: goto tr30;
		case 101: goto st22;
	}
	goto st0;
st22:
	if ( ++p == pe )
		goto _test_eof22;
case 22:
	switch( (*p) ) {
		case 10: goto tr29;
		case 13: goto tr30;
		case 108: goto st23;
	}
	goto st0;
st23:
	if ( ++p == pe )
		goto _test_eof23;
case 23:
	switch( (*p) ) {
		case 10: goto tr29;
		case 13: goto tr30;
		case 112: goto st24;
	}
	goto st0;
st24:
	if ( ++p == pe )
		goto _test_eof24;
case 24:
	switch( (*p) ) {
		case 10: goto tr29;
		case 13: goto tr30;
	}
	goto st0;
st25:
	if ( ++p == pe )
		goto _test_eof25;
case 25:
	switch( (*p) ) {
		case 97: goto st26;
		case 104: goto st46;
	}
	goto st0;
st26:
	if ( ++p == pe )
		goto _test_eof26;
case 26:
	switch( (*p) ) {
		case 32: goto st27;
		case 118: goto st44;
	}
	goto st0;
st27:
	if ( ++p == pe )
		goto _test_eof27;
case 27:
	switch( (*p) ) {
		case 99: goto st28;
		case 115: goto st36;
	}
	goto st0;
st28:
	if ( ++p == pe )
		goto _test_eof28;
case 28:
	if ( (*p) == 111 )
		goto st29;
	goto st0;
st29:
	if ( ++p == pe )
		goto _test_eof29;
case 29:
	switch( (*p) ) {
		case 10: goto tr41;
		case 13: goto tr42;
		case 114: goto st30;
	}
	goto st0;
st30:
	if ( ++p == pe )
		goto _test_eof30;
case 30:
	switch( (*p) ) {
		case 10: goto tr41;
		case 13: goto tr42;
		case 101: goto st31;
	}
	goto st0;
st31:
	if ( ++p == pe )
		goto _test_eof31;
case 31:
	switch( (*p) ) {
		case 10: goto tr41;
		case 13: goto tr42;
		case 100: goto st32;
	}
	goto st0;
st32:
	if ( ++p == pe )
		goto _test_eof32;
case 32:
	switch( (*p) ) {
		case 10: goto tr41;
		case 13: goto tr42;
		case 117: goto st33;
	}
	goto st0;
st33:
	if ( ++p == pe )
		goto _test_eof33;
case 33:
	switch( (*p) ) {
		case 10: goto tr41;
		case 13: goto tr42;
		case 109: goto st34;
	}
	goto st0;
st34:
	if ( ++p == pe )
		goto _test_eof34;
case 34:
	switch( (*p) ) {
		case 10: goto tr41;
		case 13: goto tr42;
		case 112: goto st35;
	}
	goto st0;
st35:
	if ( ++p == pe )
		goto _test_eof35;
case 35:
	switch( (*p) ) {
		case 10: goto tr41;
		case 13: goto tr42;
	}
	goto st0;
st36:
	if ( ++p == pe )
		goto _test_eof36;
case 36:
	if ( (*p) == 110 )
		goto st37;
	goto st0;
st37:
	if ( ++p == pe )
		goto _test_eof37;
case 37:
	switch( (*p) ) {
		case 10: goto tr50;
		case 13: goto tr51;
		case 97: goto st38;
	}
	goto st0;
st38:
	if ( ++p == pe )
		goto _test_eof38;
case 38:
	switch( (*p) ) {
		case 10: goto tr50;
		case 13: goto tr51;
		case 112: goto st39;
	}
	goto st0;
st39:
	if ( ++p == pe )
		goto _test_eof39;
case 39:
	switch( (*p) ) {
		case 10: goto tr50;
		case 13: goto tr51;
		case 115: goto st40;
	}
	goto st0;
st40:
	if ( ++p == pe )
		goto _test_eof40;
case 40:
	switch( (*p) ) {
		case 10: goto tr50;
		case 13: goto tr51;
		case 104: goto st41;
	}
	goto st0;
st41:
	if ( ++p == pe )
		goto _test_eof41;
case 41:
	switch( (*p) ) {
		case 10: goto tr50;
		case 13: goto tr51;
		case 111: goto st42;
	}
	goto st0;
st42:
	if ( ++p == pe )
		goto _test_eof42;
case 42:
	switch( (*p) ) {
		case 10: goto tr50;
		case 13: goto tr51;
		case 116: goto st43;
	}
	goto st0;
st43:
	if ( ++p == pe )
		goto _test_eof43;
case 43:
	switch( (*p) ) {
		case 10: goto tr50;
		case 13: goto tr51;
	}
	goto st0;
st44:
	if ( ++p == pe )
		goto _test_eof44;
case 44:
	switch( (*p) ) {
		case 32: goto st27;
		case 101: goto st45;
	}
	goto st0;
st45:
	if ( ++p == pe )
		goto _test_eof45;
case 45:
	if ( (*p) == 32 )
		goto st27;
	goto st0;
st46:
	if ( ++p == pe )
		goto _test_eof46;
case 46:
	switch( (*p) ) {
		case 32: goto st47;
		case 111: goto st83;
	}
	goto st0;
st47:
	if ( ++p == pe )
		goto _test_eof47;
case 47:
	switch( (*p) ) {
		case 99: goto st48;
		case 102: goto st61;
		case 105: goto st66;
		case 112: goto st70;
		case 115: goto st76;
	}
	goto st0;
st48:
	if ( ++p == pe )
		goto _test_eof48;
case 48:
	if ( (*p) == 111 )
		goto st49;
	goto st0;
st49:
	if ( ++p == pe )
		goto _test_eof49;
case 49:
	switch( (*p) ) {
		case 10: goto tr67;
		case 13: goto tr68;
		case 110: goto st50;
	}
	goto st0;
st50:
	if ( ++p == pe )
		goto _test_eof50;
case 50:
	switch( (*p) ) {
		case 10: goto tr67;
		case 13: goto tr68;
		case 102: goto st51;
	}
	goto st0;
st51:
	if ( ++p == pe )
		goto _test_eof51;
case 51:
	switch( (*p) ) {
		case 10: goto tr67;
		case 13: goto tr68;
		case 105: goto st52;
	}
	goto st0;
st52:
	if ( ++p == pe )
		goto _test_eof52;
case 52:
	switch( (*p) ) {
		case 10: goto tr67;
		case 13: goto tr68;
		case 103: goto st53;
	}
	goto st0;
st53:
	if ( ++p == pe )
		goto _test_eof53;
case 53:
	switch( (*p) ) {
		case 10: goto tr67;
		case 13: goto tr68;
		case 117: goto st54;
	}
	goto st0;
st54:
	if ( ++p == pe )
		goto _test_eof54;
case 54:
	switch( (*p) ) {
		case 10: goto tr67;
		case 13: goto tr68;
		case 114: goto st55;
	}
	goto st0;
st55:
	if ( ++p == pe )
		goto _test_eof55;
case 55:
	switch( (*p) ) {
		case 10: goto tr67;
		case 13: goto tr68;
		case 97: goto st56;
	}
	goto st0;
st56:
	if ( ++p == pe )
		goto _test_eof56;
case 56:
	switch( (*p) ) {
		case 10: goto tr67;
		case 13: goto tr68;
		case 116: goto st57;
	}
	goto st0;
st57:
	if ( ++p == pe )
		goto _test_eof57;
case 57:
	switch( (*p) ) {
		case 10: goto tr67;
		case 13: goto tr68;
		case 105: goto st58;
	}
	goto st0;
st58:
	if ( ++p == pe )
		goto _test_eof58;
case 58:
	switch( (*p) ) {
		case 10: goto tr67;
		case 13: goto tr68;
		case 111: goto st59;
	}
	goto st0;
st59:
	if ( ++p == pe )
		goto _test_eof59;
case 59:
	switch( (*p) ) {
		case 10: goto tr67;
		case 13: goto tr68;
		case 110: goto st60;
	}
	goto st0;
st60:
	if ( ++p == pe )
		goto _test_eof60;
case 60:
	switch( (*p) ) {
		case 10: goto tr67;
		case 13: goto tr68;
	}
	goto st0;
st61:
	if ( ++p == pe )
		goto _test_eof61;
case 61:
	if ( (*p) == 105 )
		goto st62;
	goto st0;
st62:
	if ( ++p == pe )
		goto _test_eof62;
case 62:
	switch( (*p) ) {
		case 10: goto tr81;
		case 13: goto tr82;
		case 98: goto st63;
	}
	goto st0;
st63:
	if ( ++p == pe )
		goto _test_eof63;
case 63:
	switch( (*p) ) {
		case 10: goto tr81;
		case 13: goto tr82;
		case 101: goto st64;
	}
	goto st0;
st64:
	if ( ++p == pe )
		goto _test_eof64;
case 64:
	switch( (*p) ) {
		case 10: goto tr81;
		case 13: goto tr82;
		case 114: goto st65;
	}
	goto st0;
st65:
	if ( ++p == pe )
		goto _test_eof65;
case 65:
	switch( (*p) ) {
		case 10: goto tr81;
		case 13: goto tr82;
	}
	goto st0;
st66:
	if ( ++p == pe )
		goto _test_eof66;
case 66:
	if ( (*p) == 110 )
		goto st67;
	goto st0;
st67:
	if ( ++p == pe )
		goto _test_eof67;
case 67:
	switch( (*p) ) {
		case 10: goto tr87;
		case 13: goto tr88;
		case 102: goto st68;
	}
	goto st0;
st68:
	if ( ++p == pe )
		goto _test_eof68;
case 68:
	switch( (*p) ) {
		case 10: goto tr87;
		case 13: goto tr88;
		case 111: goto st69;
	}
	goto st0;
st69:
	if ( ++p == pe )
		goto _test_eof69;
case 69:
	switch( (*p) ) {
		case 10: goto tr87;
		case 13: goto tr88;
	}
	goto st0;
st70:
	if ( ++p == pe )
		goto _test_eof70;
case 70:
	if ( (*p) == 97 )
		goto st71;
	goto st0;
st71:
	if ( ++p == pe )
		goto _test_eof71;
case 71:
	switch( (*p) ) {
		case 10: goto tr92;
		case 13: goto tr93;
		case 108: goto st72;
	}
	goto st0;
st72:
	if ( ++p == pe )
		goto _test_eof72;
case 72:
	switch( (*p) ) {
		case 10: goto tr92;
		case 13: goto tr93;
		case 108: goto st73;
	}
	goto st0;
st73:
	if ( ++p == pe )
		goto _test_eof73;
case 73:
	switch( (*p) ) {
		case 10: goto tr92;
		case 13: goto tr93;
		case 111: goto st74;
	}
	goto st0;
st74:
	if ( ++p == pe )
		goto _test_eof74;
case 74:
	switch( (*p) ) {
		case 10: goto tr92;
		case 13: goto tr93;
		case 99: goto st75;
	}
	goto st0;
st75:
	if ( ++p == pe )
		goto _test_eof75;
case 75:
	switch( (*p) ) {
		case 10: goto tr92;
		case 13: goto tr93;
	}
	goto st0;
st76:
	if ( ++p == pe )
		goto _test_eof76;
case 76:
	switch( (*p) ) {
		case 108: goto st77;
		case 116: goto st80;
	}
	goto st0;
st77:
	if ( ++p == pe )
		goto _test_eof77;
case 77:
	switch( (*p) ) {
		case 10: goto tr100;
		case 13: goto tr101;
		case 97: goto st78;
	}
	goto st0;
st78:
	if ( ++p == pe )
		goto _test_eof78;
case 78:
	switch( (*p) ) {
		case 10: goto tr100;
		case 13: goto tr101;
		case 98: goto st79;
	}
	goto st0;
st79:
	if ( ++p == pe )
		goto _test_eof79;
case 79:
	switch( (*p) ) {
		case 10: goto tr100;
		case 13: goto tr101;
	}
	goto st0;
st80:
	if ( ++p == pe )
		goto _test_eof80;
case 80:
	switch( (*p) ) {
		case 10: goto tr104;
		case 13: goto tr105;
		case 97: goto st81;
	}
	goto st0;
st81:
	if ( ++p == pe )
		goto _test_eof81;
case 81:
	switch( (*p) ) {
		case 10: goto tr104;
		case 13: goto tr105;
		case 116: goto st82;
	}
	goto st0;
st82:
	if ( ++p == pe )
		goto _test_eof82;
case 82:
	switch( (*p) ) {
		case 10: goto tr104;
		case 13: goto tr105;
	}
	goto st0;
st83:
	if ( ++p == pe )
		goto _test_eof83;
case 83:
	switch( (*p) ) {
		case 32: goto st47;
		case 119: goto st84;
	}
	goto st0;
st84:
	if ( ++p == pe )
		goto _test_eof84;
case 84:
	if ( (*p) == 32 )
		goto st47;
	goto st0;
	}
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 
	_test_eof4: cs = 4; goto _test_eof; 
	_test_eof5: cs = 5; goto _test_eof; 
	_test_eof6: cs = 6; goto _test_eof; 
	_test_eof85: cs = 85; goto _test_eof; 
	_test_eof7: cs = 7; goto _test_eof; 
	_test_eof8: cs = 8; goto _test_eof; 
	_test_eof9: cs = 9; goto _test_eof; 
	_test_eof10: cs = 10; goto _test_eof; 
	_test_eof11: cs = 11; goto _test_eof; 
	_test_eof12: cs = 12; goto _test_eof; 
	_test_eof13: cs = 13; goto _test_eof; 
	_test_eof14: cs = 14; goto _test_eof; 
	_test_eof15: cs = 15; goto _test_eof; 
	_test_eof16: cs = 16; goto _test_eof; 
	_test_eof17: cs = 17; goto _test_eof; 
	_test_eof18: cs = 18; goto _test_eof; 
	_test_eof19: cs = 19; goto _test_eof; 
	_test_eof20: cs = 20; goto _test_eof; 
	_test_eof21: cs = 21; goto _test_eof; 
	_test_eof22: cs = 22; goto _test_eof; 
	_test_eof23: cs = 23; goto _test_eof; 
	_test_eof24: cs = 24; goto _test_eof; 
	_test_eof25: cs = 25; goto _test_eof; 
	_test_eof26: cs = 26; goto _test_eof; 
	_test_eof27: cs = 27; goto _test_eof; 
	_test_eof28: cs = 28; goto _test_eof; 
	_test_eof29: cs = 29; goto _test_eof; 
	_test_eof30: cs = 30; goto _test_eof; 
	_test_eof31: cs = 31; goto _test_eof; 
	_test_eof32: cs = 32; goto _test_eof; 
	_test_eof33: cs = 33; goto _test_eof; 
	_test_eof34: cs = 34; goto _test_eof; 
	_test_eof35: cs = 35; goto _test_eof; 
	_test_eof36: cs = 36; goto _test_eof; 
	_test_eof37: cs = 37; goto _test_eof; 
	_test_eof38: cs = 38; goto _test_eof; 
	_test_eof39: cs = 39; goto _test_eof; 
	_test_eof40: cs = 40; goto _test_eof; 
	_test_eof41: cs = 41; goto _test_eof; 
	_test_eof42: cs = 42; goto _test_eof; 
	_test_eof43: cs = 43; goto _test_eof; 
	_test_eof44: cs = 44; goto _test_eof; 
	_test_eof45: cs = 45; goto _test_eof; 
	_test_eof46: cs = 46; goto _test_eof; 
	_test_eof47: cs = 47; goto _test_eof; 
	_test_eof48: cs = 48; goto _test_eof; 
	_test_eof49: cs = 49; goto _test_eof; 
	_test_eof50: cs = 50; goto _test_eof; 
	_test_eof51: cs = 51; goto _test_eof; 
	_test_eof52: cs = 52; goto _test_eof; 
	_test_eof53: cs = 53; goto _test_eof; 
	_test_eof54: cs = 54; goto _test_eof; 
	_test_eof55: cs = 55; goto _test_eof; 
	_test_eof56: cs = 56; goto _test_eof; 
	_test_eof57: cs = 57; goto _test_eof; 
	_test_eof58: cs = 58; goto _test_eof; 
	_test_eof59: cs = 59; goto _test_eof; 
	_test_eof60: cs = 60; goto _test_eof; 
	_test_eof61: cs = 61; goto _test_eof; 
	_test_eof62: cs = 62; goto _test_eof; 
	_test_eof63: cs = 63; goto _test_eof; 
	_test_eof64: cs = 64; goto _test_eof; 
	_test_eof65: cs = 65; goto _test_eof; 
	_test_eof66: cs = 66; goto _test_eof; 
	_test_eof67: cs = 67; goto _test_eof; 
	_test_eof68: cs = 68; goto _test_eof; 
	_test_eof69: cs = 69; goto _test_eof; 
	_test_eof70: cs = 70; goto _test_eof; 
	_test_eof71: cs = 71; goto _test_eof; 
	_test_eof72: cs = 72; goto _test_eof; 
	_test_eof73: cs = 73; goto _test_eof; 
	_test_eof74: cs = 74; goto _test_eof; 
	_test_eof75: cs = 75; goto _test_eof; 
	_test_eof76: cs = 76; goto _test_eof; 
	_test_eof77: cs = 77; goto _test_eof; 
	_test_eof78: cs = 78; goto _test_eof; 
	_test_eof79: cs = 79; goto _test_eof; 
	_test_eof80: cs = 80; goto _test_eof; 
	_test_eof81: cs = 81; goto _test_eof; 
	_test_eof82: cs = 82; goto _test_eof; 
	_test_eof83: cs = 83; goto _test_eof; 
	_test_eof84: cs = 84; goto _test_eof; 

	_test_eof: {}
	_out: {}
	}

#line 144 "core/admin.rl"


	fiber->rbuf->len -= (void *)pe - (void *)fiber->rbuf->data;
	fiber->rbuf->data = pe;

	if (p != pe)
		tbuf_append(out, unknown_command, sizeof(unknown_command));

	return fiber_write(out->data, out->len);
}


static void
admin_handler(void *_data __unused__)
{
	for (;;) {
		if (admin_dispatch() <= 0)
			return;
		fiber_gc();
	}
}

int
admin_init(void)
{
	if (fiber_server(tcp_server, cfg.admin_port, admin_handler, NULL, NULL) == NULL) {
		say_syserror("can't bind to %d", cfg.admin_port);
		return -1;
	}
	return 0;
}



/*
 * Local Variables:
 * mode: c
 * End:
 */
