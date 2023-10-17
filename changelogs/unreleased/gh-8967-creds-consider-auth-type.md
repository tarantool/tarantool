## feature/config

* Now a password hash (and salt) will be regenerated for users managed
  in the configuration file if `security.auth_type` differs from a user's
  `auth_type` (gh-8967).
