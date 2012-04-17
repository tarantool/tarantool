-- testing start-up script

--
-- Access to box.cfg from start-up script
--

box_cfg = {}
function copy_box_cfg()
    for i, v in pairs(box.cfg) do
        box_cfg[i] = v
    end
end

function print_config()
    for i, v in pairs(box_cfg) do
        print(i, " = ", v)
    end
end

copy_box_cfg()

--
-- Test for bug #977898
-- Insert from detached field
--

local function do_insert()
    box.fiber.detach()
    box.insert(0, 1, 2, 4, 8)
end

fiber = box.fiber.create(do_insert)
box.fiber.resume(fiber)

--
-- Test insert from start-up script
--

box.insert(0, 2, 4, 8, 16)
