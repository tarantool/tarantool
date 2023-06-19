## feature/box

* Changed the output of the `fselect` index method from a table of strings to
  a multi-line string and dropped the `print` and `use_nbsp` options. With the
  `yaml_pretty_multiline` compat option enabled by default, multi-line strings
  now look good in the console, so there's no need to return a table of strings
  to prettify the `fselect` output anymore.
