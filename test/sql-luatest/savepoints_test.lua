local server = require('luatest.server')
local t = require('luatest')

local g = t.group("gh", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_default_engine" = '%s']], {engine})
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- These tests check that SQL savepoints properly work outside
-- transactions as well as inside transactions started in Lua.
-- gh-3313
 g.test_3313_savepoints = function(cg)
    cg.server:exec(function()
        local exp_err = "No active transaction"
        local _, err = box.execute([[SAVEPOINT t1;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[RELEASE SAVEPOINT t1;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[ROLLBACK TO SAVEPOINT t1;]])
        t.assert_equals(tostring(err), exp_err)

        box.begin()
        _, err = box.execute([[SAVEPOINT t1;]])
        t.assert_equals(err, nil)
        _, err = box.execute([[RELEASE SAVEPOINT t1;]])
        t.assert_equals(err, nil)
        box.commit()

        box.begin()
        _, err = box.execute([[SAVEPOINT t1;]])
        t.assert_equals(err, nil)
        _, err = box.execute([[ROLLBACK TO t1;]])
        t.assert_equals(err, nil)
        box.commit()

        box.begin()
        _, err = box.execute([[SAVEPOINT t1;]])
        t.assert_equals(err, nil)
        box.commit()

        box.commit()
    end)
end

-- These tests check that release of SQL savepoints works as desired.
-- gh-3379
g.test_3379_savepoints = function(cg)
    cg.server:exec(function()
        local release_sv = function()
            box.begin()
            box.execute([[SAVEPOINT t1;]])
            box.execute([[RELEASE SAVEPOINT t1;]])
        end;

        release_sv()
        box.commit()

        local release_sv_2 = function()
            box.begin()
            box.execute([[SAVEPOINT t1]])
            box.execute([[SAVEPOINT t2;]])
            box.execute([[SAVEPOINT t3;]])
            box.execute([[RELEASE SAVEPOINT t2;]])
            local _, err = box.execute([[ROLLBACK TO t1;]])
            t.assert_equals(err, nil)
        end

        release_sv_2()
        box.commit()

        local release_sv_fail = function()
            box.begin()
            box.execute([[SAVEPOINT t1;]])
            box.execute([[SAVEPOINT t2;]])
            box.execute([[RELEASE SAVEPOINT t2;]])
            box.execute([[RELEASE SAVEPOINT t1;]])
            local _, err = box.execute([[ROLLBACK TO t1;]])
            if err ~= nil then
                return err
            end
        end;
        local exp_err = "Can not rollback to savepoint: "..
                        "the savepoint does not exist"

        local err = release_sv_fail()
        t.assert_equals(tostring(err), exp_err)
        box.commit()

        -- Make sure that if the current transaction has a savepoint
        -- with the same name, the old savepoint is deleted and
        -- a new one is set. Note that no error should be raised.
        --
        local collision_sv_2 = function()
            box.begin()
            box.execute([[SAVEPOINT t1;]])
            box.execute([[SAVEPOINT t2;]])
            local _, err = box.execute([[SAVEPOINT t1;]])
            t.assert_equals(err, nil)
            box.execute([[RELEASE SAVEPOINT t1;]])
            local _, err = box.execute([[RELEASE SAVEPOINT t1;]])
            t.assert_not_equals(err, nil)
            local _, err = box.execute([[ROLLBACK TO t2;]])
            t.assert_equals(err, nil)
        end

        collision_sv_2()
        box.commit()
    end)
end
