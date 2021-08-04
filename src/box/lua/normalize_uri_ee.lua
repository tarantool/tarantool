-- normalize_uri.lua - internal file

local function trim(s)
   s = s:gsub("^" .. "%s+", "")
   return s
end

local function normalize_uri(uri)
    if uri == nil or type(uri) == 'table' then
        return uri
    end
    return tostring(uri);
end

-- Remove dublicate values from table and return new table
-- without dublicates
local function remove_dublicates_from_table(table_with_dublicates)
    local table_without_dublicates = {}
    local vals = {}
    for _, val in pairs(table_with_dublicates) do
        if not vals[val] then
            table.insert(table_without_dublicates, val)
            vals[val] = true
        end
    end
    return table_without_dublicates
end

-- Check that there are only valid options in URI table
-- and there values are also valid.
local function check_uri_options(option, option_values, valid_option_values)
    for _, opt_val in pairs(option_values) do
        if not valid_option_values[opt_val] then
            box.error(box.error.CFG, 'listen', 'invalid option value \'' ..
                      opt_val .. '\' for URI \'' .. option .. '\' option')
        end
    end
end

-- Check that there are only valid options in URI table,
-- and all of them have `table` type. Also check that URI
-- is not empty and has 'string' type.
local function check_uri_in_table_format(uri_with_options)
    local valid_options = {
        ["transport"] = {
            ["plain"] = true,
        },
    }
    if not uri_with_options["uri"] then
        box.error(box.error.CFG, 'listen', "missing URI")
    elseif type(uri_with_options["uri"]) ~= 'string' and
           type(uri_with_options["uri"]) ~= 'number' then
        box.error(box.error.CFG, 'listen',
                  'URI should be one of types string, number')
    elseif uri_with_options["uri"] == "" then
        box.error(box.error.CFG, 'listen', "URI should not be empty")
    end
    uri_with_options["uri"] = normalize_uri(uri_with_options["uri"])

    for key, value in pairs(uri_with_options) do
        if key ~= "uri" then
            if not valid_options[key] then
                box.error(box.error.CFG, 'listen', 'invalid option name \'' ..
                          key .. '\' for URI')
            elseif type(value) ~= 'table' then
                box.error(box.error.CFG, 'listen', 'URI option should be a ' ..
                          'table')
            end
            check_uri_options(key, value, valid_options[key])
        end
    end
end

-- This function accepts table that contains URI with
-- or without options, which can have 'string' or 'table'
-- type. Converts all string options to tables.
local function parse_uri_with_options_table(uri_with_options_table)
    for opt, opt_vals in pairs(uri_with_options_table) do
        if opt ~= 'uri' then
            if type(opt_vals) ~= 'table' then
                opt_vals = tostring(opt_vals)
            end
            if type(opt_vals) == 'string' then
                opt_vals = opt_vals:split(';')
                for key, opt_val in pairs(opt_vals) do
                    opt_vals[key] = trim(opt_val)
                end
            end
            assert(type(opt_vals) == 'table')
            opt_vals = remove_dublicates_from_table(opt_vals)
        end
        uri_with_options_table[opt] = opt_vals
    end
    check_uri_in_table_format(uri_with_options_table)
    return uri_with_options_table
end

-- This function accepts uri options in string format and
-- return table of options, which contains tables of options
-- values, without dublicates. For example:
-- input "option1=value11;value11&option1=value11;value12&option2=value22"
-- output {
--    option1 = { value11, value12 },
--    option2 = { value22 },
-- }
local function parse_uri_options_string(options)
    local options_table = {}
    local options = options:split('&')
    for _, opt_with_vals in pairs(options) do
        local opt_split_pos = opt_with_vals:find('=')
        if not opt_split_pos then
            box.error(box.error.CFG, 'listen', 'not found value for URI ' ..
                      '\'' .. opt_with_vals .. '\' option')
        end
        local opt = opt_with_vals:sub(1, opt_split_pos - 1)
        local opt_vals = opt_with_vals:sub(opt_split_pos + 1)
        opt_vals = opt_vals:split(';')
        if not options_table[opt] then
            options_table[opt] = {}
        end
        for _, opt_val in pairs(opt_vals) do
            table.insert(options_table[opt], opt_val)
        end
    end
    for opt, _ in pairs(options_table) do
        options_table[opt] = remove_dublicates_from_table(options_table[opt])
    end
    return options_table
