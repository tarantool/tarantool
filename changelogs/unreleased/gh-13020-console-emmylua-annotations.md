## feature/lua

* Added EmmyLua annotations for the `console` module, so IDEs can
  complete its remote-console and admin-port functions (gh-13020).
* `console.get_default_output()` is now declared without arguments;
  extra arguments are ignored instead of raising an error (gh-13020).
