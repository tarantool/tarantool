
-- truncate

s = box.schema.space.create('name_of_space', {engine='phia'})
i = s:create_index('name_of_index', {type = 'tree', parts = {1, 'STR'}})
s:insert{'a', 'b', 'c'}
box.space['name_of_space']:select{'a'}
box.space['name_of_space']:truncate()
box.space['name_of_space']:select{'a'}
s:drop()
phia_schedule()
