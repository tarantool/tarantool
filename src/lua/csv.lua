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

local module = {}

--@brief parse csv string by string
--@param readable must be string or object with method read(num) returns string
--@param opts.chunk_size (default 4096). Parser will read by chunk_size symbols
--@param opts.delimiter (default ',').
--@param opts.quote_char (default '"').
--@param opts.skip_head_lines (default 0). Skip header.
--@return iter function, iterator state
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

--@brief parse csv and make table
--@return table
module.load = function(readable, opts)
    opts = opts or {}
    local result = {}
    for i, tup in module.iterate(readable, opts) do
        result[i] = tup
    end
    return result
end

--@brief dumps tuple or table as csv
--@param t is tuple or table
--@param writable must be object with method write(string) like file or socket
--@param opts.delimiter (default ',').
--@param opts.quote_char (default '"').
--@return there is no writable it returns csv as string
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
