#!/usr/bin/env tarantool_box
box.load_cfg()
mod = require("require_mod")
package_path = package.path
package_cpath = package.cpath
