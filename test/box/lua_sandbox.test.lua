--
-- Test that some built-in functions were disabled by sandbox
--
os.execute
os.exit
os.rename
os.tmpname
os.remove
io
require
package
-- FFI can be mistakenly saved to the global variable by the one of our modules
ffi
