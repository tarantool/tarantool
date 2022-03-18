std = "luajit"
globals = {"box", "_TARANTOOL", "tonumber64", "utf8"}
ignore = {
    -- Accessing an undefined field of a global variable <debug>.
    "143/debug",
    -- Accessing an undefined field of a global variable <os>.
    "143/os",
    -- Accessing an undefined field of a global variable <string>.
    "143/string",
    -- Accessing an undefined field of a global variable <table>.
    "143/table",
    -- Unused argument <self>.
    "212/self",
    -- Redefining a local variable.
    "411",
    -- Redefining an argument.
    "412",
    -- Shadowing a local variable.
    "421",
    -- Shadowing an upvalue.
    "431",
    -- Shadowing an upvalue argument.
    "432",
}

include_files = {
    "**/*.lua",
    "extra/dist/tarantoolctl.in",
}

exclude_files = {
    "build/**/*.lua",
    "test-run/**/*.lua",
    "test/app/*.test.lua",
    "test/box/*.test.lua",
    "test/engine/*.test.lua",
    "test/engine_long/*.test.lua",
    "test/replication/*.test.lua",
    "test/sql/**/*.lua",
    "test/swim/*.test.lua",
    "test/var/**/*.lua",
    "test/vinyl/*.test.lua",
    "test/wal_off/*.test.lua",
    "test/xlog/*.test.lua",
    "third_party/**/*.lua",
    ".rocks/**/*.lua",
    ".git/**/*.lua",
}

files["test/sql-tap/**/*.lua"] = {
    ignore = {
        -- Line is too long.
        -- https://github.com/tarantool/tarantool/issues/5181
        "631"
    }
}

files["src/lua/help.lua"] = {
    -- Globals defined for interactive mode.
    globals = {"help", "tutorial"},
}
files["src/lua/init.lua"] = {
    -- Miscellaneous global function definition.
    globals = {"dostring"},
}
files["src/box/lua/console.lua"] = {
    ignore = {
        -- https://github.com/tarantool/tarantool/issues/5032
        "212",
    }
}
files["test/box/box.lua"] = {
    globals = {
        "cfg_filter",
        "sorted",
        "iproto_request",
    }
}
files["test/box/gh-5645-several-iproto-threads.lua"] = {
    globals = {
        "errinj_set",
        "ping",
    },
}
files["test/box-tap/session.test.lua"] = {
    globals = {
        "session",
        "space",
        "f1",
        "f2",
    },
}
files["test/box-tap/extended_error.test.lua"] = {
    globals = {
        "error_access_denied",
        "error_new",
        "error_new_stacked",
        "error_throw",
        "error_throw_stacked",
        "error_throw_access_denied",
        "forbidden_function",
    },
}
files["test/swim/box.lua"] = {
    globals = {
        "listen_port",
        "listen_uri",
        "uuid",
        "uri",
    }
}
files["test/replication/replica_quorum.lua"] = {
    globals = {
        "INSTANCE_URI",
        "nonexistent_uri",
    },
}
