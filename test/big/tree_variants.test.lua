box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 1, 0, 'num')
box.insert(box.schema.INDEX_ID, 0, 1, 'i1', 'tree', 1, 1, 1, 'num64')
box.insert(box.schema.INDEX_ID, 0, 2, 'i2', 'tree', 0, 1, 2, 'num64')
box.insert(box.schema.INDEX_ID, 0, 3, 'i3', 'tree', 0, 2, 3, 'str', 4, 'str')
box.insert(box.schema.INDEX_ID, 0, 4, 'i4', 'tree', 0, 2, 6, 'str', 5, 'str')
box.insert(box.schema.INDEX_ID, 0, 5, 'i5', 'tree', 0, 1, 8, 'num')
box.insert(box.schema.INDEX_ID, 0, 6, 'i6', 'tree', 1, 5, 6, 'str', 5, 'str', 3, 'str', 4, 'str', 8, 'num')

space = box.space[0]

space:insert(0, '00000000', '00000100', 'Joe', 'Sixpack', 'Drinks', 'Amstel', 'bar', 2000)
space:insert(1, '00000001', '00000200', 'Joe', 'Sixpack', 'Drinks', 'Heineken', 'bar', 2001)
space:insert(2, '00000002', '00000200', 'Joe', 'Sixpack', 'Drinks', 'Carlsberg', 'bar', 2002)
space:insert(3, '00000003', '00000300', 'Joe', 'Sixpack', 'Drinks', 'Corona Extra', 'bar', 2003)
space:insert(4, '00000004', '00000300', 'Joe', 'Sixpack', 'Drinks', 'Stella Artois', 'bar', 2004)
space:insert(5, '00000005', '00000300', 'Joe', 'Sixpack', 'Drinks', 'Miller Genuine Draft', 'bar', 2005)
space:insert(6, '00000006', '00000400', 'John', 'Smoker', 'Hits', 'A Pipe', 'foo', 2006)
space:insert(7, '00000007', '00000400', 'John', 'Smoker', 'Hits', 'A Bong', 'foo', 2007)
space:insert(8, '00000008', '00000400', 'John', 'Smoker', 'Rolls', 'A Joint', 'foo', 2008)
space:insert(9, '00000009', '00000400', 'John', 'Smoker', 'Rolls', 'A Blunt', 'foo', 2009)

space:select(0, 1)
space:select(1,'00000002')
{space:select(2,'00000300')}
{space:select(3, 'Joe', 'Sixpack')}
{space:select(3, 'John')}
{space:select(4, 'A Pipe')}
{space:select(4, 'Miller Genuine Draft', 'Drinks')}
space:select(5, 2007)
space:select(6, 'Miller Genuine Draft', 'Drinks')

space:delete(6)
space:delete(7)
space:delete(8)
space:delete(9)

space:insert(6, 6ULL, 400ULL, 'John', 'Smoker', 'Hits', 'A Pipe', 'foo', 2006)
space:insert(7, 7ULL, 400ULL, 'John', 'Smoker', 'Hits', 'A Bong', 'foo', 2007)
space:insert(8, 8ULL, 400ULL, 'John', 'Smoker', 'Rolls', 'A Joint', 'foo', 2008)
space:insert(9, 9ULL, 400ULL, 'John', 'Smoker', 'Rolls', 'A Blunt', 'foo', 2009)

space:select(1, 6ULL)
space:select(1, 6)
{space:select(2, 400ULL)}
{space:select(2, 400)}

{space:select(0)}

-- Test incorrect keys - supplied key field type does not match index type
-- https://bugs.launchpad.net/tarantool/+bug/1072624
space:insert('', '00000001', '00000002', '', '', '', '', '', 0)
space:insert('xxxxxxxx', '00000001', '00000002', '', '', '', '', '', 0)
space:insert(1, '', '00000002', '', '', '', '', '', 0)
space:insert(1, 'xxxxxxxxxxx', '00000002', '', '', '', '', '', 0)

space:drop()
