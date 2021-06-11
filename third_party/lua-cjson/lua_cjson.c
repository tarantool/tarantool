/* Lua CJSON - JSON support for Lua
 *
 * Copyright (c) 2010-2012  Mark Pulford <mark@kyne.com.au>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* Caveats:
 * - JSON "null" values are represented as lightuserdata since Lua
 *   tables cannot contain "nil". Compare with cjson.null.
 * - Invalid UTF-8 characters are not detected and will be passed
 *   untouched. If required, UTF-8 error checking should be done
 *   outside this library.
 * - Javascript comments are not part of the JSON spec, and are not
 *   currently supported.
 *
 * Note: Decoding is slower than encoding. Lua spends significant
 *       time (30%) managing tables when parsing JSON since it is
 *       difficult to know object/array sizes ahead of time.
 */

#include "trivia/util.h"

#include <assert.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <lua.h>
#include <lauxlib.h>

#include "strbuf.h"

#include "lua/utils.h"
#include "lua/serializer.h"
#include "mp_extension_types.h" /* MP_DECIMAL, MP_UUID */
#include "tt_static.h"
#include "uuid/tt_uuid.h" /* tt_uuid_to_string(), UUID_STR_LEN */
#include "cord_buf.h"

typedef enum {
    T_OBJ_BEGIN,
    T_OBJ_END,
    T_ARR_BEGIN,
    T_ARR_END,
    T_STRING,
    T_UINT,
    T_INT,
    T_NUMBER,
    T_BOOLEAN,
    T_NULL,
    T_COLON,
    T_COMMA,
    T_END,
    T_WHITESPACE,
    T_LINEFEED,
    T_ERROR,
    T_UNKNOWN
} json_token_type_t;

static const char *json_token_type_name[] = {
    "'{'",
    "'}'",
    "'['",
    "']'",
    "string",
    "unsigned int",
    "int",
    "number",
    "boolean",
    "null",
    "colon",
    "comma",
    "end",
    "whitespace",
    "line feed",
    "error",
    "unknown symbol",
    NULL
};

static struct luaL_serializer *luaL_json_default;

static json_token_type_t ch2token[256];
static char escape2char[256];  /* Decoding */

typedef struct {
    const char *data;
    const char *ptr;
    strbuf_t *tmp;    /* Temporary storage for strings */
    struct luaL_serializer *cfg;
    int current_depth;
    int line_count;
    const char *cur_line_ptr;
} json_parse_t;

typedef struct {
    json_token_type_t type;
    int column_index;
    union {
        const char *string;
        double number;
        int boolean;
    long long ival;
    } value;
    int string_len;
} json_token_t;

