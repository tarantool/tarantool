## How to add a new upgrade test

Firstly, for a new test should be prepared a `fill.lua` file. Here
should be code to populate needed spaces, create xlogs, snapshots.

`Fill.lua` should be run in an old Tarantool version, upgrade from
which is going to be tested. This will produce `.xlog` and `.snap`
files. They should be saved into
`xlog/upgrade/<version>/<test_name>/` folder. Snapshot is usually
enough. Version is the old Tarantool version. Test name is a name
of test file, which will test recovery from these xlogs. It should
obey to the same rules as other test file names.

Secondly, it is necessary to add a `<test_name>.test.lua` file to
`xlog/` folder, to all the other tests. It should start a new
instance of Tarantool with workdir set to
`xlog/upgrade/<version>/<test_name>/`. Then this file should
ensure, that `box.upgrade()` works fine, and all data is recovered
correctly.

The following directory structure should be in the result:
```
 xlog/
 |
 +- <test_name>.test.lua
 |
 +- upgrade/
    |
    +- <version>/
        |
        +- <test_name>/
           |
           +- fill.lua
           +- *.snap
           +- *.xlog
```
