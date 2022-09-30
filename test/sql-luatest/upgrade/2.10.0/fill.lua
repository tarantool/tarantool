box.execute([[CREATE TABLE t(i INT PRIMARY KEY, a INT REFERENCES t, UNIQUE(a, i));]])
box.execute([[CREATE TABLE t1(i INT PRIMARY KEY, a INT REFERENCES t, FOREIGN KEY(i, a) REFERENCES t(a, i));]])
box.execute([[CREATE TABLE t2(i INT PRIMARY KEY, a INT, CHECK(a * i > 10), CHECK(i < 7), CHECK(a > 3));]])
os.exit()
