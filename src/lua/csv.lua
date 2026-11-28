-- csv.lua (internal file)

local ffi = require('ffi')
local log = require('log')

ffi.cdef[[
    typedef void (*csv_emit_row_t)(void *ctx);
    typedef void (*csv_emit_field_t)(void *ctx, const char *field, const char *end);

    struct csv
    {
        void *emit_ctx;
        csv_emit_row_t emit_row;
        csv_emit_field_t emit_field;
        char delimiter;
        char quote_char;

        char prev_symbol;
        int error_status;
        int ending_spaces;

        void *(*realloc)(void*, size_t);

        int state;
        char *buf;
        char *bufp;
        size_t buf_len;
    };

    void csv_create(struct csv *csv);
    void csv_destroy(struct csv *csv);
    void csv_setopt(struct csv *csv, int opt, ...);

    struct csv_iterator {
        struct csv *csv;
        const char *buf_begin;
        const char *buf_end;
        const char *field;
        size_t field_len;
    };
    void csv_iterator_create(struct csv_iterator *it, struct csv *csv);
    int csv_next(struct csv_iterator *);
    void csv_feed(struct csv_iterator *, const char *, size_t);
    size_t csv_escape_field(struct csv *csv, const char *field, size_t field_len, char *dst, size_t buf_size);
    enum {
        CSV_IT_OK,
        CSV_IT_EOL,
        CSV_IT_NEEDMORE,
        CSV_IT_EOF,
        CSV_IT_ERROR
    };
]]

