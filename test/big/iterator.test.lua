-- setopt delim ';'
dofile('utils.lua');

box.space[20]:insert('pid_001', 'sid_001', 'tid_998', 'a');
box.space[20]:insert('pid_002', 'sid_001', 'tid_997', 'a');
box.space[20]:insert('pid_003', 'sid_002', 'tid_997', 'b');
box.space[20]:insert('pid_005', 'sid_002', 'tid_996', 'b');
box.space[20]:insert('pid_007', 'sid_003', 'tid_996', 'a');
box.space[20]:insert('pid_011', 'sid_004', 'tid_996', 'c');
box.space[20]:insert('pid_013', 'sid_005', 'tid_996', 'b');
box.space[20]:insert('pid_017', 'sid_006', 'tid_996', 'a');
box.space[20]:insert('pid_019', 'sid_005', 'tid_995', 'a');
box.space[20]:insert('pid_023', 'sid_005', 'tid_994', 'a');

-------------------------------------------------------------------------------
-- Iterator: tree single-part unique
-------------------------------------------------------------------------------;

iterate(20, 0, 0, 1);
iterate(20, 0, 0, 1, box.index.ALL);
iterate(20, 0, 0, 1, box.index.EQ);
iterate(20, 0, 0, 1, box.index.REQ);
iterate(20, 0, 0, 1, box.index.GE);
iterate(20, 0, 0, 1, box.index.GT);
iterate(20, 0, 0, 1, box.index.LE);
iterate(20, 0, 0, 1, box.index.LT);
iterate(20, 0, 0, 1, box.index.EQ, 'pid_003');
iterate(20, 0, 0, 1, box.index.REQ, 'pid_003');
iterate(20, 0, 0, 1, box.index.EQ, 'pid_666');
iterate(20, 0, 0, 1, box.index.REQ, 'pid_666');
iterate(20, 0, 0, 1, box.index.GE, 'pid_001');
iterate(20, 0, 0, 1, box.index.GT, 'pid_001');
iterate(20, 0, 0, 1, box.index.GE, 'pid_999');
iterate(20, 0, 0, 1, box.index.GT, 'pid_999');
iterate(20, 0, 0, 1, box.index.LE, 'pid_002');
iterate(20, 0, 0, 1, box.index.LT, 'pid_002');
iterate(20, 0, 0, 1, box.index.LE, 'pid_000');
iterate(20, 0, 0, 1, box.index.LT, 'pid_000');

-------------------------------------------------------------------------------
-- Iterator: tree single-part non-unique
-------------------------------------------------------------------------------;

iterate(20, 1, 1, 2, box.index.ALL);
iterate(20, 1, 1, 2, box.index.EQ);
iterate(20, 1, 1, 2, box.index.REQ);
iterate(20, 1, 1, 2, box.index.GE);
iterate(20, 1, 1, 2, box.index.GT);
iterate(20, 1, 1, 2, box.index.LE);
iterate(20, 1, 1, 2, box.index.LT);
iterate(20, 1, 1, 2, box.index.EQ, 'sid_005');
iterate(20, 1, 1, 2, box.index.REQ, 'sid_005');
iterate(20, 1, 1, 2, box.index.GE, 'sid_005');
iterate(20, 1, 1, 2, box.index.GT, 'sid_005');
iterate(20, 1, 1, 2, box.index.GE, 'sid_999');
iterate(20, 1, 1, 2, box.index.GT, 'sid_999');
iterate(20, 1, 1, 2, box.index.LE, 'sid_005');
iterate(20, 1, 1, 2, box.index.LT, 'sid_005');
iterate(20, 1, 1, 2, box.index.LE, 'sid_000');
iterate(20, 1, 1, 2, box.index.LT, 'sid_000');

-------------------------------------------------------------------------------
-- Iterator: tree multi-part unique
-------------------------------------------------------------------------------;

