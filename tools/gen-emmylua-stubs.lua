-- Export EmmyLua annotations from Lua sources into a stub file.
--
-- Usage:
--
--   luajit gen-emmylua-stubs.lua --out <file> [--return <name>] <input.lua>...
--
-- For every input file the annotating comments ('---@...')
-- together with the bodyless declarations they annotate are
-- collected and written as a single ---@meta file into <file>.
-- When --return is given, the stub ends with `return <name>`, so
-- that require() resolves the module value in a workspace that
-- contains only the generated stubs. The name must be declared by
-- one of the extracted annotation blocks. Function fields of a
-- `---@class` with the same name as the module value are also emitted
-- as module functions, so they are visible in the module page and in
-- completion.
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

-- `---@type T` on a table declaration or on a top-level `return { ... }`
-- makes the declaration usable as a module value. Other `---@type`
-- annotations (scalars, locals without a table literal) are skipped.
local VALUE_TAGS = {
    type = true,
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
        elseif arg == '--return' then
            i = i + 1
            if argv[i] == nil then
                fail('--return requires a name')
            end
            args.return_name = argv[i]
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
    if args.return_name == '' then
        fail('--return requires a name')
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

-- The table a function is assigned to, or nil for a local function.
local function function_root(statement)
    local name = statement:match('^local%s+function%s+([%w_%.%:]+)%s*%(')
    if name ~= nil then
        return nil
    end
    name = statement:match('^function%s+([%w_%.%:]+)%s*%(')
    if name == nil then
        name = statement:match('^([%w_%.%:]+)%s*=%s*function%s*%(')
    end
    if name == nil then
        return nil
    end
    return name:match('^([%w_]+)[%.:]')
end

local function declare_root_name(root, declared, roots, body, docs)
    if root == nil or declared[root] or roots[root] then
        return
    end
    if docs ~= nil then
        for _, line in ipairs(docs) do
            table.insert(body, line)
        end
    end
    table.insert(body, 'local ' .. root .. ' = {}')
    declared[root] = true
    roots[root] = true
end

-- Declare the table before the first annotated function that uses it.
-- This keeps `---@class` blocks above the module table.
local function declare_root(statement, declared, roots, body, return_name,
                            docs)
    local root = function_root(statement)
    if root ~= return_name then
        docs = nil
    end
    declare_root_name(root, declared, roots, body, docs)
end

-- The first statement after the annotation block, skipping blank
-- lines and other comments. Return its text and the line index.
local function find_statement(lines, first)
    for i = first, #lines do
        local line = strip(lines[i])
        if line ~= '' and line:sub(1, 2) ~= '--' then
            return line, i
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

local function class_field(line)
    local name, typ = line:match('^%-%-%-@field%s+([%w_]+)%s+(.+)$')
    if name == nil then
        return nil
    end
    return name, strip(typ)
end

local function split_top_level(text)
    local parts = {}
    local depth = 0
    local start = 1
    for i = 1, #text do
        local char = text:sub(i, i)
        if char == '<' or char == '{' or char == '(' or char == '[' then
            depth = depth + 1
        elseif char == '>' or char == '}' or char == ')' or char == ']' then
            depth = depth - 1
        elseif char == ',' and depth == 0 then
            table.insert(parts, strip(text:sub(start, i - 1)))
            start = i + 1
        end
    end
    table.insert(parts, strip(text:sub(start)))
    return parts
end

local function parse_argument(arg)
    if arg:match('^%.%.%.') then
        return '...', arg:match('^%.%.%.%s*:%s*(.+)$') or 'any', false
    end
    local name, optional, typ =
        arg:match('^([%w_]+)(%??)%s*:%s*(.+)$')
    if name == nil then
        name, optional = arg:match('^([%w_]+)(%??)$')
        typ = 'any'
    end
    if name == nil then
        name = arg
        optional = ''
        typ = 'any'
    end
    return name, typ, optional == '?'
end

-- Emit function fields of the module class as module functions. The
-- class itself stays for the type page, while the module page and
-- completion get all the functions.
local function emit_class_functions(lines, module_name, names, declared,
                                    roots, body, module_docs)
    local docs = {}
    for _, line in ipairs(lines) do
        local name, typ = class_field(line)
        if name ~= nil then
            local args, ret = typ:match('^fun%s*%((.-)%)%s*:%s*(.+)$')
            local full_name = module_name .. '.' .. name
            if names[full_name] == nil then
                declare_root_name(module_name, declared, roots, body,
                                  module_docs)
                for _, doc in ipairs(docs) do
                    table.insert(body, doc)
                end
                if args ~= nil then
                    local arguments = {}
                    for _, arg in ipairs(split_top_level(args)) do
                        if arg ~= '' then
                            local arg_name, arg_type, optional =
                                parse_argument(arg)
                            table.insert(arguments, arg_name)
                            table.insert(body, '---@param ' .. arg_name ..
                                         (optional and '?' or '') .. ' ' ..
                                         arg_type)
                        end
                    end
                    for _, result in ipairs(split_top_level(ret)) do
                        if result ~= '' then
                            table.insert(body, '---@return ' .. result)
                        end
                    end
                    table.insert(body, 'function ' .. full_name .. '(' ..
                                 table.concat(arguments, ', ') .. ') end')
                else
                    table.insert(body, '---@type ' .. typ)
                    table.insert(body, full_name .. ' = {}')
                end
                table.insert(body, '')
                names[full_name] = true
            end
            docs = {}
        elseif line:sub(1, 3) == '---' and
                not line:match('^%-%-%-@class%s') then
            table.insert(docs, line)
        end
    end
end

local function emit_type(statement, declared, body)
    local name = statement:match('^local%s+([%a_][%w_]*)%s*=')
    if name ~= nil and not declared[name] then
        table.insert(body, 'local ' .. name .. ' = {}')
        declared[name] = true
    end
end

-- The lines of a table constructor that starts at `index`, up to the
-- closing brace, taking nested tables into account.
local function collect_table(lines, index)
    local depth = 0
    local collected = {}
    for i = index, #lines do
        table.insert(collected, lines[i])
        for char in lines[i]:gmatch('[{}]') do
            if char == '{' then
                depth = depth + 1
            else
                depth = depth - 1
            end
        end
        if depth == 0 then
            return collected
        end
    end
    return nil
end

-- Identifiers used as values in a table constructor of the form
-- `return { name = value, ... }` or `local x = { name = value, ... }`,
-- or nil when the table has another shape. Only such tables can be
-- emitted as is, because the stub does not declare the names used by
-- arbitrary expressions.
local function simple_table_values(lines)
    local values = {}
    for i, line in ipairs(lines) do
        if i == 1 then
            line = line:gsub('^[^{]*{', '')
        end
        if i == #lines then
            line = line:gsub('}%s*$', '')
        end
        line = strip(line)
        if line ~= '' and line:sub(1, 2) ~= '--' then
            local value =
                line:match('^[%a_][%w_]*%s*=%s*([%a_][%w_]*)%s*,?$')
            if value == nil then
                return nil
            end
            table.insert(values, value)
        end
    end
    return values
end

-- Emit a table constructor that holds the module value under `name`.
-- The members are kept when all the values they reference are declared
-- in the stub, so that emmylua_doc_cli, which builds the module page
-- from the type of the module value, lists them. Otherwise fall back to
-- an empty table. Return whether the members were emitted.
local function emit_table(lines, index, declared, names, body, name)
    local table_lines = collect_table(lines, index)
    local values = table_lines ~= nil and
        simple_table_values(table_lines) or nil
    if values ~= nil then
        for _, value in ipairs(values) do
            if not declared[value] and not names[value] then
                values = nil
                break
            end
        end
    end
    if values ~= nil then
        for _, line in ipairs(table_lines) do
            table.insert(body, line)
        end
        declared[name] = true
        return true
    end
    table.insert(body, 'local ' .. name .. ' = {}')
    declared[name] = true
    return false
end

-- Emit a `---@type` annotated declaration. For a module value, that is
-- a top-level `return { ... }`, the table is emitted as is. An assignment
-- to a table field is emitted as a field declaration. Return whether the
-- module value was emitted as is.
local function emit_value(
    lines, index, statement, declared, names, roots, order, body,
    return_name)
    local name = statement:match('^local%s+([%a_][%w_]*)%s*=%s*{')
    if name ~= nil then
        if not declared[name] then
            table.insert(body, 'local ' .. name .. ' = {}')
            declared[name] = true
        end
        return false
    end
    local target = statement:match(
        '^([%a_][%w_]*%.[%w_%.]*)%s*=')
    if target ~= nil then
        local root = target:match('^([%a_][%w_]*)')
        if root ~= nil and not declared[root] and not roots[root] then
            roots[root] = true
            table.insert(order, root)
        end
        table.insert(body, target .. ' = nil')
        return false
    end
    if return_name == nil or not statement:match('^return%s*{') then
        return false
    end
    if declared[return_name] then
        return false
    end
    return emit_table(lines, index, declared, names, body, return_name)
end

local function emit_function(lines, index, statement, names, body)
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
    names[name] = true
end

local function generate(out_file, files, return_name)
    local body = {}
    local declared = {}
    local names = {}
    local roots = {}
    local return_emitted = false
    local return_lines = nil
    local module_class_lines = nil
    local module_doc_lines = nil

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
                local has_value = false
                local has_function = false
                local visible = true
                for _, name in ipairs(directives) do
                    if not CODE_ONLY[name] then
                        code_only = false
                    end
                    if TYPE_TAGS[name] then
                        has_type = true
                    end
                    if VALUE_TAGS[name] then
                        has_value = true
                    end
                    if FUNCTION_TAGS[name] then
                        has_function = true
                    end
                    if NON_PUBLIC[name] then
                        visible = false
                    end
                end

                local statement, statement_index = find_statement(lines, j)
                if has_dir and not code_only then
                    local module_return =
                        return_name ~= nil and statement ~= nil and
                        statement:match('^return%s+' .. return_name ..
                                        '%s*$') ~= nil
                    local module_value =
                        return_name ~= nil and statement ~= nil and
                        statement:match('^local%s+' .. return_name ..
                                        '%s*=%s*') ~= nil
                    local value_block = has_value and statement ~= nil and
                        (statement:match('^local%s+[%a_][%w_]*%s*=%s*{') ~=
                            nil or
                         statement:match('^return%s*{') ~= nil)
                    local skip_value = has_value and not module_return and
                        not value_block
                    if module_value and skip_value then
                        module_doc_lines = {}
                        for k = i, j - 1 do
                            if directive_name(lines[k]) == nil then
                                table.insert(module_doc_lines, lines[k])
                            end
                        end
                    end
                    local class_name =
                        lines[i]:match('^%-%-%-@class%s+([%w_%.]+)')
                    if class_name ~= nil and class_name == return_name then
                        module_class_lines = {}
                        for k = i, j - 1 do
                            table.insert(module_class_lines, lines[k])
                        end
                    end
                    if visible and statement ~= nil and has_function then
                        declare_root(statement, declared, roots, body,
                                     return_name, module_doc_lines)
                    end
                    if module_return then
                        return_lines = {}
                        for k = i, j - 1 do
                            table.insert(return_lines, lines[k])
                        end
                    elseif not skip_value then
                        for k = i, j - 1 do
                            table.insert(body, lines[k])
                        end
                    end

                    if visible and statement ~= nil and not module_return and
                            not skip_value then
                        if has_type then
                            emit_type(statement, declared, body)
                        elseif has_function then
                            emit_function(lines, statement_index, statement,
                                          names, body)
                        elseif has_value then
                            return_emitted = emit_value(
                                lines, statement_index, statement,
                                declared, names, roots, order, body,
                                return_name)
                        end
                    end

                    table.insert(body, '')
                elseif not has_dir and return_name ~= nil and
                        statement ~= nil and
                        statement:match('^local%s+' .. return_name ..
                                        '%s*=%s*{') then
                    -- A prose-only block above the module value is a
                    -- part of its documentation.
                    for k = i, j - 1 do
                        table.insert(body, lines[k])
                    end
                    emit_table(lines, statement_index, declared, names,
                               body, return_name)
                    table.insert(body, '')
                end

                i = j
            end
        end
    end

    if module_class_lines ~= nil then
        emit_class_functions(module_class_lines, return_name, names, declared,
                             roots, body, module_doc_lines)
    end
    if return_lines ~= nil then
        if body[#body] ~= '' then
            table.insert(body, '')
        end
        for _, line in ipairs(return_lines) do
            table.insert(body, line)
        end
        table.insert(body, '')
    end

    if return_name ~= nil and not return_emitted and
            not declared[return_name] and not roots[return_name] then
        fail("return name '%s' is not declared in the stub", return_name)
    end

    local out = {
        '---@meta',
        '-- Auto-generated by tools/gen-emmylua-stubs.lua. Do not edit.',
    }
    table.insert(out, '')
    for _, line in ipairs(body) do
        table.insert(out, line)
    end
    if return_name ~= nil and not return_emitted then
        if out[#out] ~= '' then
            table.insert(out, '')
        end
        table.insert(out, 'return ' .. return_name)
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
generate(args.out, args.files, args.return_name)
