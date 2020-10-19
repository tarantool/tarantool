std = "luajit"
globals = {"box", "_TARANTOOL", "tonumber64"}
ignore = {
    -- Accessing an undefined field of a global variable <debug>.
    "143/debug",
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
    -- Third-party source code.
    "src/box/lua/serpent.lua",
    "test-run/**/*.lua",
    "test/app/**/*.lua",
    "test/app-tap/**/*.lua",
    "test/box/**/*.lua",
    "test/box-py/**/*.lua",
    "test/box-tap/**/*.lua",
    "test/engine/**/*.lua",
    "test/engine_long/**/*.lua",
    "test/long_run-py/**/*.lua",
    "test/luajit-tap/**/*.lua",
    "test/replication/**/*.lua",
    "test/replication-py/**/*.lua",
    "test/sql/**/*.lua",
    "test/swim/**/*.lua",
    "test/var/**/*.lua",
    "test/vinyl/**/*.lua",
    "test/wal_off/**/*.lua",
    "test/xlog/**/*.lua",
    "test/xlog-py/**/*.lua",
    "third_party/**/*.lua",
    ".rocks/**/*.lua",
    ".git/**/*.lua",
}

files["test/sql-tap/**/*.lua"] = {
    ignore = {
        -- Accessing an undefined global variable.
        "113",
        -- Unused local variable.
        "211",
        -- Unused argument.
        "212",
        -- Unused loop variable.
        "213",
        -- Local variable is set but never accessed.
        "231",
        -- "Value assigned to a local variable is unused."
        "311",
        -- Unreachable code.
        "511",
        -- Loop can be executed at most once.
        "512",
        -- An empty if branch.
        "542",
        -- A line consists of nothing but whitespace.
        "611",
        -- A line contains trailing whitespace.
        "612",
        -- Trailing whitespace in a string.
        "613",
        -- Trailing whitespace in a comment.
        "614",
        -- Inconsistent indentation (SPACE followed by TAB).
        "621",
        -- Line is too long.
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
