package.path  = os.getenv("TARANTOOL_SRC_DIR").."/src/module/sql/?.lua"
package.cpath  = "?.so"

require("sql")
if type(box.net.sql) ~= "table" then
	error("net.sql load failed")
end
require("box.net.pg")
