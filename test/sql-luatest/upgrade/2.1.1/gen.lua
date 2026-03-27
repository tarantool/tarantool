box.cfg{}
box.sql.execute([[CREATE TABLE t5(x  INT primary key, y INT, CHECK( x < 2 ))]])
box.schema.user.grant('guest', 'super')
box.snapshot()
os.exit(0)
