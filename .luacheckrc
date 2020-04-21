std = "luajit"

include_files = {
    "**/*.lua",
}

exclude_files = {
    "build/**/*.lua",
    "extra/**/*.lua",
    "src/**/*.lua",
    "test-run/**/*.lua",
    "test/**/*.lua",
    "third_party/**/*.lua",
    ".rocks/**/*.lua",
    ".git/**/*.lua",
}
