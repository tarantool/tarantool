
box.cfg.sophia
box.cfg.sophia.threads = 5

space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'tree', mmap=1, amqf=1, parts = {1, 'NUM'}})
box.sophia["db.".. tostring(space.id)..".amqf"]
box.sophia["db.".. tostring(space.id)..".mmap"]
space:drop()
