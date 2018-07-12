remote = require('net.box')
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

-- gh-3010: COLLATE after LIMIT should throw an error

-- All of these tests should throw error "near "COLLATE": syntax error"
box.sql.execute("SELECT 1 LIMIT 1 COLLATE BINARY;")
box.sql.execute("SELECT 1 LIMIT 1 COLLATE BINARY OFFSET 1;")
box.sql.execute("SELECT 1 LIMIT 1 OFFSET 1 COLLATE BINARY;")
box.sql.execute("SELECT 1 LIMIT 1, 1 COLLATE BINARY;")
box.sql.execute("SELECT 1 LIMIT 1 COLLATE BINARY, 1;")

-- gh-3052: upper/lower support only default locale
-- For tr-TR result depends on collation
box.sql.execute([[CREATE TABLE tu (descriptor CHAR(50) PRIMARY KEY, letter CHAR(50))]]);
box.internal.collation.create('TURKISH', 'ICU', 'tr-TR', {strength='primary'});
box.sql.execute([[INSERT INTO tu VALUES ('Latin Capital Letter I U+0049','I');]])
box.sql.execute([[INSERT INTO tu VALUES ('Latin Small Letter I U+0069','i');]])
box.sql.execute([[INSERT INTO tu VALUES ('Latin Capital Letter I With Dot Above U+0130','İ');]])
box.sql.execute([[INSERT INTO tu VALUES ('Latin Small Letter Dotless I U+0131','ı');]])
-- Without collation
box.sql.execute([[SELECT descriptor, upper(letter) AS upper,lower(letter) AS lower FROM tu;]])
-- With collation
box.sql.execute([[SELECT descriptor, upper(letter COLLATE "TURKISH") AS upper,lower(letter COLLATE "TURKISH") AS lower FROM tu;]])
box.internal.collation.drop('TURKISH')

-- For de-DE result is actually the same
box.internal.collation.create('GERMAN', 'ICU', 'de-DE', {strength='primary'});
box.sql.execute([[INSERT INTO tu VALUES ('German Small Letter Sharp S U+00DF','ß');]])
-- Without collation
box.sql.execute([[SELECT descriptor, upper(letter), letter FROM tu where UPPER(letter) = 'SS';]])
-- With collation
box.sql.execute([[SELECT descriptor, upper(letter COLLATE "GERMAN"), letter FROM tu where UPPER(letter COLLATE "GERMAN") = 'SS';]])
box.internal.collation.drop('GERMAN')
box.sql.execute(([[DROP TABLE tu]]))

box.schema.user.grant('guest','read,write,execute', 'universe')
cn = remote.connect(box.cfg.listen)

cn:execute('select 1 limit ? collate not_exist', {1})

cn:close()
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
