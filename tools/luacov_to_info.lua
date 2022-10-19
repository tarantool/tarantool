#!/usr/bin/env luajit

local luacov = require('luacov')
local ReporterBase = require('luacov.reporter').ReporterBase
local LcovReporter = setmetatable({}, ReporterBase)
LcovReporter.__index = LcovReporter

function LcovReporter:on_new_file(filename)
	self.finfo = self.current_files[filename] or {name=filename, coverage={}}
end

function LcovReporter:on_mis_line(_, lineno, _)
	self.finfo.coverage[lineno] = self.finfo.coverage[lineno] or 0
end

function LcovReporter:on_hit_line(_, lineno, _, hits)
	self.finfo.coverage[lineno] = (self.finfo.coverage[lineno] or 0) + hits
end

function LcovReporter:on_end_file()
	self.current_files[self.finfo.name] = self.finfo
	self.finfo = nil
end

-- Write out results in lcov format
local function write_lcov_info(files)
	for fname, finfo in pairs(files) do
		local instrumented, nonzero = 0, 0
		print('TN:')
		print(string.format('SF:%s', fname))
		for i, hits in pairs(finfo.coverage) do
			print(string.format('DA:%d,%d', i, hits))
			instrumented = instrumented + 1
			if hits > 0 then
				nonzero = nonzero + 1
			end
		end
		print(string.format('LH:%d', nonzero))
		print(string.format('LF:%d', instrumented))
		print('end_of_record')
	end
end

-- Accumulate total coverage.
local all_files = {}
for _, fname in ipairs(arg) do
	local conf = luacov.load_config()
	conf.statsfile = fname
	local reporter = assert(LcovReporter:new(conf))
	reporter.current_files = all_files
	reporter:run()
	reporter:close()
end

-- Write results.
write_lcov_info(all_files)
