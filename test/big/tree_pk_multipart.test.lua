--
-- Insert test
--
box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
-- Multipart primary key (sender nickname, receiver nickname, message id)
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 3, 0, 'str', 1, 'str', 2, 'num')

space = box.space[0]

space:insert('Vincent', 'Jules', 0, 'Do you know what they call a - a - a Quarter Pounder with cheese in Paris?')
space:insert('Jules', 'Vincent', 0, 'They don`t call it a Quarter Pounder with cheese?')
space:insert('Vincent', 'Jules', 1, 'No man, they got the metric system. They wouldn`t know what the f--k a Quarter Pounder is.')
space:insert('Jules', 'Vincent', 1, 'Then what do they call it?')
space:insert('Vincent', 'Jules', 2, 'They call it a `Royale` with cheese.')
space:insert('Jules', 'Vincent', 2, 'A `Royale` with cheese!')
space:insert('Vincent', 'Jules', 3, 'That`s right.')
space:insert('Jules', 'Vincent', 3, 'What do they call a Big Mac?')
space:insert('Vincent', 'Jules', 4, 'A Big Mac`s a Big Mac, but they call it `Le Big Mac.`')
space:insert('Jules', 'Vincent', 4, '`Le Big Mac!`')
space:insert('Vincent', 'Jules', 5, 'Ha, ha, ha.')
space:insert('Jules', 'Vincent', 5, 'What do they call a `Whopper`?')
space:insert('Vincent', 'Jules', 6, 'I dunno, I didn`t go into Burger King.')

space:insert('The Wolf!', 'Vincent', 0, 'Jimmie, lead the way. Boys, get to work.')
space:insert('Vincent', 'The Wolf!', 0, 'A please would be nice.')
space:insert('The Wolf!', 'Vincent', 1, 'Come again?')
space:insert('Vincent', 'The Wolf!', 1, 'I said a please would be nice.')
space:insert('The Wolf!', 'Vincent', 2, 'Get it straight buster - I`m not here to say please, I`m here to tell you what to do and if self-preservation is an instinct you possess you`d better fucking do it and do it quick. I`m here to help - if my help`s not appreciated then lotsa luck, gentlemen.')
space:insert('The Wolf!', 'Vincent', 3, 'I don`t mean any disrespect, I just don`t like people barking orders at me.')
space:insert('Vincent', 'The Wolf!', 2, 'If I`m curt with you it`s because time is a factor. I think fast, I talk fast and I need you guys to act fast if you wanna get out of this. So, pretty please... with sugar on top. Clean the fucking car.')

--
-- Select test
--

-- Select by one entry
space:select(0, 'Vincent', 'Jules', 0)
space:select(0, 'Jules', 'Vincent', 0)
space:select(0, 'Vincent', 'Jules', 1)
space:select(0, 'Jules', 'Vincent', 1)
space:select(0, 'Vincent', 'Jules', 2)
space:select(0, 'Jules', 'Vincent', 2)
space:select(0, 'Vincent', 'Jules', 3)
space:select(0, 'Jules', 'Vincent', 3)
space:select(0, 'Vincent', 'Jules', 4)
space:select(0, 'Jules', 'Vincent', 4)
space:select(0, 'Vincent', 'Jules', 5)
space:select(0, 'Jules', 'Vincent', 5)
space:select(0, 'Vincent', 'Jules', 6)

space:select(0, 'The Wolf!', 'Vincent', 0)
space:select(0, 'Vincent', 'The Wolf!', 0)
space:select(0, 'The Wolf!', 'Vincent', 1)
space:select(0, 'Vincent', 'The Wolf!', 1)
space:select(0, 'The Wolf!', 'Vincent', 2)
space:select(0, 'The Wolf!', 'Vincent', 3)
space:select(0, 'Vincent', 'The Wolf!', 2)

-- Select all messages from Vincent to Jules
space:select(0, 'Vincent', 'Jules')

-- Select all messages from Jules to Vincent
space:select(0, 'Jules', 'Vincent')

-- Select all messages from Vincent to The Wolf
space:select(0, 'Vincent', 'The Wolf!')

-- Select all messages from The Wolf to Vincent
space:select(0, 'The Wolf!', 'Vincent')

-- Select all Vincent messages
space:select(0, 'Vincent')

--
-- Delete test
--

-- Delete some messages from the The Wolf and Vincent dialog
space:delete('The Wolf!', 'Vincent', 0)
space:delete('The Wolf!', 'Vincent', 3)
space:delete('Vincent', 'The Wolf!', 0)

space:update({'Vincent', 'The Wolf!', 1}, '=p=p', 0, 'Updated', 4, 'New')
space:update({'Updated', 'The Wolf!', 1}, '=p#p', 0, 'Vincent', 4, '')
-- Checking Vincent's last messages
space:select(0, 'Vincent', 'The Wolf!')
-- Checking The Wolf's last messages
space:select(0, 'The Wolf!', 'Vincent')

-- try to delete nonexistent message
space:delete('Vincent', 'The Wolf!', 3)
-- try to delete patrial defined key
space:delete('Vincent', 'The Wolf!')
-- try to delete by invalid key
space:delete('The Wolf!', 'Vincent', 1, 'Come again?')

--
-- Update test
--
space:update({'The Wolf!', 'Vincent', 1}, '=p', 3, '<ooops>')
space:update({'Vincent', 'The Wolf!', 1}, '=p', 3, '<ooops>')

-- Checking Vincent's last messages
space:select(0, 'Vincent', 'The Wolf!')
-- Checking The Wolf's last messages
space:select(0, 'The Wolf!', 'Vincent')

-- try to update a nonexistent message
space:update({'Vincent', 'The Wolf!', 3}, '=p', 3, '<ooops>')
-- try to update patrial defined key
space:update({'Vincent', 'The Wolf!'}, '=p', 3, '<ooops>')
-- try to update by invalid key
space:update({'The Wolf!', 'Vincent', 1, 'Come again?'}, '=p', 3, '<ooops>')
space:len()
space:truncate()
space:len()

-- A test case for Bug#1051006 Tree iterators return garbage
--if an index is modified between calls
--
box.replace(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 1, 0, 'str')
box.replace(box.schema.INDEX_ID, 0, 1, 'second', 'tree', 1, 2, 1, 'str', 2, 'str')
space = box.space[0]

space:insert('a', 'a', 'a')
space:insert('d', 'd', 'd')
space:insert('e', 'e', 'e')
space:insert('b', 'b', 'b')
space:insert('c', 'c', 'c')

t = {}
-- setopt delimiter ';'
for i = 1, 2 do
    k,v = space.index[1]:next(k)
    table.insert(t, v)
end;
-- setopt delimiter ''

t

space:truncate()
v
collectgarbage('collect')
v

k,v = space.index[1]:next(k)
v
collectgarbage('collect')
v

t = {}
-- setopt delimiter ';'
for i = 1, 3 do
    k,v = space.index[1]:next(k)
    table.insert(t, v)
end;
-- setopt delimiter ''
t
space:drop()

-- Bug #1082356
-- Space #19, https://bugs.launchpad.net/tarantool/+bug/1082356

box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
-- Multipart primary key (sender nickname, receiver nickname, message id)
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 2, 0, 'num', 2, 'num')

space = box.space[0]

space:insert(1, 1)
space:replace_if_exists(1, 1)

space:drop()

space = nil

-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
