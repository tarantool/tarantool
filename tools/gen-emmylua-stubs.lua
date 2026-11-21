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
-- one of the extracted annotation blocks.
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
-- annotations (scalars, locals without a table literal) are emitted as
-- comments only.
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

-- Split a field text into the argument list, return type and description.
-- The text starts with 'fun('.
local function parse_fun_field(text)
    local rest = text:match('^fun%s*%((.*)$')
    if rest == nil then
        return nil
    end
    local args_end = find_closing_paren(rest)
    if args_end == nil then
        return nil
    end
    local args = rest:sub(1, args_end - 1)
    local after = rest:sub(args_end + 1)
    local ret
    local desc
    local tail = after:match('^%s*:%s*(.*)$')
    if tail ~= nil then
        local depth2 = 0
        local cut = #tail + 1
        for i = 1, #tail do
            local char = tail:sub(i, i)
            if char == '<' or char == '{' or char == '(' or
                    char == '[' then
                depth2 = depth2 + 1
            elseif char == '>' or char == '}' or char == ')' or
                    char == ']' then
                depth2 = depth2 - 1
            elseif (char == ' ' or char == '\t') and depth2 == 0 then
                cut = i
                break
            end
        end
        ret = tail:sub(1, cut - 1)
        desc = tail:sub(cut):gsub('^%s+', '')
    else
        desc = after:gsub('^%s+', '')
    end
    return args, ret, desc
end

-- Group the lines of a class block into fields. Keep the first line
-- and the continuation lines separately, so that a description with
-- code blocks can be emitted line by line.
local function parse_class_fields(lines)
    local fields = {}
    local current = nil
    local function flush()
        if current ~= nil then
            table.insert(fields, current)
            current = nil
        end
    end
    for _, line in ipairs(lines) do
        local name, rest = line:match('^%-%-%-@field%s+([%w_]+)%s+(.*)$')
        if name ~= nil then
            flush()
            rest = strip(rest)
            current = {name = name, text = rest, first = rest, cont = {}}
        elseif line:sub(1, 4) == '---@' then
            -- A new directive ends the description of the field.
            flush()
        elseif line:sub(1, 3) ~= '---' then
            flush()
        elseif current ~= nil then
            -- Drop the standard continuation indent, but keep deeper
            -- indentation used inside code blocks.
            local cont = line:sub(4):gsub('^    ', '')
            if cont ~= '' then
                current.text = current.text .. ' ' .. cont
            end
            table.insert(current.cont, cont)
        end
    end
    flush()
    return fields
end

-- Emit function fields of the module class as module functions. The
-- class itself stays for the type page, while the module page gets
-- all the functions.
local function emit_class_functions(lines, module_name, names, body)
    for _, field in ipairs(parse_class_fields(lines)) do
        local args, ret, desc = parse_fun_field(field.text)
        local desc_lines = nil
        -- When the signature ends on the first line, keep the rest of
        -- the description as separate lines so that markdown code
        -- blocks are not merged into a single line.
        local first_args, first_ret, first_desc =
            parse_fun_field(field.first)
        if first_args ~= nil then
            args = first_args
            ret = first_ret
            desc_lines = {}
            if first_desc ~= nil and first_desc ~= '' then
                table.insert(desc_lines, first_desc)
            end
            for _, line in ipairs(field.cont) do
                table.insert(desc_lines, line)
            end
        end
        local full_name = module_name .. '.' .. field.name
        if args ~= nil and names[full_name] == nil then
            if desc_lines ~= nil then
                for _, line in ipairs(desc_lines) do
                    table.insert(body, '---' .. line)
                end
            elseif desc ~= nil and desc ~= '' then
                table.insert(body, '---' .. desc)
            end
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
            if ret ~= nil and ret ~= '' then
                for _, result in ipairs(split_top_level(ret)) do
                    if result ~= '' then
                        table.insert(body, '---@return ' .. result)
                    end
                end
            end
            table.insert(body, 'function ' .. full_name .. '(' ..
                         table.concat(arguments, ', ') .. ') end')
            table.insert(body, '')
            names[full_name] = true
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

local function emit_function(
    lines, index, statement, declared, names, roots, order, body)
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

    if prefix ~= 'local function' then
        local root = name:match('^([%w_]+)[%.:]')
        if root ~= nil and not declared[root] and not roots[root] then
            roots[root] = true
            table.insert(order, root)
        end
    end
end

local function generate(out_file, files, return_name)
    local body = {}
    local declared = {}
    local names = {}
    local roots = {}
    local order = {}
    local return_emitted = false
    local module_class_lines = nil
    local module_docs = nil
    local module_type_index = nil

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
                    local class_name = nil
                    for k = i, j - 1 do
                        class_name = lines[k]:match(
                            '^%-%-%-@class%s+([%w_%.]+)')
                        if class_name ~= nil then
                            local class_docs = {}
                            for m = i, k - 1 do
                                table.insert(class_docs, lines[m])
                            end
                            if class_name == return_name then
                                module_class_lines = {}
                                module_type_index = nil
                                for m = i, j - 1 do
                                    table.insert(module_class_lines, lines[m])
                                    local typ = lines[m]:match(
                                        '^%-%-%-@type%s+([%w_%.]+)%s*$')
                                    if typ == return_name then
                                        module_type_index = m
                                    end
                                end
                                module_docs = class_docs
                                if not declared[return_name] and
                                        not roots[return_name] then
                                    roots[return_name] = true
                                    table.insert(order, return_name)
                                    declared[return_name] = true
                                end
                            end
                            break
                        end
                    end
                    local body_start = i
                    if class_name == return_name then
                        for m = i, j - 1 do
                            if lines[m]:match('^%-%-%-@class%s') then
                                body_start = m
                                break
                            end
                        end
                    end
                    local is_module_value =
                        return_name ~= nil and
                        class_name == return_name and
                        statement ~= nil and
                        statement:match('^local%s+' .. return_name ..
                                        '%s*=') ~= nil
                    if visible and is_module_value then
                        emit_class_functions(module_class_lines,
                                             return_name, names, body)
                    end
                    local skip_index = nil
                    if is_module_value then
                        -- The module value type is only needed in the source.
                        -- Keeping it in the stub would make the module page
                        -- resolve to the class instead of the emitted
                        -- functions.
                        skip_index = module_type_index
                    end
                    for k = body_start, j - 1 do
                        if k ~= skip_index then
                            table.insert(body, lines[k])
                        end
                    end

                    if visible and statement ~= nil then
                        if has_type then
                            emit_type(statement, declared, body)
                        elseif has_function then
                            emit_function(lines, statement_index, statement,
                                          declared, names, roots, order, body)
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

    if return_name ~= nil and not return_emitted and
            not declared[return_name] and not roots[return_name] then
        fail("return name '%s' is not declared in the stub", return_name)
    end

    local out = {
        '---@meta',
        '-- Auto-generated by tools/gen-emmylua-stubs.lua. Do not edit.',
    }
    if #order > 0 then
        table.insert(out, '')
        for _, root in ipairs(order) do
            if root == return_name and module_docs ~= nil then
                for _, line in ipairs(module_docs) do
                    table.insert(out, line)
                end
            end
            table.insert(out, 'local ' .. root .. ' = {}')
        end
    end
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
