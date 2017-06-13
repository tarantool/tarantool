-- Get next token from the SQL query at the specified offset.
-- Context.offset is propagated on count of read characters.
-- @param context Table with two keys: sql and offset. sql - 
--        query string, offset - start position, from which need
--        to extract a next token.
--
-- @retval Token. Comments are extracted as a monolite token, for
--         example '/* select 1, 2, 3 */' is returned as a single
--         token, but 'select 1, 2, 3' is returned as 'select',
--         '1', '2', '3'.
local function get_next_token(context)
    local chars_read = 0
    -- Accumulate here the token char by char.
    local token = ''
    -- True, if now the tokenizer reads the comment.
    local in_comment = false
    -- Type of the read comment: '-- ... \n' or  '/* ... */'.
    local comment_type = nil
    -- Iterate until token is ready.
    for i = context.offset, context.sql:len() do
        local c = context.sql:sub(i, i)
        local next_c = context.sql:sub(i + 1, i + 1)
        if in_comment then
            -- Comment '-- ... \n' ends with '\n'.
            -- Comment '/* .. */' ends with the '*/'.
            if comment_type == '-' and c == '\n' then
                chars_read = chars_read + 1
                token = token..c
                goto finish
            elseif comment_type == '*' and c == '*' and next_c == '/' then
                chars_read = chars_read + 2
                token = token..'*/'
                goto finish
            end
            -- Accumulate commented text.
            token = token..c
            chars_read = chars_read + 1
        elseif c == '-' and next_c == '-' then
            assert(token:len() == 0)
            in_comment = true
            comment_type = '-'
            token = token..c
            chars_read = chars_read + 1
        elseif c == '/' and next_c == '*' then
            assert(token:len() == 0)
            in_comment = true
            comment_type = '*'
            token = token..c
            chars_read = chars_read + 1
        elseif c == ' ' or c == '\n' or c == '\t' then
            chars_read = chars_read + 1
            if token:len() ~= 0 then
                goto finish
            end
        elseif not c:match('[%d%a]') then
            if token:len() == 0 then
                token = c
                chars_read = chars_read + 1
            end
            goto finish
        else
            token = token..c
            chars_read = chars_read + 1
        end
    end
::finish::
    context.offset = context.offset + chars_read
    return token
end

local function split_sql(query)
    -- Array of result statements
    local res = {}
    -- True, if the splitter reads the trigger body. In such a
    -- case the ';' can not be used as end of the statement.
    local in_trigger = false
    -- True, if the splitter reads the string in the query.
    -- Inside a string all chars lose their special meanings.
    local in_quotes = false
    -- Type of the quotes - either ' or ".
    local quote_type = nil
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
    local context = { sql = query, offset = 1 }
    local token = get_next_token(context)
    -- Read until multistatement query is finished.
    while token:len() ~= 0 do
        if token == '"' or token == "'" then
            if in_quotes and token == quote_type then
                in_quotes = false
            elseif not in_quotes then
                in_quotes = true
                quote_type = token
            end
        elseif not in_quotes and not in_trigger and token == ';' then
            table.insert(res, query:sub(prev_sub_i, context.offset - 1))
            prev_sub_i = context.offset
        elseif token:upper() == 'CREATE' then
            token = get_next_token(context)
            -- 'TEMP' can be a part of 'CREATE TRIGGER' or
            -- 'CREATE TABLE' or 'CREATE VIEW'. Skip it.
            if token:upper() == 'TEMP' then
                token = get_next_token(context)
            end
            if token:upper() == 'TRIGGER' then
                in_trigger = true
            end
        elseif token:upper() == 'CASE' then
            in_case = true
        elseif token:upper() == 'END' then
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
    end
    if prev_sub_i < context.offset then
        table.insert(res, query:sub(prev_sub_i, context.offset))
    end
    return res
end

return {split_sql = split_sql}
