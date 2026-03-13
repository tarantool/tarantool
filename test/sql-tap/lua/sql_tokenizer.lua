local TK_CREATE = 1
local TK_TEMP = 2
local TK_TRIGGER = 3
local TK_CASE = 4
local TK_END = 5
local TK_SEMI = 6
local TK_EMPTY = 7
local TK_TEXT = 8

-- Read comment of the specified type and propagate the offset.
-- @param context Map {sql: string, offset: number}.
-- @param comment_type Either '-' for '-- ...' comments or
--        '*' for '/* ... */' comments.
local function read_comment(context, comment_type)
    -- Skip '/*' or '--'.
    local chars_read = 2
    for i = context.offset + 2, context.sql:len() do
        local c = context.sql:sub(i, i)
        local next_c = context.sql:sub(i + 1, i + 1)
        chars_read = chars_read + 1
        if comment_type == '-' and c == '\n' then
            goto finish
        elseif comment_type == '*' and c == '*' and next_c == '/' then
            chars_read = chars_read + 1
            goto finish
        end
    end
::finish::
    context.offset = context.offset + chars_read
end

-- Read spaces and propagate the offset.
-- @param context Map {sql: string, offset: number}.
local function read_spaces(context)
    -- Skip the first space.
    local chars_read = 1
    for i = context.offset + 1, context.sql:len() do
        local c = context.sql:sub(i, i)
        if c ~= ' ' and c ~= '\n' and c ~= '\t' then
            goto finish
        end
        chars_read = chars_read + 1
    end
::finish::
    context.offset = context.offset + chars_read
end

-- Read a text. It can be an identifier consisted of letters and
-- digits or another single symbol (,.:;^&?! etc). Propagate the
-- offset.
-- @param context Map {sql: string, offset: number}.
--
-- Retvals identify words without case sensitivity.
-- @retval not TK_TEXT means word TK_'word'.
-- @retval     TK_TEXT means another, not special word.
local function read_text(context)
    local chars_read = 0
    local tk_type = TK_TEXT
    for i = context.offset, context.sql:len() do
        local c = context.sql:sub(i, i)
        if not c:match('[%d%a_]') then
            if chars_read == 0 then
                -- Read nontext symbol only if it is the first
                -- symbol in the token. It is needed to avoid
                -- mixing of SQL special words
                -- (CREATE, TRIGGER, ...) with nontext symbols.
                chars_read = 1
                if c == ';' then
                    tk_type = TK_SEMI
                end
                assert(c ~= '"' and c ~= "'")
            end
            goto finish
        end
        chars_read = chars_read + 1
    end
::finish::
    local new_offset = context.offset + chars_read
    local end_pos = new_offset - 1
    if chars_read == 6 and context.sql:sub(context.offset, end_pos):upper() == 'CREATE' then
        tk_type = TK_CREATE
    elseif chars_read == 4 then
        local word = context.sql:sub(context.offset, end_pos):upper()
        if word == 'TEMP' then
            tk_type = TK_TEMP
        elseif word == 'CASE' then
            tk_type = TK_CASE
        end
    elseif chars_read == 7 and context.sql:sub(context.offset, end_pos):upper() == 'TRIGGER' then
        tk_type = TK_TRIGGER
    elseif chars_read == 3 and context.sql:sub(context.offset, end_pos):upper() == 'END' then
        tk_type = TK_END
    end
    context.offset = new_offset
    return tk_type
end

-- Read a string in '' or "" quotes. Inside a string all special
-- words and symbols lose their special meanings. Single quote '
-- can be escaped using double-single quote: ''. For example:
-- 'Example of the ''string'' with escape'. Double quote can not
-- be escaped.
-- @param context Map {sql: string, offset: number}.
local function read_string(context)
    local i = context.offset
    local quote = context.sql:sub(i, i)
    i = i + 1
    while i <= context.sql:len() do
        local c = context.sql:sub(i, i)
        i = i + 1
        if c == quote then
            if quote == "'" then
                local next_c = context.sql:sub(i, i)
                if next_c ~= c then
                    goto finish
                else
                    i = i + 1
                end
            else
                goto finish
            end
        end
    end
::finish::
    context.offset = i
end

-- Get next token from the SQL request at the specified offset.
-- Context.offset is propagated on count of read characters.
-- @param context Table with two keys: sql and offset. sql -
--        request string, offset - start position, from which need
--        to extract a next token.
--
-- @retval Token type. If the rest of the SQL request consists of
--         spaces and comments, then return TK_EMPTY.
local function get_next_token(context)
    local c
    repeat
        local i = context.offset
        c = context.sql:sub(i, i)
        local next_c = context.sql:sub(i + 1, i + 1)
        if (c == '-' and next_c == '-') or (c == '/' and next_c == '*') then
            read_comment(context, next_c)
        elseif c == ' ' or c == '\n' or c == '\t' then
            read_spaces(context)
        elseif c == "'" or c == '"' then
            read_string(context, c)
        elseif c ~= '' then
            return read_text(context)
        end
    until c == ''
    return TK_EMPTY
end

local function split_sql(request)
    -- Array of result statements
    local res = {}
    -- True, if the splitter reads the trigger body. In such a
    -- case the ';' can not be used as end of the statement.
    local in_trigger = false
    -- True, if the splitter reads the 'CASE WHEN ... END'
    -- statement. It is a special case, because 'END' is used
    -- to determine both end of 'CASE' and end of
    -- 'CREATE TRIGGER'. And 'CASE' can be inside trigger body.
    -- Tracking of 'CASE ... END' helps to determine true borders
    -- of 'CREATE TRIGGER' statement.
    local in_case = false
    -- End of the previous statement.
    local prev_sub_i = 1
    -- Tokenizer context.
    local context = { sql = request, offset = 1 }
    local token = get_next_token(context)
    local is_not_empty = token ~= TK_EMPTY and token ~= TK_SEMI
    -- Read until multistatement request is finished.
    while token ~= TK_EMPTY do
        if not in_trigger and token == TK_SEMI then
            if is_not_empty then
                table.insert(res, request:sub(prev_sub_i, context.offset - 1))
            end
            is_not_empty = false
            prev_sub_i = context.offset
        elseif token == TK_CREATE then
            token = get_next_token(context)
            is_not_empty = is_not_empty or token ~= TK_EMPTY and token ~= TK_SEMI
            -- 'TEMP' can be a part of 'CREATE TRIGGER' or
            -- 'CREATE TABLE' or 'CREATE VIEW'. Skip it.
            if token == TK_TEMP then
                token = get_next_token(context)
                is_not_empty = is_not_empty or token ~= TK_EMPTY and token ~= TK_SEMI
            end
            if token == TK_TRIGGER then
                in_trigger = true
            end
        elseif token == TK_CASE then
            in_case = true
        elseif token == TK_END then
            -- 'TRIGGER' can contain 'CASE', but not vice versa.
            -- In a case: CREATE TRIGGER ... BEGIN
            --                   SELECT ... CASE ... END;
            --            END;
            -- At first close CASE and then close TRIGGER.
            if in_case then
                in_case = false
            elseif in_trigger then
                in_trigger = false
            end
        end
        token = get_next_token(context)
        is_not_empty = is_not_empty or token ~= TK_EMPTY and token ~= TK_SEMI
    end
    if prev_sub_i < context.offset and is_not_empty then
        table.insert(res, request:sub(prev_sub_i, context.offset))
    end
    return res
end

return {split_sql = split_sql}
