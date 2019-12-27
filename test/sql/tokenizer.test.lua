env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

sql_tokenizer = require('sql_tokenizer')

sql_tokenizer.split_sql('select 1; select 2; select 3;')
sql_tokenizer.split_sql(';')
sql_tokenizer.split_sql('')
sql_tokenizer.split_sql('select 1')
sql_tokenizer.split_sql('create table test(a, b, c, d, primary key(a, b, c))')
sql_tokenizer.split_sql('select ";" from ";;;;"\'\' as ";"; select 100;     \n\n')

test_run:cmd("setopt delimiter ';'")
sql_tokenizer.split_sql('create trigger begin\n'..
				'select 1; select 2;\n'..
				'create table a (b,c,d);\n'..
			'end;');
test_run:cmd("setopt delimiter ''");

sql_tokenizer.split_sql('-- comment comment comment; select 1; select 2;')
sql_tokenizer.split_sql('select 1; select /* comment comment select 100; select 1000; */ 3; select 2;')
sql_tokenizer.split_sql('select abc from test where case abc when 100 then kek else grek end;')

test_run:cmd("setopt delimiter ';'")
sql_tokenizer.split_sql('create temp trigger begin\n'..
				'select case cond when a 100 else 200 end;\n'..
				'insert into tst values v a l u e s;\n'..
			'end;\n\n\n\t\t\n\n select 200..";select1000";');
test_run:cmd("setopt delimiter ''");

sql_tokenizer.split_sql('select 1; -- test comment\n; select 2;')

sql_tokenizer.split_sql("START TRANSACTION; insert into table values ('example kek--'); commit;")

sql_tokenizer.split_sql("insert into quoted(a) value 'lalala'")

sql_tokenizer.split_sql("insert into table values('textextextext''textextext''extext--extext/*ext''ext*/'); commit;")
