#!/usr/bin/env tarantool

local pwd = require("pwd")

local test = require("tap").test("pwd")
test:plan(6)

local base_group = pwd.getgr()
local base_user  = pwd.getpw()

test:is_deeply(pwd.getpw(base_user.id),   base_user, "checking user by id")
test:is_deeply(pwd.getpw(base_user.name), base_user, "checking user by name")

test:is_deeply(pwd.getgr(base_group.id),   base_group, "checking group by id")
test:is_deeply(pwd.getgr(base_group.name), base_group, "checking group by name")

test:ok(#pwd.getpwall() > 0, "check output of getpwall")
test:ok(#pwd.getgrall() > 0, "check output of getgrall")

os.exit(test:check() == true and 0 or 1)
