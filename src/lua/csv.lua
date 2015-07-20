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
        char csv_delim;
        char csv_quote;

        int csv_invalid;
        int csv_ending_spaces;

        void *(*csv_realloc)(void*, size_t);

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
    typedef struct csv csv_t;
    typedef struct csv_iterator csv_iterator_t;
    void csv_iter_create(struct csv_iterator *it, struct csv *csv);
    int csv_next(struct csv_iterator *);
    void csv_feed(struct csv_iterator *, const char *);
    int csv_escape_field(struct csv *csv, const char *field, size_t field_len, char *dst, size_t buf_size);
    enum {
        CSV_IT_OK,
        CSV_IT_EOL,
        CSV_IT_NEEDMORE,
        CSV_IT_EOF,
        CSV_IT_ERROR
    };
]]

local make_readable = function(s)
    rd = {}
    rd.val = s
    rd.read = function(self, cnt)
        local res = self.val;
        self.val = ""
        return res
    end
    return rd
end

local make_writable = function()
    wr = {}
    wr.returnstring = ""
    wr.write = function(self, s)
        wr.returnstring = wr.returnstring .. s
    end
    return wr
end


local iter = function(csvstate, i)
    local readable = csvstate[1]
    local csv_chunk_size = csvstate[2]
    local csv = csvstate[3]
    local it = csvstate[4]
    local tup = {}
    local st = ffi.C.csv_next(it)
    while st ~= ffi.C.CSV_IT_EOF do
        if st == ffi.C.CSV_IT_NEEDMORE then
            ffi.C.csv_feed(it, readable:read(csv_chunk_size))
        elseif st == ffi.C.CSV_IT_EOL then
            i = i + 1
            return i, tup
        elseif st == ffi.C.CSV_IT_OK then
            table.insert(tup, ffi.string(it[0].field, it[0].field_len))
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

local module = {}

module.delimiter = ','
module.quote = '"'

--@brief parse csv string by string
--@param readable must be string or object with method read(num) returns string
--@param csv_chunk_size (default 4096). Parser will read by csv_chunk_size symbols
--@return iter function, iterator state
module.iterate = function(readable, csv_chunk_size)
    csv_chunk_size = csv_chunk_size or 4096
    if type(readable) == "string" then
        readable = make_readable(readable)
    end
    if type(readable.read) ~= "function" then
        error("Usage: load(object with method read(num) returns string)")
    end

    local str = readable:read(csv_chunk_size)
    if not str then
        error("Usage: load(object with method read(num) returns string)")
    end
    local it = ffi.new('csv_iterator_t[1]')
    local csv = ffi.new('csv_t[1]')
    ffi.C.csv_create(csv)
    csv[0].csv_delim = string.byte(module.delimiter)
    csv[0].csv_quote = string.byte(module.quote)
    ffi.C.csv_iter_create(it, csv)
    ffi.C.csv_feed(it, str)

    return iter, {readable, csv_chunk_size, csv, it}, 0
end

--@brief parse csv and make table
--@param skip_lines is number of lines to skip.
--@return table
module.load = function(readable, skip_lines, csv_chunk_size)
    skip_lines = skip_lines or 0
    csv_chunk_size = csv_chunk_size or 4096
    result = {}
    for i, tup in module.iterate(readable, csv_chunk_size) do
        if i > skip_lines then             
            result[i - skip_lines] = tup
        end
    end

    return result
end

--@brief dumps tuple or table as csv
--@param writable must be object with method write(string) like file or socket
--@return there is no writable it returns csv as string
module.dump = function(t, writable)
    if type(writable) == "nil" then
        writable = make_writable()
    end
    if type(writable.write) ~= "function" or type(t) ~= "table" then
        error("Usage: dump(writable, table)")
    end
    local csv = ffi.new('csv_t[1]')
    ffi.C.csv_create(csv)
    csv[0].csv_delim = string.byte(module.delimiter)
    csv[0].csv_quote = string.byte(module.quote)
    local bufsz = 256
    local buf = csv[0].csv_realloc(ffi.cast(ffi.typeof('void *'), 0), bufsz)
    if type(t[1]) ~= 'table' then
        t = {t}
    end
    for k, line in pairs(t) do
        local first = true
        for k2, field in pairs(line) do
            strf = tostring(field)
            if (strf:len() + 1) * 2 > bufsz then
                bufsz = (strf:len() + 1) * 2
                buf = csv[0].csv_realloc(buf, bufsz)
            end
            local len = ffi.C.csv_escape_field(csv, strf, string.len(strf), buf, bufsz)
            if first then
                first = false
            else
                writable:write(module.delimiter)
            end
            writable:write(ffi.string(buf, len))
        end
        writable:write('\n')
    end
    ffi.C.csv_destroy(csv)
    csv[0].csv_realloc(buf, 0)
    if writable.returnstring then
        return writable.returnstring
    end
end

return module
