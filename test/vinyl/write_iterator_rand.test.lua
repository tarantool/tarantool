env = require('test_run')
test_run = env.new()

test_run:cmd("setopt delimiter ';'")

function clean_space(sp, cnt)
	for i = 1, cnt do
		sp:delete({i})
	end
	box.snapshot()
	return sp:count() == 0
end;

function check_tuples_len(sp, cnt, len)
	for i = 1, cnt do
		if not (#sp:get({i}) == len) then
			return false
		end
	end
	return true
end;

function fill_space(sp, cnt)
	local err = 'delete after upsert error'
	for i = 1, cnt do
		sp:upsert({i}, {{'!', 2, i}})
	end
	for i = 1, cnt do
		sp:delete({i})
	end
	box.snapshot()
	if not (sp:count() == 0) then
		return err
	end

	err = 'upsert after delete error'
	for i = 1, cnt do
		sp:insert({i})
	end
	for i = 1, cnt do
		sp:delete({i})
	end
	for i = 1, cnt do
		sp:upsert({i}, {{'!', 2, i}})
	end
	box.snapshot()
	if not (sp:count() == cnt) then
		return err
	end
	err = 'clean after "'..err..'" error'
	if not clean_space(sp, cnt) then
		return err
	end

	err = 'upsert before upsert error'
	for i = 1, cnt do
		sp:upsert({i}, {{'!', 2, i}})
	end
	for i = 1, cnt do
		sp:upsert({i}, {{'!', 2, i}})
	end
	box.snapshot()
	if not check_tuples_len(sp, cnt, 2) then
		return err
	end
	err = 'clean after "'..err..'" error'
	if not clean_space(sp, cnt) then
		return err
	end

	err = 'replace before upsert error'
	for i = 1, cnt do
		sp:replace({i})
	end
	for i = 1, cnt do
		sp:upsert({i}, {{'!', 2, i}})
	end
	box.snapshot()
	if not check_tuples_len(sp, cnt, 2) then
		return err
	end
	err = 'clean after "'..err..'" error'
	if not clean_space(sp, cnt) then
		return err
	end

	err = 'upsert before replace error'
	for i = 1, cnt do
		sp:upsert({i, i}, {{'!', 2, i}})
	end
	for i = 1, cnt do
		sp:replace({i})
	end
	box.snapshot()
	if not check_tuples_len(sp, cnt, 1) then
		return err
	end
	err = 'clean after "'..err..'" error'
	if not clean_space(sp, cnt) then
		return err
	end

	err = 'delete before replace error'
	for i = 1, cnt do
		sp:insert({i})
	end
	box.snapshot()
	for i = 1, cnt do
		sp:delete({i})
	end
	for i = 1, cnt do
		sp:replace({i, i})
	end
	box.snapshot()
	if not check_tuples_len(sp, cnt, 2) then
		return err
	end
	err = 'clean after "'..err..'" error'
	if not clean_space(sp, cnt) then
		return err
	end

	err = 'replace before delete error'
	for i = 1, cnt do
		sp:replace({i})
	end
	for i = 1, cnt do
		sp:delete({i})
	end
	box.snapshot()
	if not (sp:count() == 0) then
		return err
	end

	err = 'replace before replace error'
	for i = 1, cnt do
		sp:replace({i})
	end
	for i = 1, cnt do
		sp:replace({i, i})
	end
	box.snapshot()
	if not check_tuples_len(sp, cnt, 2) then
		return err
	end
	err = 'clean after "'..err..'" error'
	if not clean_space(sp, cnt) then
		return err
	end

	err = 'single upserts error'
	for i = 1, cnt do
		sp:upsert({i}, {{'!', 2, i}})
	end
	box.snapshot()
	if not check_tuples_len(sp, cnt, 1) then
		return err
	end
	err = 'clean after "'..err..'" error'
	if not clean_space(sp, cnt) then
		return err
	end

	err = 'single replaces error'
	for i = 1, cnt do
		sp:replace({i})
	end
	box.snapshot()
	if not check_tuples_len(sp, cnt, 1) then
		return err
	end
	err = 'clean after "'..err..'" error'
	if not clean_space(sp, cnt) then
		return err
	end
	return 'ok'
end;

function fill_space_with_sizes(page_size, range_size, cnt)
	local space = box.schema.space.create('test', { engine = 'vinyl' })
	local pk = space:create_index('primary', { page_size = page_size, range_size = range_size })
	local ret = fill_space(space, cnt)
	space:drop()
	return ret
end;
test_run:cmd("setopt delimiter ''");

-- Tests on write iterator with random combinations of page_size and range_size

page_size = math.random(128, 256)
range_size = page_size * math.random(10, 20)
fill_space_with_sizes(page_size, range_size, 300)

page_size = math.random(256, 512)
range_size = page_size * math.random(10, 20)
fill_space_with_sizes(page_size, range_size, 500)

page_size = math.random(512, 1024)
range_size = page_size * math.random(10, 20)
fill_space_with_sizes(page_size, range_size, 700)

page_size = math.random(1024, 2048)
range_size = page_size * math.random(10, 20)
fill_space_with_sizes(page_size, range_size, 900)
