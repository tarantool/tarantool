## feature/core

* Added slab_alloc_granularity option to box.cfg{}. This option allows
  user to set multiplicity of memory allocation in small allocator.
  slab_alloc_granularity must be exponent of two and >= 4 (gh-5518).
