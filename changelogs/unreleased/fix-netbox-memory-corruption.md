## bugfix/core

* Fixed memory corruption in netbox. Because of the wrong order of the ffi.gc
  and ffi.cast calls memory of struct error, which was still used, was freed