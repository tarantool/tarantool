## bugfix/core

* Fixed a bug when a space creation failed with a duplication error.
  The issue occurred if the explicit and implicit space IDs were mixed.
  Now the actual maximal space `id` is used to generate a new one (gh-8036).
