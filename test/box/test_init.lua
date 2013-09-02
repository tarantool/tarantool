-- testing start-up script
floor = require("math").floor

--
-- Access to box.cfg from start-up script
--

box_cfg = box.cfg()

function print_config()
	return box_cfg
end


--
-- Test for bug #977898
-- Insert from detached fiber
--

local function do_insert()
    box.fiber.detach()
    box.insert(0, 1, 2, 4, 8)
end

box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')


fiber = box.fiber.create(do_insert)
box.fiber.resume(fiber)

--
-- Test insert from start-up script
--

box.insert(0, 2, 4, 8, 16)
