local doc = require('help.en_US')

help = {}
help[1] = {}
help[1]["Help topics"] = { "Tutorial", "Basics", "Administration" }
help[2] = "To get help on a topic, type help('topic') (with quotes)"
help[3] = "To get help on a function/object, type help(function) (without quotes)"
help[4] = "To start tutorial, type tutorial()"

tutorial = {}
tutorial[1] = help[4]

local help_function_data = {};
help_function_data["Administration"] = {}
help_function_data["Administration"]["Server administrative commands"] =
{		"box.snapshot()",
		"box.info()",
		"box.stat()",
		"box.slab.info()",
		"box.slab.check()",
		"box.fiber.info()",
		"box.plugin.info()",
		"box.cfg()",
		"box.coredump()"
}
help_function_data["Basics"] = "First thing to be done before any database object can be created, is calling box.cfg() to configure and bootstrap the database instance. Once this is done, define database objects using box.schema, for example type box.schema.space.create('test') to create space 'test'. To add an index on a space, type box.space.test:create_index(). With an index, the space can accept tuples. Insert a tuple with box.space.test:insert{1, 'First tuple'}"

local help_object_data = {}

local function help_call(table, param)
    if type(param) == 'string' then
        if help_function_data[param] ~= nil then
            return help_function_data[param]
        end
    end
    if type(param) == 'table' then
        if help_object_data[param] ~= nil then
            return help_object_data[param]
        end
    end
    if param ~= nil then
        return "Help object not found"
    end
    return table
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
    tutorial= tutorial;
}
