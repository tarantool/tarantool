box.once()
box.once("key")
box.once("key", "key")
box.once("key", nil)
box.once("key", function() end)


function f(arg) if i ~= nil then i = i + arg else i = arg end end
box.once("test", f, 1)
i
box.once("test", f, 1)
i
