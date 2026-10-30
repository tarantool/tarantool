#!/usr/bin/env tarantool

local test = require('tap').test('log')
local log = require('log')

test:plan(8)

log.log_format('plain')

-- Test symbolic names for loglevels
local _, err = pcall(log.cfg, {level='fatal'})
test:ok(err == nil and log.cfg.level == 'fatal', 'got fatal')

_, err = pcall(log.cfg, {level='syserror'})
test:ok(err == nil and log.cfg.level == 'syserror', 'got syserror')

_, err = pcall(log.cfg, {level='error'})
test:ok(err == nil and log.cfg.level == 'error', 'got error')

_, err = pcall(log.cfg, {level='crit'})
test:ok(err == nil and log.cfg.level == 'crit', 'got crit')

_, err = pcall(log.cfg, {level='warn'})
test:ok(err == nil and log.cfg.level == 'warn', 'got warn')

_, err = pcall(log.cfg, {level='info'})
test:ok(err == nil and log.cfg.level == 'info', 'got info')

_, err = pcall(log.cfg, {level='verbose'})
test:ok(err == nil and log.cfg.level == 'verbose', 'got verbose')

_, err = pcall(log.cfg, {level='debug'})
test:ok(err == nil and log.cfg.level == 'debug', 'got debug')

os.exit(test:check() and 0 or 1)
