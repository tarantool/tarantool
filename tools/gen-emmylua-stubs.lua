-- Export EmmyLua annotations from Lua sources into a stub file.
--
-- Usage:
--
--   luajit gen-emmylua-stubs.lua --out <file> <input.lua>...
--
-- For every input file the annotating comments ('---@...')
-- together with the bodyless declarations they annotate are
-- collected and written as a single ---@meta file into <file>.
-- The set of directives and the rules of associating a comment
-- block with a declaration follow the EmmyLua parser used by
-- emmylua_doc_cli.

local CODE_ONLY = {
    cast = true,
    diagnostic = true,
}

local TYPE_TAGS = {
    class = true,
    enum = true,
}

local FUNCTION_TAGS = {
    async = true,
    generic = true,
    nodiscard = true,
    overload = true,
    param = true,
    return_overload = true,
    -- `return` is a reserved keyword in Lua and cannot be used as
    -- name.
    ['return'] = true,
    vararg = true,
}

local NON_PUBLIC = {
    private = true,
    protected = true,
    package = true,
}

local function fail(fmt, ...)
    io.stderr:write('gen-emmylua-stubs: ' .. string.format(fmt, ...) .. '\n')
    os.exit(1)
end

local function parse_args(argv)
    local args = {files = {}}
    local i = 1
    while i <= #argv do
        local arg = argv[i]
        if arg == '--out' then
            i = i + 1
            args.out = argv[i]
        elseif arg:sub(1, 2) == '--' then
            fail("unknown option '%s'", arg)
        else
            table.insert(args.files, arg)
        end
        i = i + 1
    end
    if args.out == nil then
        fail('--out is required')
    end
    if #args.files == 0 then
        fail('no input files')
    end
    return args
end

local function read_file(path)
    local file, err = io.open(path, 'r')
    if file == nil then
        fail("cannot open '%s': %s", path, err)
    end
    assert(file)
    local content = file:read('*a')
    file:close()
    return content
end

local function split_lines(content)
    content = content:gsub('\r\n', '\n'):gsub('\r', '\n')
    local lines = {}
    local pos = 1
    while true do
        local nl = content:find('\n', pos, true)
        if nl == nil then
            break
        end
        table.insert(lines, content:sub(pos, nl - 1))
        pos = nl + 1
    end
    table.insert(lines, content:sub(pos))
    return lines
end

local function directive_name(line)
    return line:match('^%-%-%-@([%a_][%w_]*)')
end

local function strip(line)
    return (line:gsub('^%s+', ''):gsub('%s+$', ''))
end

-- The first statement after the annotation block, skipping blank
-- lines and other comments.
local function find_statement(lines, first)
    for i = first, #lines do
        local line = strip(lines[i])
        if line ~= '' and line:sub(1, 2) ~= '--' then
            return lines[i]
        end
    end
    return nil
end

-- The position of the parenthesis closing the one that starts the
-- argument list, taking nested parentheses into account.
local function find_closing_paren(text)
    local depth = 1
    for i = 1, #text do
        local char = text:sub(i, i)
        if char == '(' then
            depth = depth + 1
        elseif char == ')' then
            depth = depth - 1
            if depth == 0 then
                return i
            end
        end
    end
    return nil
end

local function collect_arguments(lines, index, rest)
    local text = rest
    local i = index
    while true do
        local close = find_closing_paren(text)
        if close ~= nil then
            return (text:sub(1, close - 1)
                :gsub('%s+', ' ')
                :gsub('^%s+', '')
                :gsub('%s+$', ''))
        end
        i = i + 1
        if i > #lines then
            return nil
        end
        text = text .. ' ' .. lines[i]
    end
end

local function emit_type(statement, declared, body)
    local name = statement:match('^local%s+([%a_][%w_]*)%s*=')
    if name ~= nil then
        table.insert(body, 'local ' .. name .. ' = {}')
        declared[name] = true
    end
end

local function emit_function(
    lines, index, statement, declared, roots, order, body)
    local name, rest =
        statement:match('^local%s+function%s+([%w_%.%:]+)%s*%((.*)$')
    local prefix = name ~= nil and 'local function' or nil
    if name == nil then
        name, rest = statement:match('^function%s+([%w_%.%:]+)%s*%((.*)$')
        prefix = name ~= nil and 'function' or nil
    end
    if name == nil then
        name, rest = statement:match('^([%w_%.%:]+)%s*=%s*function%s*%((.*)$')
        prefix = name ~= nil and 'function' or nil
    end
    if name == nil then
        return
    end

    local arguments = collect_arguments(lines, index, rest)
    if arguments == nil then
        return
    end
    table.insert(body, prefix .. ' ' .. name .. '(' .. arguments .. ') end')

    if prefix ~= 'local function' then
        local root = name:match('^([%w_]+)[%.:]')
        if root ~= nil and not declared[root] and not roots[root] then
            roots[root] = true
            table.insert(order, root)
        end
    end
end

local function generate(out_file, files)
    local body = {}
    local declared = {}
    local roots = {}
    local order = {}

    for _, path in ipairs(files) do
        local lines = split_lines(read_file(path))
        local nlines = #lines
        local i = 1
        while i <= nlines do
            if lines[i]:sub(1, 3) ~= '---' then
                i = i + 1
            else
                local directives = {}
                local j = i
                while j <= nlines and lines[j]:sub(1, 3) == '---' do
                    local name = directive_name(lines[j])
                    if name ~= nil then
                        table.insert(directives, name)
                    end
                    j = j + 1
                end

                local has_dir = #directives > 0
                local code_only = has_dir
                local has_type = false
                local has_function = false
                local visible = true
                for _, name in ipairs(directives) do
                    if not CODE_ONLY[name] then
                        code_only = false
                    end
                    if TYPE_TAGS[name] then
                        has_type = true
                    end
                    if FUNCTION_TAGS[name] then
                        has_function = true
                    end
                    if NON_PUBLIC[name] then
                        visible = false
                    end
                end

                if has_dir and not code_only then
                    for k = i, j - 1 do
                        table.insert(body, lines[k])
                    end

                    if visible then
                        local statement = find_statement(lines, j)
                        if statement ~= nil then
                            if has_type then
                                emit_type(statement, declared, body)
                            elseif has_function then
                                emit_function(lines, j, statement, declared,
                                              roots, order, body)
                            end
                        end
                    end

                    table.insert(body, '')
                end

                i = j
            end
        end
    end

    local out = {
        '---@meta',
        '-- Auto-generated by tools/gen-emmylua-stubs.lua. Do not edit.',
    }
    if #order > 0 then
        table.insert(out, '')
        for _, root in ipairs(order) do
            table.insert(out, 'local ' .. root .. ' = {}')
        end
    end
    table.insert(out, '')
    for _, line in ipairs(body) do
        table.insert(out, line)
    end

    local file, err = io.open(out_file, 'w')
    if file == nil then
        fail("cannot write '%s': %s", out_file, err)
    end
    assert(file)
    file:write(table.concat(out, '\n'))
    file:write('\n')
    file:close()
end

local args = parse_args(arg)
generate(args.out, args.files)
