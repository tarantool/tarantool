
function test_conflict()
    local test_run = require('test_run')
    local inspector = test_run.new()
    local engine = inspector:get_cfg('engine')

    local s = box.schema.space.create('tester', {engine=engine});
    local i = s:create_index('test_index', {type = 'tree', parts = {1, 'STR'}});

    local commits = 0
    local function conflict()
        box.begin()
	s:replace({'test'})
	box.commit()
	commits = commits + 1
    end;

    local fiber = require('fiber');
    local f0 = fiber.create(conflict);
    local f1 = fiber.create(conflict); -- conflict
    fiber.sleep(0);

    s:drop();
    return commits
end
