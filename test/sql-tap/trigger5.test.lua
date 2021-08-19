#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(1)

--!./tcltestrunner.lua
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file tests the triggers of views.
--
-- # Ticket #844
-- #
test:do_execsql_test(
    "trigger5-1.1",
    [[
        CREATE TABLE Item(
           a integer PRIMARY KEY NOT NULL ,
           b NUMBER NULL ,
           c int NOT NULL DEFAULT 0
        );
        CREATE TABLE Undo(id INTEGER PRIMARY KEY, UndoAction TEXT);
        INSERT INTO Item VALUES (1,38205.60865,340);
        CREATE TRIGGER trigItem_UNDO_AD AFTER DELETE ON Item FOR EACH ROW
        BEGIN
          INSERT INTO Undo VALUES
             ((SELECT coalesce(max(id),0) + 1 FROM Undo),
              (SELECT 'INSERT INTO Item (a,b,c) VALUES (' || CAST(coalesce(old.a,'NULL') AS TEXT)
                  || ',' || CAST(quote(old.b) AS STRING) || ',' ||
                  CAST(old.c AS TEXT) || ');'));
        END;
        DELETE FROM Item WHERE a = 1;
        SELECT * FROM Undo;
    ]], {
        -- <trigger5-1.1>
        1, "INSERT INTO Item (a,b,c) VALUES (1,38205.60865,340);"
        -- </trigger5-1.1>
    })

-- integrity_check trigger5-99.9
test:finish_test()
