#!/usr/bin/env tarantool

--
-- Check that Tarantool creates ADMIN session for #! script
-- 
session = require('session')
print('session.id()', session.id())
print('session.uid()', session.uid())
