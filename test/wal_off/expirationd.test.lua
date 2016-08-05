fiber = require('fiber')
-- test for expirationd. iterator must continue iterating after space insert/delete
env = require('test_run')
test_run = env.new()
s0 = box.schema.space.create('tweedledum')
i0 = s0:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}, unique = true })
s0:insert{20000}
test_run:cmd("setopt delimiter ';'")
for i = 1, 10000 do
	a = math.floor(math.random() * 10000)
    s0:replace{a}
end;
hit_end = false;
gen, param, state = i0:pairs({}, {iterator = box.index.ALL});

for i = 1, 10000 do
	for j = 1, 10 do
		state, tuple = gen(param, state)
		if (tuple) then
			if (tuple[1] == 20000) then
				hit_end = true
			end
			if (math.random() > 0.9) then
				s0:delete{tuple[1]}
			end
		else
			gen, param, state = i0:pairs({}, {iterator = box.index.ALL})
		end
	end
	for j = 1, 5 do
		a = math.floor(math.random() * 10000)
		if #s0:select{a} == 0 then
			s0:insert{a}
		end
	end
	if hit_end then break end
end;
hit_end;
test_run:cmd("setopt delimiter ''");
s0:drop()
s0 = nil
