show injections;
set injection some-injection on;
box.space[0]:select(0,222444);
set injection ERRINJ_TESTING on;
box.space[0]:select(0,222444);
set injection ERRINJ_TESTING off;

-- Check how well we handle a failed log write;
set injection ERRINJ_WAL_IO on;
box.space[0]:insert(1);
box.space[0]:select(0,1);
set injection ERRINJ_WAL_IO off;
box.space[0]:insert(1);
set injection ERRINJ_WAL_IO on;
box.space[0]:update(1, '=p', 0, 2);
box.space[0]:select(0,1);
box.space[0]:select(0,2);
set injection ERRINJ_WAL_IO off;
box.space[0]:truncate();

-- Check a failed log rotation;
set injection ERRINJ_WAL_ROTATE on;
box.space[0]:insert(1);
box.space[0]:select(0,1);
set injection ERRINJ_WAL_ROTATE off;
box.space[0]:insert(1);
set injection ERRINJ_WAL_ROTATE on;
box.space[0]:update(1, '=p', 0, 2);
box.space[0]:select(0,1);
box.space[0]:select(0,2);
set injection ERRINJ_WAL_ROTATE off;
box.space[0]:update(1, '=p', 0, 2);
box.space[0]:select(0,1);
box.space[0]:select(0,2);
set injection ERRINJ_WAL_ROTATE on;
box.space[0]:truncate();
set injection ERRINJ_WAL_ROTATE off;
box.space[0]:truncate();
-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
