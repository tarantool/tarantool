#!/usr/bin/env tarantool

--
-- Check that Tarantool creates ADMIN session for #! script
-- 
session = require('box.session')
print('session.id()', session.id())
print('session.uid()', session.uid())
