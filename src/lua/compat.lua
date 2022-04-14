-- compat.lua -- internal file

local json = require('json')

local options = {
	json_escape_forward_slash = {
		old = true,
		new = false,
		default = true,
		brief = "<...>",
		doc  = "https://github.com/tarantool/tarantool/wiki/compat_json_escape_forward_slash"
	},
	option_2 = {
		old = false,
		new = true,
		default = true,
		brief = "<...>",
		doc  = "https://github.com/tarantool/tarantool/wiki/option_2"
	}
}

local postaction = {
	json_escape_forward_slash = function(value)
			json.cfg{encode_esc_slash = value}
		end,
	option_2 = function (value)
			print("option_2 postaction was called!")
		end
}

local help = [[
This is Tarantool compatibility module.
To get help, see the Tarantool manual at https://tarantool.io/en/doc/
Available commands:
	
	candidates()                -- list all unselected options
	dump()                      -- get command that sets up compat with same options as current
	help()                      -- show this help
	reset()                     -- set all options to default
	restore{'option_name'}      -- set to default specified options
	{option_name = true}        -- set list of options to desired values, could be true, false, 'old', 'new', 'default'
	option_name                 -- list option info
	option_name = true          -- set desired value to option, could be true, false, 'old', 'new', 'default'
]]

local cfg = { }

local function serialize_policy(key, policy)
	assert(policy ~= nil)
	local result = { }
	for f in pairs(policy) do
		result[f] = policy[f]
	end
	result.value = cfg[key].value
	return result
end

local function serialize_compat(compat)
	local result = { }
	for key, val in pairs(options) do
		result[key] = serialize_policy(key, val)
	end
	return result
end

local function set_option(key, val)
	if not options[key] then
		error(('Invalid option %s'):format(key))
	end
	if val == 'new' then
		val = options[key].new
	end
	if val == 'old' then
		val = options[key].old
	end
	if val == 'default' then							-- should 'default' set option as `selected`?
		val = options[key].default
	end
	if type(val) ~= 'boolean' then
		error(('Invalid argument %s'):format(val))
	end
	cfg[key].value = val
	cfg[key].selected = true
	postaction[key](val)
end

compat = setmetatable({
			candidates = function()
				local result = { }
				for key, val in pairs(options) do
					if not cfg[key].selected then
						result[key] = serialize_policy(key, val)
					end
				end
				return result
			end,
			dump = function()
				local result = "require('tarantool').compat({"
				local isFirst = true
				for key, val in pairs(options) do
					if cfg[key].selected then
						if not isFirst then
							result = result .. ", "
						end
						result = result .. key ..  " = " .. tostring(cfg[key].value)
						isFirst = false
					end
				end
				return result .. "})"
			end,
			reset = function()
				for name, elem in pairs(options) do
 					cfg[name].value = elem.default
					cfg[name].selected = false
				end
			end,
			restore = function(list)								-- it receives only {'option_1', 'option_2'} lists
				if type(list) ~= 'table' then
					error(('Invalid argument %s'):format(list))
				end
				for i, key in pairs(list) do
					if not options[key] then
						error(('Invalid option %s'):format(key))
					end
					cfg[key].value = options[key].default
					cfg[key].selected = false
					postaction[key](cfg[key].value)
				end
			end,
			preload = function()
				for name, elem in pairs(options) do
					cfg[name] = { }
 					cfg[name].value = elem.default
					cfg[name].selected = false
				end
			end,
			postload = function()
				for name, elem in pairs(options) do
					postaction[name](elem.default)
				end
			end,
			help = function()
				print()												-- not sure about this
				print(help)
			end
		}, {
			__call = function(compat, list)
				if type(list) ~= 'table' then
					error(('Invalid argument %s'):format(list))
				end
				for key, val in pairs(list) do
					set_option(key, val)
				end
			end,
			__newindex = function(compat, key, val)					-- setters should be 'old'/'new' or true/false ?
				set_option(key, val)
			end,
			__index = function(compat, key)
				local policy = options[key]
				if not policy then
					error(('Invalid option %s'):format(key))
				end
				return serialize_policy(key, policy);
			end,
			__serialize = serialize_compat,
			__tostring = serialize_compat,
			__autocomplete = function(self)
				local res = { }
				local i = 1
				for name, elem in pairs(options) do
					res[name] = true
					i = i + 1
				end
				return res
			end
		}
	)

compat.preload()
compat.preload = nil
compat.postload()				-- should postload be called from within init.c?
compat.postload = nil
								-- "A log warning at start for old values. Possibly also CLI / UI warnings." ?
return compat