iterate(20, 2, 1, 3, box.index.ALL);
iterate(20, 2, 1, 3, box.index.EQ);
iterate(20, 2, 1, 3, box.index.REQ);
iterate(20, 2, 1, 3, box.index.GE);
iterate(20, 2, 1, 3, box.index.GT);
iterate(20, 2, 1, 3, box.index.LE);
iterate(20, 2, 1, 3, box.index.LT);
iterate(20, 2, 1, 3, box.index.EQ, 'sid_005');
iterate(20, 2, 1, 3, box.index.EQ, 'sid_005', 'tid_995');
iterate(20, 2, 1, 3, box.index.EQ, 'sid_005', 'tid_999');
iterate(20, 2, 1, 3, box.index.REQ, 'sid_005');
iterate(20, 2, 1, 3, box.index.REQ, 'sid_005', 'tid_995');
iterate(20, 2, 1, 3, box.index.REQ, 'sid_005', 'tid_999');
iterate(20, 2, 1, 3, box.index.GE, 'sid_005');
iterate(20, 2, 1, 3, box.index.GT, 'sid_005');
iterate(20, 2, 1, 3, box.index.GE, 'sid_005', 'tid_995');
iterate(20, 2, 1, 3, box.index.GT, 'sid_005', 'tid_995');
iterate(20, 2, 1, 3, box.index.GE, 'sid_005', 'tid_999');
iterate(20, 2, 1, 3, box.index.GT, 'sid_005', 'tid_999');
iterate(20, 2, 1, 3, box.index.GE, 'sid_999');
iterate(20, 2, 1, 3, box.index.GT, 'sid_999');
iterate(20, 2, 1, 3, box.index.LE, 'sid_005');
iterate(20, 2, 1, 3, box.index.LT, 'sid_005');
iterate(20, 2, 1, 3, box.index.LE, 'sid_005', 'tid_997');
iterate(20, 2, 1, 3, box.index.LT, 'sid_005', 'tid_997');
iterate(20, 2, 1, 3, box.index.LE, 'sid_005', 'tid_000');
iterate(20, 2, 1, 3, box.index.LT, 'sid_005', 'tid_000');
iterate(20, 2, 1, 3, box.index.LE, 'sid_000');
iterate(20, 2, 1, 3, box.index.LT, 'sid_000');

-------------------------------------------------------------------------------
-- Iterator: tree multi-part non-unique
-------------------------------------------------------------------------------;

iterate(20, 3, 2, 4, box.index.ALL);
iterate(20, 3, 2, 4, box.index.EQ);
iterate(20, 3, 2, 4, box.index.REQ);
iterate(20, 3, 2, 4, box.index.GE);
iterate(20, 3, 2, 4, box.index.GT);
iterate(20, 3, 2, 4, box.index.LE);
iterate(20, 3, 2, 4, box.index.LT);
iterate(20, 3, 2, 4, box.index.EQ, 'tid_996');
iterate(20, 3, 2, 4, box.index.EQ, 'tid_996', 'a');
iterate(20, 3, 2, 4, box.index.EQ, 'tid_996', 'z');
iterate(20, 3, 2, 4, box.index.REQ, 'tid_996');
iterate(20, 3, 2, 4, box.index.REQ, 'tid_996', 'a');
iterate(20, 3, 2, 4, box.index.REQ, 'tid_996', '0');
iterate(20, 3, 2, 4, box.index.GE, 'tid_997');
iterate(20, 3, 2, 4, box.index.GT, 'tid_997');
iterate(20, 3, 2, 4, box.index.GE, 'tid_998');
iterate(20, 3, 2, 4, box.index.GT, 'tid_998');
iterate(20, 3, 2, 4, box.index.LE, 'tid_997');
iterate(20, 3, 2, 4, box.index.LT, 'tid_997');
iterate(20, 3, 2, 4, box.index.LE, 'tid_000');
iterate(20, 3, 2, 4, box.index.LT, 'tid_000');
iterate(20, 3, 2, 4, box.index.LT, 'tid_996', 'to', 'many', 'keys');

-------------------------------------------------------------------------------
-- Iterator: hash single-part unique
-------------------------------------------------------------------------------;

iterate(20, 4, 0, 1);
iterate(20, 4, 0, 1, box.index.ALL);
iterate(20, 4, 0, 1, box.index.EQ);
iterate(20, 4, 0, 1, box.index.GE);
iterate(20, 4, 0, 1, box.index.EQ, 'pid_003');
iterate(20, 4, 0, 1, box.index.EQ, 'pid_666');
iterate(20, 4, 0, 1, box.index.GE, 'pid_001');
iterate(20, 4, 0, 1, box.index.GE, 'pid_999');

-------------------------------------------------------------------------------
-- Iterator: hash multi-part unique
-------------------------------------------------------------------------------;

iterate(20, 5, 1, 3, box.index.ALL);
iterate(20, 5, 1, 3, box.index.EQ);
iterate(20, 5, 1, 3, box.index.EQ, 'sid_005');
iterate(20, 5, 1, 3, box.index.GE);
iterate(20, 2, 1, 3, box.index.EQ, 'sid_005', 'tid_995');
iterate(20, 2, 1, 3, box.index.EQ, 'sid_005', 'tid_999');
iterate(20, 2, 1, 3, box.index.EQ, 'sid_005', 'tid_995', 'a');
iterate(20, 2, 1, 3, box.index.GE, 'sid_005', 'tid_995');
iterate(20, 2, 1, 3, box.index.GE, 'sid_005', 'tid_999');

-------------------------------------------------------------------------------
-- Iterator: various
-------------------------------------------------------------------------------;

box.space[20].index[0]:iterator(-666);

box.space[20]:truncate();

-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
