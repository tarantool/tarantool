
function test_conflict()
	s = box.schema.space.create('tester', {engine='sophia'});
	i = s:create_index('sophia_index', {type = 'tree', parts = {1, 'STR'}});

	commits = 0
	function conflict()
		box.begin()
		s:replace({'test'})
		box.commit()
		commits = commits + 1
	end;

	fiber = require('fiber');
	f0 = fiber.create(conflict);
	f1 = fiber.create(conflict); -- conflict
	fiber.sleep(0);

	s:drop();
	sophia_schedule();
	return commits
end
