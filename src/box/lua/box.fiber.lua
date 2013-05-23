--
-- create and start new detached fiber
--
function box.fiber.wrap(foo, ...)
    if type(foo) ~= 'function' then
        error "box.fiber.wrap: first argument must be function"
    end

    local args = { ... }

    local f = box.fiber.create(
        function()
            box.fiber.detach()
            foo(unpack(args))
        end
    )

    box.fiber.resume(f)

    return f
end
