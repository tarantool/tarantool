## feature/core

* Added a dynamic validation mechanism to fiber_new_ex() that limits
the number of client fibers based on vm.max_map_count. This prevents
excessive fiber creation and potential memory allocation failures.
The validation ensures that the number of client fibers does not
exceed 45% of available mappings.
