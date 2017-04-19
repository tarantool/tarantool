local doc = require('help.en_US')

help = { doc.help }
tutorial = {}
tutorial[1] = help[1]

local help_function_data = {};
local help_object_data = {}

local function help_call(table, param)
    return help
end

setmetatable(help, { __call = help_call })

local screen_id = 1;

local function tutorial_call(table, action)
    if action == 'start' then
        screen_id = 1;
    elseif action == 'next' or action == 'more' then
        screen_id = screen_id + 1
    elseif action == 'prev' then
        screen_id = screen_id - 1
    elseif type(action) == 'number' and action % 1 == 0 then
        screen_id = tonumber(action)
    elseif action ~= nil then
        error('Usage: tutorial("start" | "next" | "prev" | 1 .. '..
            #doc.tutorial..')')
    end
    if screen_id < 1 then
        screen_id = 1
    elseif screen_id > #doc.tutorial then
        screen_id = #doc.tutorial
    end
    return doc.tutorial[screen_id]
end

setmetatable(tutorial, { __call = tutorial_call })

return {
    help = help;
    tutorial = tutorial;
}
