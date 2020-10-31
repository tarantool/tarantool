netbox = require('net.box')
--
-- gh-5451: "path" is not installed to index schema.
--

box.schema.user.grant('guest','super')

_ = box.schema.space.create('gh5451')
_ = box.space.gh5451:create_index('primary',                    \
    {parts = {{field = 1, type = 'unsigned', path = 'path1'}}})
_ = box.space.gh5451:create_index('secondary',                  \
    {parts = {{field = 2, type = 'unsigned', path = 'path2'}}})
_ = box.space.gh5451:create_index('multikey',                   \
    {parts = {{field = 3, type = 'unsigned', path = '[*]'}}})

c = netbox.connect(box.cfg.listen)

c.space.gh5451.index[0].parts
c.space.gh5451.index[1].parts
c.space.gh5451.index[2].parts

c:close()

box.space.gh5451:drop()
box.schema.user.revoke('guest', 'super')
