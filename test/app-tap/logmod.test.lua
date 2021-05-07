#!/usr/bin/env tarantool

local test = require('tap').test('log')
local log = require('log')

test:plan(8)

log.log_format('plain')

-- Test symbolic names for loglevels
local _, err = pcall(log.cfg, {level='fatal'})
test:ok(err == nil and log.cfg.level == 0, 'both got fatal')

_, err = pcall(log.cfg, {level='syserror'})
test:ok(err == nil and log.cfg.level == 1, 'got syserror')

_, err = pcall(log.cfg, {level='error'})
test:ok(err == nil and log.cfg.level == 2, 'got error')

_, err = pcall(log.cfg, {level='crit'})
test:ok(err == nil and log.cfg.level == 3, 'got crit')

_, err = pcall(log.cfg, {level='warn'})
test:ok(err == nil and log.cfg.level == 4, 'got warn')

_, err = pcall(log.cfg, {level='info'})
test:ok(err == nil and log.cfg.level == 5, 'got info')

_, err = pcall(log.cfg, {level='verbose'})
test:ok(err == nil and log.cfg.level == 6, 'got verbose')

_, err = pcall(log.cfg, {level='debug'})
test:ok(err == nil and log.cfg.level == 7, 'got debug')

test:check()
os.exit()
