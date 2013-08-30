-- testing start-up script
floor = require("math").floor

box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')

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
	t = {}
    for i, v in pairs(box_cfg) do
        table.insert(t, tostring(i).." = "..tostring(v))
    end
	return t
end

copy_box_cfg()

--
-- Test for bug #977898
-- Insert from detached fiber
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
