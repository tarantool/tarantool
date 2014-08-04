--# setopt delimiter ';'
-- empty transaction - ok
box.begin() box.commit();
-- double begin
box.begin() box.begin();
-- no active transaction since exception rolled it back
box.commit();
-- double commit - implicit start of transaction 
box.begin() box.commit() box.commit();
-- commit if not started - implicit start of transaction
box.commit();
-- rollback if not started - ok
box.rollback()
-- double rollback - ok
box.begin() box.rollback() box.rollback();
-- rollback of an empty trans - ends transaction
box.begin() box.rollback();
-- no current transaction - implicit begin 
box.commit();
fiber = require('fiber');
function sloppy()
    box.begin()
end;
f = fiber.create(sloppy);
-- when the sloppy fiber ends, its session has an active transction
-- ensure it's rolled back automatically
f:status();
fiber.sleep(0);
fiber.sleep(0);
f:status();
-- transactions and system spaces
box.begin() box.schema.space.create('test');
box.rollback();
box.begin() box.schema.func.create('test');
box.rollback();
box.begin() box.schema.user.create('test');
box.rollback();
box.begin() box.schema.user.grant('guest', 'read', 'space', '_priv');
box.rollback();
box.begin() box.space._schema:insert{'test'};
box.rollback();
box.begin() box.space._cluster:insert{123456789, 'abc'};
box.rollback();
s = box.schema.space.create('test');
box.begin() s:create_index('primary');
box.rollback();
s:create_index('primary');
function multi()
    box.begin()
    s:auto_increment{'first row'}
    s:auto_increment{'second row'}
    t = s:select{}
    box.commit()
end;
multi();
t;
s:select{};
s:truncate();
function multi()
    box.begin()
    s:auto_increment{'first row'}
    s:auto_increment{'second row'}
    t = s:select{}
    box.rollback()
end;
multi();
t;
s:select{};
function multi()
    box.begin()
    s:insert{1, 'first row'}
    pcall(s.insert, s, {1, 'duplicate'})
    t = s:select{}
    box.commit()
end;
multi();
t;
s:select{};
s:truncate();
--
-- Test that fiber yield causes a transaction rollback
-- but only if the transaction has changed any data
--
-- Test admin console
box.begin();
-- should be ok - active transaction, and we don't
-- know, maybe it will use sophia engine, which
-- may support yield() in the future, so we don't roll
-- back a transction with no statements.
box.commit();
box.begin() s:insert{1, 'Must be rolled back'};
-- nothing to commit because of yield
box.commit();
-- nothing - the transaction was rolled back
s:select{}
-- Test background fiber
--
function sloppy()
    box.begin()
    s:insert{1, 'From background fiber'}
end;
f = fiber.create(sloppy);
while f:status() == 'running' do
    fiber.sleep(0)
end;
-- When the sloppy fiber ends, its session has an active transction
-- It's rolled back automatically
s:select{};
function sloppy()
    box.begin()
    s:insert{1, 'From background fiber'}
    fiber.sleep(0)
    pcall(box.commit)
    t = s:select{}
end;
f = fiber.create(sloppy);
while f:status() == 'running' do
    fiber.sleep(0)
end;
t;
s:select{};
s:drop();
--# setopt delimiter ''
tester = box.schema.create_space('tester')
tester:create_index('primary')
box.begin() tester:insert{1} box.rollback()
tester:select{1}
box.begin() tester:insert{1} box.commit()
tester:select{1}
