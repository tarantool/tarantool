box.cfg{}

local create_table_statement = [[
	CREATE TABLE sbtest1(
 		id INTEGER,
		k INTEGER DEFAULT '0' NOT NULL,
		c CHAR(120) DEFAULT '' NOT NULL,
		pad CHAR(60) DEFAULT '' NOT NULL,
		PRIMARY KEY (id)); 
	]]

box.sql.execute("DROP TABLE IF EXISTS sbtest1")
box.sql.execute(create_table_statement)

for i=1,2000,1 do box.sql.execute("INSERT INTO sbtest1 VALUES (".. i ..", ".. i ..", 'blabla', 'bloblo')") end

box.sql.execute("CREATE INDEX k_1 ON sbtest1(k)")

box.cfg{listen=3301}


