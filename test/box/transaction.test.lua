--# setopt delimiter ';'
-- empty transaction - ok
box.begin() box.commit();
-- double begin
box.begin() box.begin();
-- no active transaction since exception rolled it back
box.commit();
-- double commit - error
box.begin() box.commit() box.commit();
-- commit if not started - error
box.commit();
-- rollback if not started - ok
box.rollback()
-- double rollback - ok
box.begin() box.rollback() box.rollback();
-- rollback of an empty trans - ends transaction
box.begin() box.rollback();
-- no current transaction - error
box.commit();
fiber = require('fiber');
function sloppy()
    box.begin()
end;
f = fiber.wrap(sloppy);
-- when the sloppy fiber ends, its session has an active transction
-- ensure it's rolled back automatically
fiber.sleep(0);
fiber.sleep(0);
-- transactions and system spaces
box.begin() box.schema.space.create('test');
box.rollback();
box.begin() box.schema.func.create('test');
box.rollback();
box.begin() box.schema.user.create('test');
box.rollback();
box.begin() box.schema.user.grant('guest', 'read', 'universe');
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
