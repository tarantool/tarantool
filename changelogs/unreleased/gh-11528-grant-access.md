## feature/box

* Introduced new `grant` and `metagrant` privileges. They're only grantable on
  the `universe`. The `grant` privilege allows a user to grant any privilege,
  excepting `grant` and `metagrant`, on an object, object class or `universe`.
  The `metagrant` allows to grant `grant` and `metagrant` privileges. Both only
  allow granting to other users, no granting oneself allowed by them (gh-11528).