end

-- This function accepts string that contains one or several
-- URIs separated by commas. The function returns a table, each
-- element of which also contains a table, which contains URI
-- and its options. For example:
-- input "uri1?option1=value1, uri2?option2=value2"
-- output {
--    { ["uri"] = "uri1", ["option1"] = { "value1" } },
--    { ["uri"] = "uri2", ["option2"] = { "value2" } },
-- }
local function parse_uris_with_options_string(uris_string)
    local uris_with_options_table = {}
    local uris_with_options = uris_string:split(',')
    for _, uri_with_options in pairs(uris_with_options) do
        uri_with_options = trim(uri_with_options)
        local uri_with_options_table = {}
        local uri = uri_with_options
        local options = ""
        local uri_split_pos  = uri_with_options:find('?')
        if uri_split_pos then
            uri = uri_with_options:sub(1, uri_split_pos - 1)
            options = uri_with_options:sub(uri_split_pos + 1)
            if options == "" then
                box.error(box.error.CFG, 'listen',
                          'missing URI options after \'?\'')
            end
        end
        if uri == "" then
            box.error(box.error.CFG, 'listen', "URI should not be empty")
        end
        if options ~= "" then
            uri_with_options_table = parse_uri_options_string(options)
        end
        uri_with_options_table["uri"] = uri
        check_uri_in_table_format(uri_with_options_table)
        table.insert(uris_with_options_table, uri_with_options_table)
    end
    return uris_with_options_table
end

-- Check that there is no dublicate listen URI in result
-- URI table.
local function check_dublicates_in_uris_table(uris_table)
    local uris = {}
    for _, uri_in_table_format in pairs(uris_table) do
        local uri = uri_in_table_format["uri"]
        if uris[uri] then
            box.error(box.error.CFG, 'listen', 'dublicate listen URI')
        end
        uris[uri] = true
    end
    return uris_table
end

-- URIs can be passed in multiple ways, for example as a string, as
-- a table of strings or as a table of tables or as a combination of
-- these options. This function accepts any of the previously listed
-- input alternatives and returs table, which contain tables each of
-- which contains URIs and it's options. For example:
-- input {
--     "uri1?option1=value11;value12&option2=value2",
--     { ["uri"] = "uri2", ["option"] = "value" },
--     "uri3?option3=value3, uri4?option4=value4"
-- }
-- output {
--     {
--       ["uri"] = "uri1",
--       ["option1"] = { "value11", "value12" },
--       ["option2"] = { "value2" },
--     },
--     { ["uri"] = "uri2", ["option"] = { "value" } },
--     {
--       { ["uri"] = "uri3", ["option3"] = { "value3" } },
--       { ["uri"] = "uri4", ["option4"] = { "value4" } },
--     }
-- }
local function normalize_uris_with_options_list(input_uris)
    local uris_table = {}
    if type (input_uris) ~= 'table' then
        input_uris = normalize_uri(input_uris)
        if not input_uris then
            return nil
        end
        uris_table = parse_uris_with_options_string(input_uris)
    else
        for key, input_uri in pairs(input_uris) do
            if type(key) == 'string' then
                -- if type(key) == 'string', this means that input_uris,
                -- contains only one uri with or without options.
                input_uris = parse_uri_with_options_table(input_uris)
                table.insert(uris_table, input_uris)
                break
            elseif type(key) == 'number' then
                if type(input_uri) == 'string' or
                   type(input_uri) == 'number' then
                    input_uri = normalize_uri(input_uri)
                    local tmp_uris_table =
                        parse_uris_with_options_string(input_uri)
                    for _, uri_in_table_format in pairs(tmp_uris_table) do
                        uris_table[#uris_table + 1] = uri_in_table_format
                    end
                elseif type(input_uri) == 'table' then
                    input_uri = parse_uri_with_options_table(input_uri)
                    table.insert(uris_table, input_uri)
                else
                    box.error(box.error.CFG, 'listen',
                              'value in the URI table should be ' ..
                              'one of types string, number, table')
                end
            else
                box.error(box.error.CFG, 'listen',
                          'key in the URI table should be ' ..
                           'one of types string, number')
            end
        end
    end
    check_dublicates_in_uris_table(uris_table)
    return uris_table
end

box.internal.cfg_get_listen_type = function() return 'string, number, table' end
box.internal.cfg_get_listen = normalize_uris_with_options_list
