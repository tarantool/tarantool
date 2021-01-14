## feature/core

* Added granularity option to box.cfg{}. Granularity is an option
  that allows user to set multiplicity of memory allocation in small
  allocator. Granulatiry must be exponent of two and >= 4 (gh-5518).
