
box.cfg.phia
box.cfg.phia.threads = 5

space = box.schema.space.create('test', { engine = 'phia' })
index = space:create_index('primary', {type = 'tree', amqf=1, read_oldest=0, parts = {1, 'NUM'}})
box.phia["db.".. tostring(space.id)..":0.amqf"]
box.phia["db.".. tostring(space.id)..":0.mmap"]
space:drop()