local iter = function(csvstate, i)
    local readable = csvstate[1]
    local csv_chunk_size = csvstate[2]
    local csv = csvstate[3]
    local it = csvstate[4]
    local tup = {}
    local st = ffi.C.csv_next(it)
    while st ~= ffi.C.CSV_IT_EOF do
        if st == ffi.C.CSV_IT_NEEDMORE then
            if readable then
                local buf = readable:read(csv_chunk_size)
                ffi.C.csv_feed(it, buf, string.len(buf))
                -- extend buf lifetime - csv_feed saves pointers
                csvstate[5] = buf
            else
                ffi.C.csv_feed(it, "", 0)
            end
        elseif st == ffi.C.CSV_IT_EOL then
            i = i + 1
            if i > 0 then
                return i, tup
            end
        elseif st == ffi.C.CSV_IT_OK then
            if i >= 0 then
                tup[#tup + 1] = ffi.string(it.field, it.field_len)
            end
        elseif st == ffi.C.CSV_IT_ERROR then
            log.warn("CSV file has errors")
            break
        elseif st == ffi.C.CSV_IT_EOF then
            ffi.C.csv_destroy(csv)
            break
        end
        st = ffi.C.csv_next(it)
    end
end

---The `csv` module handles records formatted according to
---Comma-Separated-Values (CSV) rules.
---
---The default formatting rules are:
---* Lua [escape sequences](http://www.lua.org/pil/2.4.html) such as `\n`
---  or `\10` are legal within strings but not within files.
---* Commas designate end-of-field.
---* Line feeds, or line feeds plus carriage returns, designate end-of-record.
---* Leading or trailing spaces are ignored.
---* Quote marks may enclose fields or parts of fields.
---* When enclosed by quote marks, commas and line feeds and spaces are treated
---  as ordinary characters, and a pair of quote marks "" is treated as a
---  single quote mark.
local module = {}

---Options for `csv.iterate()`, `csv.load()`, and `csv.dump()`.
---@class csv.options
---@field delimiter? string (default: comma) single-byte character to
---    designate end-of-field
---@field quote_char? string (default: quote mark) single-byte character
---    to designate encloser of string
---@field chunk_size? integer (default: 4096) number of characters to read
---    at once (usually for file-IO efficiency)
---@field skip_head_lines? integer (default: 0) number of lines to skip at
---    the start (usually for a header)

---Iterate through CSV records.
---
---Form a Lua iterator function for going through CSV records one
---field at a time.
---
---Use of an iterator is strongly recommended if the amount of data is
---large (ten or more megabytes).
---
---**Example:**
---
---[`csv.iterate()`](lua://csv.iterate) is the low level of
---[`csv.load()`](lua://csv.load) and [`csv.dump()`](lua://csv.dump).
---
---To illustrate that, here is a function which is the same as the
---[`csv.load()`](lua://csv.load) function, as seen in the Tarantool
---source code.
---
--- ```tarantoolsession
--- tarantool> load = function(readable, opts)
---          >   opts = opts or {}
---          >   local result = {}
---          >   for i, tup in csv.iterate(readable, opts) do
---          >     result[i] = tup
---          >   end
---          >   return result
---          > end
--- ---
--- ...
--- tarantool> load('a,b,c')
--- ---
--- - - - a
---     - b
---     - c
--- ...
--- ```
---
---@param readable string | { read: fun(self: table, count?: integer): string }
---@param opts? csv.options
---@return fun(state: table, i: integer): integer?, string[]? iter
---@return table state
---@return integer initial
module.iterate = function(readable, opts)
    opts = opts or {}
    if type(readable) ~= "string" and type(readable.read) ~= "function" then
        error("Usage: load(string or object with method read(num)" ..
              "returns string)")
    end
    if not opts.chunk_size then
        opts.chunk_size = 4096
    end
    if not opts.delimiter then
        opts.delimiter = ','
    end
    if not opts.quote_char then
        opts.quote_char = '"'
    end
    if not opts.skip_head_lines then
        opts.skip_head_lines = 0
    end
    local str
    if type(readable) == "string" then
        str = readable
        readable = nil
    else
        str = readable:read(opts.chunk_size)
    end

    if not str then --read not works
        error("Usage: load(string or object with method read(num)" ..
              "returns string)")
    end
    local it = ffi.new('struct csv_iterator')
    local csv = ffi.new('struct csv')
    ffi.C.csv_create(csv)
    ffi.gc(csv, ffi.C.csv_destroy)

    csv.delimiter = string.byte(opts.delimiter)
    csv.quote_char = string.byte(opts.quote_char)
    ffi.C.csv_iterator_create(it, csv)
    ffi.C.csv_feed(it, str, string.len(str))

    -- csv_feed remembers the pointer;
    -- str included in csv state to make sure it lives long enough
    return iter, {readable, opts.chunk_size, csv, it, str}, -opts.skip_head_lines
end

---Get CSV-formatted input from `readable` and return a table as output.
---
---Usually `readable` is either a string or a file opened for reading.
---
---Usually `opts` is not specified.
---
---**Example:**
---
---Readable string has 3 fields, field#2 has comma and space so use quote marks:
---
--- ```tarantoolsession
--- tarantool> csv = require('csv')
--- ---
--- ...
--- tarantool> csv.load('a,"b,c ",d')
--- ---
--- - - - a
---     - 'b,c '
---     - d
--- ...
--- ```
---
---Readable string contains 2-byte character = Cyrillic Letter Palochka:
---(This displays a palochka if and only if character set = UTF-8.)
---
--- ```tarantoolsession
--- tarantool> csv.load('a\\211\\128b')
--- ---
--- - - - a\211\128b
--- ...
--- ```
---
---Semicolon instead of comma for the delimiter:
---
--- ```tarantoolsession
--- tarantool> csv.load('a,b;c,d', {delimiter = ';'})
--- ---
--- - - - a,b
---     - c,d
--- ...
--- ```
---
---Readable file :file:`./file.csv` contains two CSV records. Explanation of
---`fio` is in section [fio](lua://fio). Source CSV file and example
---respectively:
---
--- ```tarantoolsession
---
--- tarantool> -- input in file.csv is:
--- tarantool> -- a,"b,c ",d
--- tarantool> -- a\\211\\128b
--- tarantool> fio = require('fio')
--- ---
--- ...
--- tarantool> f = fio.open('./file.csv', {'O_RDONLY'})
--- ---
--- ...
--- tarantool> csv.load(f, {chunk_size = 4096})
--- ---
--- - - - a
---     - 'b,c '
---     - d
---   - - a\\211\\128b
--- ...
--- tarantool> f:close()
--- ---
--- - true
--- ...
--- ```
---
---@param readable string | { read: fun(self: table, count?: integer): string }
---@param opts? csv.options
---@return string[][] loaded_value
module.load = function(readable, opts)
    opts = opts or {}
    local result = {}
    for i, tup in module.iterate(readable, opts) do
        result[i] = tup
    end
    return result
end

---Get table input and return a CSV-formatted string as output.
---
---Or, get table input from `t` and put the output in `writable`.
---
---Usually `opts` is not specified.
---
---Usually `writable`, if specified, is a file opened for writing.
---[`csv.dump()`](lua://csv.dump) is the reverse of
---[`csv.load()`](lua://csv.load).
---
---**Example:**
---
---CSV-table has 3 fields, field#2 has "," so result has quote marks
---
--- ```tarantoolsession
--- tarantool> csv = require('csv')
--- ---
--- ...
--- tarantool> csv.dump({'a','b,c ','d'})
--- ---
--- - 'a,"b,c ",d
---
--- '
--- ...
--- ```
---
---Round Trip: from string to table and back to string
---
--- ```tarantoolsession
--- tarantool> csv_table = csv.load('a,b,c')
--- ---
--- ...
--- tarantool> csv.dump(csv_table)
--- ---
--- - 'a,b,c
---
--- '
--- ...
--- ```
---
---@param t table a table which can be formatted according to the CSV
---@param opts? csv.options
---@param writable? { write: fun(self: table, s: string): boolean }
---@return string? dumped_value
module.dump = function(t, opts, writable)
    opts = opts or {}
    writable = writable or nil
    if not opts.delimiter then
        opts.delimiter = ','
    end
    if not opts.quote_char then
        opts.quote_char = '"'
    end

    if (type(writable) ~= "nil" and type(writable.write) ~= "function")
        or type(t) ~= "table" then
        error("Usage: dump(table[, opts, writable])")
    end
    local csv = ffi.new('struct csv')
    ffi.C.csv_create(csv)
    ffi.gc(csv, ffi.C.csv_destroy)
    csv.delimiter = string.byte(opts.delimiter)
    csv.quote_char = string.byte(opts.quote_char)

    local bufsz = 256
    local buf = csv.realloc(ffi.cast(ffi.typeof('void *'), 0), bufsz)
    if type(t[1]) ~= 'table' then
        t = {t}
    end
    local result_table
    if type(writable) == 'nil' then
        result_table = {}
    end
    for _, line in pairs(t) do
        local first = true
        local output_tuple = {}
        for _, field in pairs(line) do
            local strf = tostring(field)
            local buf_new_size = (strf:len() + 1) * 2
            if buf_new_size > bufsz then
                bufsz = buf_new_size
                buf = csv.realloc(buf, bufsz)
            end
            local len = ffi.C.csv_escape_field(csv, strf,
                string.len(strf), buf, bufsz)
            if first then
                first = false
            else
                output_tuple[#output_tuple + 1] = opts.delimiter
            end
            output_tuple[#output_tuple + 1] = ffi.string(buf, len)
        end

        output_tuple[#output_tuple + 1] = '\n'
        if result_table then
            result_table[#result_table + 1] = table.concat(output_tuple)
        else
            writable:write(table.concat(output_tuple))
        end
    end
    ffi.C.csv_destroy(csv)
    csv.realloc(buf, 0)
    if result_table then
        return table.concat(result_table)
    end
end

return module