static const char *char2escape[256] = {
    "\\u0000", "\\u0001", "\\u0002", "\\u0003",
    "\\u0004", "\\u0005", "\\u0006", "\\u0007",
    "\\b", "\\t", "\\n", "\\u000b",
    "\\f", "\\r", "\\u000e", "\\u000f",
    "\\u0010", "\\u0011", "\\u0012", "\\u0013",
    "\\u0014", "\\u0015", "\\u0016", "\\u0017",
    "\\u0018", "\\u0019", "\\u001a", "\\u001b",
    "\\u001c", "\\u001d", "\\u001e", "\\u001f",
    NULL, NULL, "\\\"", NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, "\\/",
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, "\\\\", NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, "\\u007f",
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

#if 0
static int json_destroy_config(lua_State *l)
{
    struct luaL_serializer *cfg;

    cfg = lua_touserdata(l, 1);
    if (cfg)
    strbuf_free(&encode_buf);
    cfg = NULL;

    return 0;
}
#endif

static void json_create_tokens()
{
    int i;

    /* Decoding init */

    /* Tag all characters as an error */
    for (i = 0; i < 256; i++)
    ch2token[i] = T_ERROR;

    /* Set tokens that require no further processing */
    ch2token['{'] = T_OBJ_BEGIN;
    ch2token['}'] = T_OBJ_END;
    ch2token['['] = T_ARR_BEGIN;
    ch2token[']'] = T_ARR_END;
    ch2token[','] = T_COMMA;
    ch2token[':'] = T_COLON;
    ch2token['\0'] = T_END;
    ch2token[' '] = T_WHITESPACE;
    ch2token['\t'] = T_WHITESPACE;
    ch2token['\n'] = T_LINEFEED;
    ch2token['\r'] = T_WHITESPACE;

    /* Update characters that require further processing */
    ch2token['f'] = T_UNKNOWN;     /* false? */
    ch2token['i'] = T_UNKNOWN;     /* inf, ininity? */
    ch2token['I'] = T_UNKNOWN;
    ch2token['n'] = T_UNKNOWN;     /* null, nan? */
    ch2token['N'] = T_UNKNOWN;
    ch2token['t'] = T_UNKNOWN;     /* true? */
    ch2token['"'] = T_UNKNOWN;     /* string? */
    ch2token['+'] = T_UNKNOWN;     /* number? */
    ch2token['-'] = T_UNKNOWN;
    for (i = 0; i < 10; i++)
    ch2token['0' + i] = T_UNKNOWN;

    /* Lookup table for parsing escape characters */
    for (i = 0; i < 256; i++)
    escape2char[i] = 0;          /* String error */
    escape2char['"'] = '"';
    escape2char['\\'] = '\\';
    escape2char['/'] = '/';
    escape2char['b'] = '\b';
    escape2char['t'] = '\t';
    escape2char['n'] = '\n';
    escape2char['f'] = '\f';
    escape2char['r'] = '\r';
    escape2char['u'] = 'u';          /* Unicode parsing required */
}

/* ===== ENCODING ===== */

/* json_append_string args:
 * - lua_State
 * - JSON strbuf
 * - String (Lua stack index)
 *
 * Returns nothing. Doesn't remove string from Lua stack */
static void json_append_string(struct luaL_serializer *cfg, strbuf_t *json,
                   const char *str, size_t len)
{
    (void) cfg;
    const char *escstr;
    size_t i;

    /* Worst case is len * 6 (all unicode escapes).
     * This buffer is reused constantly for small strings
     * If there are any excess pages, they won't be hit anyway.
     * This gains ~5% speedup. */
    strbuf_ensure_empty_length(json, len * 6 + 2);

    strbuf_append_char_unsafe(json, '\"');
    for (i = 0; i < len; i++) {
        escstr = char2escape[(unsigned char)str[i]];
        if (escstr)
            strbuf_append_string(json, escstr);
        else
            strbuf_append_char_unsafe(json, str[i]);
    }
    strbuf_append_char_unsafe(json, '\"');
}

static void json_append_data(lua_State *l, struct luaL_serializer *cfg,
                             int current_depth, strbuf_t *json);

/* json_append_array args:
 * - lua_State
 * - JSON strbuf
 * - Size of passwd Lua array (top of stack) */
static void json_append_array(lua_State *l, struct luaL_serializer *cfg,
                  int current_depth, strbuf_t *json,
                  int array_length)
{
    int comma, i;

    strbuf_append_char(json, '[');

    comma = 0;
    for (i = 1; i <= array_length; i++) {
        if (comma)
            strbuf_append_char(json, ',');
        else
            comma = 1;

        lua_rawgeti(l, -1, i);
        json_append_data(l, cfg, current_depth, json);
        lua_pop(l, 1);
    }

    strbuf_append_char(json, ']');
}

static void json_append_uint(struct luaL_serializer *cfg, strbuf_t *json,
                 uint64_t num)
{
    (void) cfg;
    enum { INT_BUFSIZE = 22 };
    strbuf_ensure_empty_length(json, INT_BUFSIZE);
    int len = snprintf(strbuf_empty_ptr(json), INT_BUFSIZE, "%llu",
               (unsigned long long) num);
    strbuf_extend_length(json, len);
}

static void json_append_int(struct luaL_serializer *cfg, strbuf_t *json,
               int64_t num)
{
    (void) cfg;
    enum {INT_BUFSIZE = 22 };
    strbuf_ensure_empty_length(json, INT_BUFSIZE);
    int len = snprintf(strbuf_empty_ptr(json), INT_BUFSIZE, "%lld",
               (long long) num);
    strbuf_extend_length(json, len);
}

static void json_append_nil(struct luaL_serializer *cfg, strbuf_t *json)
{
    (void) cfg;
    strbuf_append_mem(json, "null", 4);
}

static void json_append_number(struct luaL_serializer *cfg, strbuf_t *json,
                   lua_Number num)
{
    if (isnan(num)) {
    strbuf_append_mem(json, "nan", 3);
    return;
    }

    int len;
    strbuf_ensure_empty_length(json, FPCONV_G_FMT_BUFSIZE);
    len = fpconv_g_fmt(strbuf_empty_ptr(json), num, cfg->encode_number_precision);
    strbuf_extend_length(json, len);
}

static void json_append_object(lua_State *l, struct luaL_serializer *cfg,
                               int current_depth, strbuf_t *json)
{
    int comma;

    /* Object */
    strbuf_append_char(json, '{');

    lua_pushnil(l);
    /* table, startkey */
    comma = 0;
    while (lua_next(l, -2) != 0) {
        if (comma)
            strbuf_append_char(json, ',');
        else
            comma = 1;

    struct luaL_field field;
    luaL_checkfield(l, cfg, -2, &field);
    if (field.type == MP_UINT) {
        strbuf_append_char(json, '"');
        json_append_uint(cfg, json, field.ival);
        strbuf_append_mem(json, "\":", 2);
    } else if (field.type == MP_INT) {
        strbuf_append_char(json, '"');
        json_append_int(cfg, json, field.ival);
        strbuf_append_mem(json, "\":", 2);
    } else if (field.type == MP_STR) {
        json_append_string(cfg, json, field.sval.data, field.sval.len);
        strbuf_append_char(json, ':');
    } else {
        luaL_error(l, "table key must be a number or string");
    }

        /* table, key, value */
        json_append_data(l, cfg, current_depth, json);
        lua_pop(l, 1);
        /* table, key */
    }

    strbuf_append_char(json, '}');
}

/* Serialise Lua data into JSON string. */
static void json_append_data(lua_State *l, struct luaL_serializer *cfg,
                             int current_depth, strbuf_t *json)
{
    struct luaL_field field;
    luaL_checkfield(l, cfg, -1, &field);
    switch (field.type) {
    case MP_UINT:
        return json_append_uint(cfg, json, field.ival);
    case MP_STR:
    case MP_BIN:
        return json_append_string(cfg, json, field.sval.data, field.sval.len);
    case MP_INT:
        return json_append_int(cfg, json, field.ival);
    case MP_FLOAT:
        return json_append_number(cfg, json, field.fval);
    case MP_DOUBLE:
        return json_append_number(cfg, json, field.dval);
    case MP_BOOL:
    if (field.bval)
        strbuf_append_mem(json, "true", 4);
    else
        strbuf_append_mem(json, "false", 5);
    break;
    case MP_NIL:
    json_append_nil(cfg, json);
    break;
    case MP_MAP:
    if (current_depth >= cfg->encode_max_depth) {
        if (! cfg->encode_deep_as_nil)
            luaL_error(l, "Too high nest level");
        return json_append_nil(cfg, json); /* Limit nested maps */
    }
    json_append_object(l, cfg, current_depth + 1, json);
    return;
    case MP_ARRAY:
    /* Array */
    if (current_depth >= cfg->encode_max_depth) {
        if (! cfg->encode_deep_as_nil)
            luaL_error(l, "Too high nest level");
        return json_append_nil(cfg, json); /* Limit nested arrays */
    }
    json_append_array(l, cfg, current_depth + 1, json, field.size);
    return;
    case MP_EXT:
        switch (field.ext_type) {
        case MP_DECIMAL:
        {
            const char *str = decimal_to_string(field.decval);
            return json_append_string(cfg, json, str, strlen(str));
        }
        case MP_UUID:
            return json_append_string(cfg, json, tt_uuid_str(field.uuidval),
                                      UUID_STR_LEN);
        default:
            assert(false);
        }
    }
}

static int json_encode(lua_State *l) {
    luaL_argcheck(l, lua_gettop(l) == 2 || lua_gettop(l) == 1, 1,
                  "expected 1 or 2 arguments");

    /* Reuse existing buffer. */
    strbuf_t encode_buf;
    struct ibuf *ibuf = cord_ibuf_take();
    strbuf_create(&encode_buf, STRBUF_DEFAULT_SIZE, ibuf);
    struct luaL_serializer *cfg = luaL_checkserializer(l);

    if (lua_gettop(l) == 2) {
        struct luaL_serializer user_cfg = *cfg;
        luaL_serializer_parse_options(l, &user_cfg);
        lua_pop(l, 1);
        json_append_data(l, &user_cfg, 0, &encode_buf);
    } else {
        json_append_data(l, cfg, 0, &encode_buf);
    }

    char *json = strbuf_string(&encode_buf, NULL);
    lua_pushlstring(l, json, strbuf_length(&encode_buf));
    /*
     * Even if an exception is raised above, it is fine to skip the buffer
     * destruction. The strbuf object destructor does not free anything, and
     * the cord_ibuf object is freed automatically on a next yield.
     */
    strbuf_destroy(&encode_buf);
    cord_ibuf_put(ibuf);
    return 1;
}

/* ===== DECODING ===== */

static void json_process_value(lua_State *l, json_parse_t *json,
                               json_token_t *token);

static int hexdigit2int(char hex)
{
    if ('0' <= hex  && hex <= '9')
        return hex - '0';

    /* Force lowercase */
    hex |= 0x20;
    if ('a' <= hex && hex <= 'f')
        return 10 + hex - 'a';

    return -1;
}

static int decode_hex4(const char *hex)
{
    int digit[4];
    int i;

    /* Convert ASCII hex digit to numeric digit
     * Note: this returns an error for invalid hex digits, including
     *       NULL */
    for (i = 0; i < 4; i++) {
        digit[i] = hexdigit2int(hex[i]);
        if (digit[i] < 0) {
            return -1;
        }
    }

    return (digit[0] << 12) +
           (digit[1] << 8) +
           (digit[2] << 4) +
            digit[3];
}

/* Converts a Unicode codepoint to UTF-8.
 * Returns UTF-8 string length, and up to 4 bytes in *utf8 */
static int codepoint_to_utf8(char *utf8, int codepoint)
{
    /* 0xxxxxxx */
    if (codepoint <= 0x7F) {
        utf8[0] = codepoint;
        return 1;
    }

    /* 110xxxxx 10xxxxxx */
    if (codepoint <= 0x7FF) {
        utf8[0] = (codepoint >> 6) | 0xC0;
        utf8[1] = (codepoint & 0x3F) | 0x80;
        return 2;
    }

    /* 1110xxxx 10xxxxxx 10xxxxxx */
    if (codepoint <= 0xFFFF) {
        utf8[0] = (codepoint >> 12) | 0xE0;
        utf8[1] = ((codepoint >> 6) & 0x3F) | 0x80;
        utf8[2] = (codepoint & 0x3F) | 0x80;
        return 3;
    }

    /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if (codepoint <= 0x1FFFFF) {
        utf8[0] = (codepoint >> 18) | 0xF0;
        utf8[1] = ((codepoint >> 12) & 0x3F) | 0x80;
        utf8[2] = ((codepoint >> 6) & 0x3F) | 0x80;
        utf8[3] = (codepoint & 0x3F) | 0x80;
        return 4;
    }

    return 0;
}


/* Called when index pointing to beginning of UTF-16 code escape: \uXXXX
 * \u is guaranteed to exist, but the remaining hex characters may be
 * missing.
 * Translate to UTF-8 and append to temporary token string.
 * Must advance index to the next character to be processed.
 * Returns: 0   success
 *          -1  error
 */
static int json_append_unicode_escape(json_parse_t *json)
{
    char utf8[4];       /* Surrogate pairs require 4 UTF-8 bytes */
    int codepoint;
    int surrogate_low;
    int len;
    int escape_len = 6;

    /* Fetch UTF-16 code unit */
    codepoint = decode_hex4(json->ptr + 2);
    if (codepoint < 0)
        return -1;

    /* UTF-16 surrogate pairs take the following 2 byte form:
     *      11011 x yyyyyyyyyy
     * When x = 0: y is the high 10 bits of the codepoint
     *      x = 1: y is the low 10 bits of the codepoint
     *
     * Check for a surrogate pair (high or low) */
    if ((codepoint & 0xF800) == 0xD800) {
        /* Error if the 1st surrogate is not high */
        if (codepoint & 0x400)
            return -1;

        /* Ensure the next code is a unicode escape */
        if (*(json->ptr + escape_len) != '\\' ||
            *(json->ptr + escape_len + 1) != 'u') {
            return -1;
        }

        /* Fetch the next codepoint */
        surrogate_low = decode_hex4(json->ptr + 2 + escape_len);
        if (surrogate_low < 0)
            return -1;

        /* Error if the 2nd code is not a low surrogate */
        if ((surrogate_low & 0xFC00) != 0xDC00)
            return -1;

        /* Calculate Unicode codepoint */
        codepoint = (codepoint & 0x3FF) << 10;
        surrogate_low &= 0x3FF;
        codepoint = (codepoint | surrogate_low) + 0x10000;
        escape_len = 12;
    }

    /* Convert codepoint to UTF-8 */
    len = codepoint_to_utf8(utf8, codepoint);
    if (!len)
        return -1;

    /* Append bytes and advance parse index */
    strbuf_append_mem_unsafe(json->tmp, utf8, len);
    json->ptr += escape_len;

    return 0;
}

static void json_set_token_error(json_token_t *token, json_parse_t *json,
                                 const char *errtype)
{
    token->type = T_ERROR;
    token->column_index = json->ptr - json->cur_line_ptr;
    token->value.string = errtype;
}

static void json_next_string_token(json_parse_t *json, json_token_t *token)
{
    char ch;

    /* Caller must ensure a string is next */
    assert(*json->ptr == '"');

    /* Skip " */
    json->ptr++;

    /* json->tmp is the temporary strbuf used to accumulate the
     * decoded string value.
     * json->tmp is sized to handle JSON containing only a string value.
     */
    strbuf_reset(json->tmp);

    while ((ch = *json->ptr) != '"') {
        if (!ch) {
            /* Premature end of the string */
            json_set_token_error(token, json, "unexpected end of string");
            return;
        }

        /* Handle escapes */
        if (ch == '\\') {
            /* Fetch escape character */
            ch = *(json->ptr + 1);

            /* Translate escape code and append to tmp string */
            ch = escape2char[(unsigned char)ch];
            if (ch == 'u') {
                if (json_append_unicode_escape(json) == 0)
                    continue;

                json_set_token_error(token, json,
                                     "invalid unicode escape code");
                return;
            }
            if (!ch) {
                json_set_token_error(token, json, "invalid escape code");
                return;
            }

            /* Skip '\' */
            json->ptr++;
        }
        /* Append normal character or translated single character
         * Unicode escapes are handled above */
        strbuf_append_char_unsafe(json->tmp, ch);
        json->ptr++;
    }
    json->ptr++;    /* Eat final quote (") */

    strbuf_ensure_null(json->tmp);

    token->type = T_STRING;
    token->value.string = strbuf_string(json->tmp, &token->string_len);
}

/* JSON numbers should take the following form:
 *      -?(0|[1-9]|[1-9][0-9]+)(.[0-9]+)?([eE][-+]?[0-9]+)?
 *
 * json_next_number_token() uses strtod() which allows other forms:
 * - numbers starting with '+'
 * - NaN, -NaN, infinity, -infinity
 * - hexadecimal numbers
 * - numbers with leading zeros
 *
 * json_is_invalid_number() detects "numbers" which may pass strtod()'s
 * error checking, but should not be allowed with strict JSON.
 *
 * json_is_invalid_number() may pass numbers which cause strtod()
 * to generate an error.
 */
static int json_is_invalid_number(json_parse_t *json)
{
    const char *p = json->ptr;

    /* Reject numbers starting with + */
    if (*p == '+')
        return 1;

    /* Skip minus sign if it exists */
    if (*p == '-')
        p++;

    /* Reject numbers starting with 0x, or leading zeros */
    if (*p == '0') {
        int ch2 = *(p + 1);

        if ((ch2 | 0x20) == 'x' ||          /* Hex */
            ('0' <= ch2 && ch2 <= '9'))     /* Leading zero */
            return 1;

        return 0;
    } else if (*p < '0' || *p > '9') {
        return 1;
    }

    return 0;
}

static void json_next_number_token(json_parse_t *json, json_token_t *token)
{
    char *endptr;


    token->type = T_INT;
    token->value.ival = strtoll(json->ptr, &endptr, 10);
    if (token->value.ival == LLONG_MAX) {
        token->type = T_UINT;
        token->value.ival = strtoull(json->ptr, &endptr, 10);
    }
    if (*endptr == '.' || *endptr == 'e' || *endptr == 'E') {
        token->type = T_NUMBER;
        token->value.number = fpconv_strtod(json->ptr, &endptr);
    }

    if (json->ptr == endptr)
        json_set_token_error(token, json, "invalid number");
    else
        json->ptr = endptr;     /* Skip the processed number */
}

/* Fills in the token struct.
 * T_STRING will return a pointer to the json_parse_t temporary string
 * T_ERROR will leave the json->ptr pointer at the error.
 */
static void json_next_token(json_parse_t *json, json_token_t *token)
{
    int ch;

    /* Eat whitespace. */
    while (1) {
        ch = (unsigned char)*(json->ptr);
        token->type = ch2token[ch];
        if (token->type == T_LINEFEED) {
            json->line_count++;
            json->cur_line_ptr = json->ptr + 1;
        } else if (token->type != T_WHITESPACE) {
            break;
        }
        json->ptr++;
    }

    /* Store location of new token. Required when throwing errors
     * for unexpected tokens (syntax errors). */
    token->column_index = json->ptr - json->cur_line_ptr;

    /* Don't advance the pointer for an error or the end */
    if (token->type == T_ERROR) {
        json_set_token_error(token, json, "invalid token");
        return;
    }

    if (token->type == T_END) {
        return;
    }

    /* Found a known single character token, advance index and return */
    if (token->type != T_UNKNOWN) {
        json->ptr++;
        return;
    }

    /* Process characters which triggered T_UNKNOWN
     *
     * Must use strncmp() to match the front of the JSON string.
     * JSON identifier must be lowercase.
     * When strict_numbers if disabled, either case is allowed for
     * Infinity/NaN (since we are no longer following the spec..) */
    if (ch == '"') {
        json_next_string_token(json, token);
        return;
    } else if (!json_is_invalid_number(json)) {
        json_next_number_token(json, token);
        return;
    } else if (!strncmp(json->ptr, "true", 4)) {
        token->type = T_BOOLEAN;
        token->value.boolean = 1;
        json->ptr += 4;
        return;
    } else if (!strncmp(json->ptr, "false", 5)) {
        token->type = T_BOOLEAN;
        token->value.boolean = 0;
        json->ptr += 5;
        return;
    } else if (!strncmp(json->ptr, "null", 4)) {
        token->type = T_NULL;
        json->ptr += 4;
        return;
    } else if (json->cfg->decode_invalid_numbers) {
        /*
         * RFC4627: Numeric values that cannot be represented as sequences of
         * digits (such as Infinity and NaN) are not permitted.
         */
        if (!strncmp(json->ptr, "inf", 3)) {
            token->type = T_NUMBER;
            token->value.number = INFINITY;
            json->ptr += 3;
            return;
        } else if (!strncmp(json->ptr, "-inf", 4)) {
            token->type = T_NUMBER;
            token->value.number = -INFINITY;
            json->ptr += 4;
            return;
        } else if (!strncmp(json->ptr, "nan", 3) ||
                   !strncmp(json->ptr, "-nan", 3)) {
            token->type = T_NUMBER;
            token->value.number = NAN;
            json->ptr += (*json->ptr == '-' ? 4 : 3);
            return;
        }
    }

    /* Token starts with t/f/n but isn't recognised above. */
    json_set_token_error(token, json, "invalid token");
}

enum err_context_length {
    ERR_CONTEXT_ARROW_LENGTH = 4,
    ERR_CONTEXT_MAX_LENGTH_BEFORE = 8,
    ERR_CONTEXT_MAX_LENGTH_AFTER = 8,
    ERR_CONTEXT_MAX_LENGTH = ERR_CONTEXT_MAX_LENGTH_BEFORE +
    ERR_CONTEXT_MAX_LENGTH_AFTER + ERR_CONTEXT_ARROW_LENGTH,
};

/**
 * Copy characters near wrong token with the position @a
 * column_index to a static string buffer @a err_context and lay
 * out arrow " >> " before this token.
 *
 * @param context      String static buffer to fill.
 * @param json         Structure with pointers to parsing string.
 * @param column_index Position of wrong token in the current
 *                     line.
 */
static void fill_err_context(char *err_context, json_parse_t *json,
                             int column_index)
{
    assert(column_index >= 0);
    int length_before = column_index < ERR_CONTEXT_MAX_LENGTH_BEFORE ?
                        column_index : ERR_CONTEXT_MAX_LENGTH_BEFORE;
    const char *src = json->cur_line_ptr + column_index - length_before;
    /* Fill error context before the arrow. */
    memcpy(err_context, src, length_before);
    err_context += length_before;
    src += length_before;

    /* Make the arrow. */
    *(err_context++) = ' ';
    memset(err_context, '>', ERR_CONTEXT_ARROW_LENGTH - 2);
    err_context += ERR_CONTEXT_ARROW_LENGTH - 2;
    *(err_context++) = ' ';

    /* Fill error context after the arrow. */
    const char *end = err_context + ERR_CONTEXT_MAX_LENGTH_AFTER;
    for (; err_context < end && *src != '\0' && *src != '\n'; ++src,
         ++err_context)
        *err_context = *src;
    *err_context = '\0';
}

/* This function does not return.
 * DO NOT CALL WITH DYNAMIC MEMORY ALLOCATED.
 * The only supported exception is the temporary parser string
 * json->tmp struct.
 * json and token should exist on the stack somewhere.
 * luaL_error() will long_jmp and release the stack */
static void json_throw_parse_error(lua_State *l, json_parse_t *json,
                                   const char *exp, json_token_t *token)
{
    const char *found;
    struct ibuf *ibuf = json->tmp->ibuf;
    strbuf_destroy(json->tmp);
    cord_ibuf_put(ibuf);

    if (token->type == T_ERROR)
        found = token->value.string;
    else
        found = json_token_type_name[token->type];

    char err_context[ERR_CONTEXT_MAX_LENGTH + 1];
    fill_err_context(err_context, json, token->column_index);

    /* Note: token->column_index is 0 based, display starting from 1 */
    luaL_error(l, "Expected %s but found %s on line %d at character %d here "
               "'%s'", exp, found, json->line_count, token->column_index + 1,
               err_context);
}

static inline void json_decode_ascend(json_parse_t *json)
{
    json->current_depth--;
}

static void json_decode_descend(lua_State *l, json_parse_t *json, int slots)
{
    json->current_depth++;

    if (json->current_depth <= json->cfg->decode_max_depth &&
        lua_checkstack(l, slots)) {
        return;
    }

    char err_context[ERR_CONTEXT_MAX_LENGTH + 1];
    fill_err_context(err_context, json, json->ptr - json->cur_line_ptr - 1);

    struct ibuf *ibuf = json->tmp->ibuf;
    strbuf_destroy(json->tmp);
    cord_ibuf_put(ibuf);
    luaL_error(l, "Found too many nested data structures (%d) on line %d at cha"
               "racter %d here '%s'", json->current_depth, json->line_count,
               json->ptr - json->cur_line_ptr, err_context);
}

static void json_parse_object_context(lua_State *l, json_parse_t *json)
{
    json_token_t token;

    /* 3 slots required:
     * .., table, key, value */
    json_decode_descend(l, json, 3);

    lua_newtable(l);
    if (json->cfg->decode_save_metatables)
        luaL_setmaphint(l, -1);

    json_next_token(json, &token);

    /* Handle empty objects */
    if (token.type == T_OBJ_END) {
        json_decode_ascend(json);
        return;
    }

    while (1) {
        if (token.type != T_STRING)
            json_throw_parse_error(l, json, "object key string", &token);

        /* Push key */
        lua_pushlstring(l, token.value.string, token.string_len);

        json_next_token(json, &token);
        if (token.type != T_COLON)
            json_throw_parse_error(l, json, "colon", &token);

        /* Fetch value */
        json_next_token(json, &token);
        json_process_value(l, json, &token);

        /* Set key = value */
        lua_rawset(l, -3);

        json_next_token(json, &token);

        if (token.type == T_OBJ_END) {
            json_decode_ascend(json);
            return;
        }

        if (token.type != T_COMMA)
            json_throw_parse_error(l, json, "comma or '}'", &token);

        json_next_token(json, &token);
    }
}

/* Handle the array context */
static void json_parse_array_context(lua_State *l, json_parse_t *json)
{
    json_token_t token;
    int i;

    /* 2 slots required:
     * .., table, value */
    json_decode_descend(l, json, 2);

    lua_newtable(l);
    if (json->cfg->decode_save_metatables)
        luaL_setarrayhint(l, -1);

    json_next_token(json, &token);

    /* Handle empty arrays */
    if (token.type == T_ARR_END) {
        json_decode_ascend(json);
        return;
    }

    for (i = 1; ; i++) {
        json_process_value(l, json, &token);
        lua_rawseti(l, -2, i);            /* arr[i] = value */

        json_next_token(json, &token);

        if (token.type == T_ARR_END) {
            json_decode_ascend(json);
            return;
        }

        if (token.type != T_COMMA)
            json_throw_parse_error(l, json, "comma or ']'", &token);

        json_next_token(json, &token);
    }
}

/* Handle the "value" context */
static void json_process_value(lua_State *l, json_parse_t *json,
                               json_token_t *token)
{
    switch (token->type) {
    case T_STRING:
        lua_pushlstring(l, token->value.string, token->string_len);
        break;;
    case T_UINT:
        luaL_pushuint64(l, token->value.ival);
        break;;
    case T_INT:
        luaL_pushint64(l, token->value.ival);
        break;;
    case T_NUMBER:
        luaL_checkfinite(l, json->cfg, token->value.number);
        lua_pushnumber(l, token->value.number);
        break;;
    case T_BOOLEAN:
        lua_pushboolean(l, token->value.boolean);
        break;;
    case T_OBJ_BEGIN:
        json_parse_object_context(l, json);
        break;;
    case T_ARR_BEGIN:
        json_parse_array_context(l, json);
        break;;
    case T_NULL:
    luaL_pushnull(l);
        break;;
    default:
        json_throw_parse_error(l, json, "value", token);
    }
}

static int json_decode(lua_State *l)
{
    json_parse_t json;
    json_token_t token;
    size_t json_len;

    luaL_argcheck(l, lua_gettop(l) == 2 || lua_gettop(l) == 1, 1,
                  "expected 1 or 2 arguments");

    struct luaL_serializer *cfg = luaL_checkserializer(l);

    /*
     * user_cfg is per-call local version of serializer instance
     * options: it is used if a user passes custom options to
     * :decode() method within a separate argument. In this case
     * it is required to avoid modifying options of the instance.
     * Life span of user_cfg is restricted by the scope of
     * :decode() so it is enough to allocate it on the stack.
     */
    struct luaL_serializer user_cfg;
    json.cfg = cfg;
    if (lua_gettop(l) == 2) {
        /*
         * on_update triggers are left uninitialized for user_cfg.
         * The decoding code don't (and shouldn't) run them.
         */
        luaL_serializer_copy_options(&user_cfg, cfg);
        luaL_serializer_parse_options(l, &user_cfg);
        lua_pop(l, 1);
        json.cfg = &user_cfg;
    }

    json.data = luaL_checklstring(l, 1, &json_len);
    json.current_depth = 0;
    json.ptr = json.data;
    json.line_count = 1;
    json.cur_line_ptr = json.data;

    /* Detect Unicode other than UTF-8 (see RFC 4627, Sec 3)
     *
     * CJSON can support any simple data type, hence only the first
     * character is guaranteed to be ASCII (at worst: '"'). This is
     * still enough to detect whether the wrong encoding is in use. */
    if (json_len >= 2 && (!json.data[0] || !json.data[1]))
        luaL_error(l, "JSON parser does not support UTF-16 or UTF-32");

    /* Ensure the temporary buffer can hold the entire string.
     * This means we no longer need to do length checks since the decoded
     * string must be smaller than the entire json string */
    strbuf_t decode_buf;
    json.tmp = &decode_buf;
    struct ibuf *ibuf = cord_ibuf_take();
    strbuf_create(&decode_buf, json_len, ibuf);

    json_next_token(&json, &token);
    json_process_value(l, &json, &token);

    /* Ensure there is no more input left */
    json_next_token(&json, &token);

    if (token.type != T_END)
        json_throw_parse_error(l, &json, "the end", &token);

    strbuf_destroy(&decode_buf);
    cord_ibuf_put(ibuf);

    return 1;
}

/* ===== INITIALISATION ===== */

static int
json_new(lua_State *L);

static const luaL_Reg jsonlib[] = {
    { "encode", json_encode },
    { "decode", json_decode },
    { "new",    json_new },
    { NULL, NULL}
};

static int
json_new(lua_State *L)
{
    luaL_newserializer(L, NULL, jsonlib);
    return 1;
}

int
luaopen_json(lua_State *L)
{
    json_create_tokens();
    luaL_json_default = luaL_newserializer(L, "json", jsonlib);
    luaL_pushnull(L);
    lua_setfield(L, -2, "null"); /* compatibility with cjson */
    return 1;
}

/* vi:ai et sw=4 ts=4:
 */
