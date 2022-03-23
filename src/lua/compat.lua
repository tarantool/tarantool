compat = {}

function compat.help()
    print("This is a very usefull help msg.")
end

function compat.new(ctx)
	local c = compat
	c.ctx = ctx
	return c
end

-- Idea: use metatable for indexation skipping service funcs
-- Idea: have an options table inside compat for handy iteration
-- Idea: display help/list options via __to_string overload
-- Question: can we protect some fields?

json_escape_forward_slash = {}
json_escape_forward_slash.old = false
json_escape_forward_slash.new = true
json_escape_forward_slash.default = json_escape_forward_slash.old		-- TODO think if it is rational to add a toggle in func or just edit src when necessary
json_escape_forward_slash.current = json_escape_forward_slash.default
json_escape_forward_slash.manually_selected = false
json_escape_forward_slash.brief = "<...>"
json_escape_forward_slash.name = "json_escape_forward_slash"
json_escape_forward_slash.doc  = "https://github.com/tarantool/tarantool/wiki/compat_json_escape_forward_slash"


function compat.internal_set(self, val)
	self.manually_selected = true
	self.current = val
end

function compat.internal_get(self)
	return self.current
end

function compat.internal_list(self)		-- There are two options, we can leave listing to correspond to every option's specifics or generalize in the compat module (not sure how to do it yet)
	print(self.name)
	print("", "current:", self.current)		-- TODO align?
	print("", "old:", self.old)
	print("", "new:", self.new)
	print("", "default:", self.default)
	print("", "brief:", self.brief)
	print("", "doc:", self.doc)
end

-- For getters, setters and listing there is a generic option:
--
-- 		compat.list(option)
-- 		compat.set(option, val)
--
-- but it is questionably more usable than the current
--
--
-- UPD: maybe we can also combine them:
--
-- 		function option.list(self)
--			compat.list(self)				-- where do we get compat here?
--		end									-- should we store it in every option?
--


option_2 = {}
option_2.old = true
option_2.new = false
option_2.default = json_escape_forward_slash.new
option_2.current = json_escape_forward_slash.default
option_2.manually_selected = false
option_2.brief = "<...>"
option_2.name = "option_2"								-- maybe excessive
option_2.doc  = "https://github.com/tarantool/tarantool/wiki/option_2"


local options = {
	json_escape_forward_slash,
	option_2
}


for i,elem in pairs(options) do
	compat.elem = elem
	compat.elem.compat = compat
	compat.elem.list = compat.internal_list
	compat.elem.set = compat.set
	compat.elem.get = compat.get
end


--=====================
--== TESTING_SECTION ==
--=====================

ctx = {online = true}

c = compat.new(ctx)

print(c.ctx.online)

-- is it a good idea to pass context like this?

print()
json_escape_forward_slash:list()
print()
option_2:list()
print()
print()
print()

for elem in pairs(c) do					-- This is how I wanted to implement compat.candidates() but for whatever reason it is not working...
	print(elem, elem.list)
	if (elem.list ~= nil) then
		elem:list()
	end
end


return compat			-- Lua book says it's a good practice
