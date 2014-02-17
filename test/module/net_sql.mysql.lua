package.path  = "../../src/module/sql/?.lua;"..package.path
require("sql")
if type(box.net.sql) ~= "table" then
	error("net.sql load failed")
end
require("box.net.mysql")
