## bugfix/box

* Fixed an incorrect error message on granting privileges to the `admin` user.
  Such attempts now fail with proper error messages such as "User 'admin'
  already has read access on universe" (gh-7226).
