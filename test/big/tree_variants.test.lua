-- setopt delim ';'
box.space[6]:insert(0, '00000000', '00000100', 'Joe', 'Sixpack', 'Drinks', 'Amstel', 'bar', 2000);
box.space[6]:insert(1, '00000001', '00000200', 'Joe', 'Sixpack', 'Drinks', 'Heineken', 'bar', 2001);
box.space[6]:insert(2, '00000002', '00000200', 'Joe', 'Sixpack', 'Drinks', 'Carlsberg', 'bar', 2002);
box.space[6]:insert(3, '00000003', '00000300', 'Joe', 'Sixpack', 'Drinks', 'Corona Extra', 'bar', 2003);
box.space[6]:insert(4, '00000004', '00000300', 'Joe', 'Sixpack', 'Drinks', 'Stella Artois', 'bar', 2004);
box.space[6]:insert(5, '00000005', '00000300', 'Joe', 'Sixpack', 'Drinks', 'Miller Genuine Draft', 'bar', 2005);
box.space[6]:insert(6, '00000006', '00000400', 'John', 'Smoker', 'Hits', 'A Pipe', 'foo', 2006);
box.space[6]:insert(7, '00000007', '00000400', 'John', 'Smoker', 'Hits', 'A Bong', 'foo', 2007);
box.space[6]:insert(8, '00000008', '00000400', 'John', 'Smoker', 'Rolls', 'A Joint', 'foo', 2008);
box.space[6]:insert(9, '00000009', '00000400', 'John', 'Smoker', 'Rolls', 'A Blunt', 'foo', 2009);

box.space[6]:select(0, 1);
box.space[6]:select(1,'00000002');
box.space[6]:select(2,'00000300');
box.space[6]:select(3, 'Joe', 'Sixpack');
box.space[6]:select(3, 'John');
box.space[6]:select(4, 'A Pipe');
box.space[6]:select(4, 'Miller Genuine Draft', 'Drinks');
box.space[6]:select(5, 2007);
box.space[6]:select(6, 'Miller Genuine Draft', 'Drinks');

box.space[6]:delete(6);
box.space[6]:delete(7);
box.space[6]:delete(8);
box.space[6]:delete(9);

box.space[6]:insert(6, 6ULL, 400ULL, 'John', 'Smoker', 'Hits', 'A Pipe', 'foo', 2006);
box.space[6]:insert(7, 7ULL, 400ULL, 'John', 'Smoker', 'Hits', 'A Bong', 'foo', 2007);
box.space[6]:insert(8, 8ULL, 400ULL, 'John', 'Smoker', 'Rolls', 'A Joint', 'foo', 2008);
box.space[6]:insert(9, 9ULL, 400ULL, 'John', 'Smoker', 'Rolls', 'A Blunt', 'foo', 2009);

box.space[6]:select(1, 6ULL);
box.space[6]:select(1, 6);
box.space[6]:select(2, 400ULL);
box.space[6]:select(2, 400);

for k,v in box.space[6]:pairs() do
    print(' - ', v)
end;

-- Test incorrect keys - supplied key field type does not match index type
-- https://bugs.launchpad.net/tarantool/+bug/1072624;
box.space[6]:insert('', '00000001', '00000002', '', '', '', '', '', 0);
box.space[6]:insert('xxxxxxxx', '00000001', '00000002', '', '', '', '', '', 0);
box.space[6]:insert(1, '', '00000002', '', '', '', '', '', 0);
box.space[6]:insert(1, 'xxxxxxxxxxx', '00000002', '', '', '', '', '', 0);

-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
