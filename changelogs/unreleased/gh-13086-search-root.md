## bugfix/config

* Configuration startup now preserves an explicitly pinned module search root,
  including one equal to the startup directory. An unset root follows the
  current directory again after temporary workdir-based module lookup, even
  when initial configuration validation fails (gh-13086).
