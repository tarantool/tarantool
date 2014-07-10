help = {}
help[1] = {}
help[1]["Help topics"] = { "Tutorial", "Basics", "Administration" }
help[2] = "To get help on a topic, type help('topic') (with quotes)"
help[3] = "To get help on a function/object, type help(function) (without quotes)"

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


