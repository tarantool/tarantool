## bugfix/luajit

Backported patches from vanilla LuaJIT trunk (gh-8069). In the scope of this
activity, the following issues have been resolved:

* Fixed loop realigment for dual-number mode
* Fixed os.date() for wider libc strftime() compatibility.
