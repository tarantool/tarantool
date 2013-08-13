-- setopt delim ';'
--
-- Insert test
--;

box.space[9]:insert('Vincent', 'Jules', 0, 'Do you know what they call a - a - a Quarter Pounder with cheese in Paris?');
box.space[9]:insert('Jules', 'Vincent', 0, 'They don`t call it a Quarter Pounder with cheese?');
box.space[9]:insert('Vincent', 'Jules', 1, 'No man, they got the metric system. They wouldn`t know what the f--k a Quarter Pounder is.');
box.space[9]:insert('Jules', 'Vincent', 1, 'Then what do they call it?');
box.space[9]:insert('Vincent', 'Jules', 2, 'They call it a `Royale` with cheese.');
box.space[9]:insert('Jules', 'Vincent', 2, 'A `Royale` with cheese!');
box.space[9]:insert('Vincent', 'Jules', 3, 'That`s right.');
box.space[9]:insert('Jules', 'Vincent', 3, 'What do they call a Big Mac?');
box.space[9]:insert('Vincent', 'Jules', 4, 'A Big Mac`s a Big Mac, but they call it `Le Big Mac.`');
box.space[9]:insert('Jules', 'Vincent', 4, '`Le Big Mac!`');
box.space[9]:insert('Vincent', 'Jules', 5, 'Ha, ha, ha.');
box.space[9]:insert('Jules', 'Vincent', 5, 'What do they call a `Whopper`?');
box.space[9]:insert('Vincent', 'Jules', 6, 'I dunno, I didn`t go into Burger King.');

box.space[9]:insert('The Wolf!', 'Vincent', 0, 'Jimmie, lead the way. Boys, get to work.');
box.space[9]:insert('Vincent', 'The Wolf!', 0, 'A please would be nice.');
box.space[9]:insert('The Wolf!', 'Vincent', 1, 'Come again?');
box.space[9]:insert('Vincent', 'The Wolf!', 1, 'I said a please would be nice.');
box.space[9]:insert('The Wolf!', 'Vincent', 2, 'Get it straight buster - I`m not here to say please, I`m here to tell you what to do and if self-preservation is an instinct you possess you`d better fucking do it and do it quick. I`m here to help - if my help`s not appreciated then lotsa luck, gentlemen.');
box.space[9]:insert('The Wolf!', 'Vincent', 3, 'I don`t mean any disrespect, I just don`t like people barking orders at me.');
box.space[9]:insert('Vincent', 'The Wolf!', 2, 'If I`m curt with you it`s because time is a factor. I think fast, I talk fast and I need you guys to act fast if you wanna get out of this. So, pretty please... with sugar on top. Clean the fucking car.');

--
-- Select test
--;

-- Select by one entry;
box.select(9, 0, 'Vincent', 'Jules', 0);
box.select(9, 0, 'Jules', 'Vincent', 0);
box.select(9, 0, 'Vincent', 'Jules', 1);
box.select(9, 0, 'Jules', 'Vincent', 1);
box.select(9, 0, 'Vincent', 'Jules', 2);
box.select(9, 0, 'Jules', 'Vincent', 2);
box.select(9, 0, 'Vincent', 'Jules', 3);
box.select(9, 0, 'Jules', 'Vincent', 3);
box.select(9, 0, 'Vincent', 'Jules', 4);
box.select(9, 0, 'Jules', 'Vincent', 4);
box.select(9, 0, 'Vincent', 'Jules', 5);
box.select(9, 0, 'Jules', 'Vincent', 5);
box.select(9, 0, 'Vincent', 'Jules', 6);

box.select(9, 0, 'The Wolf!', 'Vincent', 0);
box.select(9, 0, 'Vincent', 'The Wolf!', 0);
box.select(9, 0, 'The Wolf!', 'Vincent', 1);
box.select(9, 0, 'Vincent', 'The Wolf!', 1);
box.select(9, 0, 'The Wolf!', 'Vincent', 2);
box.select(9, 0, 'The Wolf!', 'Vincent', 3);
box.select(9, 0, 'Vincent', 'The Wolf!', 2);

-- Select all messages from Vincent to Jules;
box.select(9, 0, 'Vincent', 'Jules');

-- Select all messages from Jules to Vincent;
box.select(9, 0, 'Jules', 'Vincent');

-- Select all messages from Vincent to The Wolf;
box.select(9, 0, 'Vincent', 'The Wolf!');

-- Select all messages from The Wolf to Vincent;
box.select(9, 0, 'The Wolf!', 'Vincent');

-- Select all Vincent messages;
box.select(9, 0, 'Vincent');

--
-- Delete test
--;

-- Delete some messages from the The Wolf and Vincent dialog;
box.delete(9, 'The Wolf!', 'Vincent', 0);
box.delete(9, 'The Wolf!', 'Vincent', 3);
box.delete(9, 'Vincent', 'The Wolf!', 0);

box.update(9, {'Vincent', 'The Wolf!', 1}, '=p=p', 0, 'Updated', 4, 'New');
box.update(9, {'Updated', 'The Wolf!', 1}, '=p#p', 0, 'Vincent', 4, '');
-- Checking Vincent's last messages;
box.select(9, 0, 'Vincent', 'The Wolf!');
-- Checking The Wolf's last messages;
box.select(9, 0, 'The Wolf!', 'Vincent');

-- try to delete nonexistent message;
box.delete(9, 'Vincent', 'The Wolf!', 3);
-- try to delete patrial defined key;
box.delete(9, 'Vincent', 'The Wolf!');
-- try to delete by invalid key;
box.delete(9, 'The Wolf!', 'Vincent', 1, 'Come again?');

--
-- Update test
--;
box.update(9, {'The Wolf!', 'Vincent', 1}, '=p', 3, '<ooops>');
box.update(9, {'Vincent', 'The Wolf!', 1}, '=p', 3, '<ooops>');

-- Checking Vincent's last messages;
box.select(9, 0, 'Vincent', 'The Wolf!');
-- Checking The Wolf's last messages;
box.select(9, 0, 'The Wolf!', 'Vincent');

-- try to update a nonexistent message;
box.update(9, {'Vincent', 'The Wolf!', 3}, '=p', 3, '<ooops>');
-- try to update patrial defined key;
box.update(9, {'Vincent', 'The Wolf!'}, '=p', 3, '<ooops>');
-- try to update by invalid key;
box.update(9, {'The Wolf!', 'Vincent', 1, 'Come again?'}, '=p', 3, '<ooops>');
box.space[9]:len();
box.space[9]:truncate();
box.space[9]:len();

-- A test case for Bug#1051006 Tree iterators return garbage
--if an index is modified between calls;

box.space[16]:insert('a', 'a', 'a');
box.space[16]:insert('d', 'd', 'd');
box.space[16]:insert('e', 'e', 'e');
box.space[16]:insert('b', 'b', 'b');
box.space[16]:insert('c', 'c', 'c');

for i = 1, 2 do
    k,v = box.space[16].index[1]:next(k)
    print(' - ', v)
end;

box.space[16]:truncate();
v;
collectgarbage('collect');
v;

k,v = box.space[16].index[1]:next(k);
v;
collectgarbage('collect');
v;

for i = 1, 3 do
    k,v = box.space[16].index[1]:next(k)
    print(' - ', v)
end;

-- Bug #1082356;
box.space[19]:insert(1, 1);
box.replace_if_exists(19, 1, 1);

-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
