# ALEX

Header-only C++ implementation of ALEX, an updatable adaptive learned index,
taken from https://github.com/microsoft/ALEX at commit
4370da6aa8b509fdc9b0d2c49faa0624b0078589 (MIT license, see `LICENSE`).

Only the core headers from `src/core/` are kept: `alex.h`, `alex_base.h`,
`alex_nodes.h`, `alex_fanout_tree.h`.

Local changes:

- `alex_base.h`: the x86 intrinsics (`_mm_popcnt_u64`, `_lzcnt_u64`,
  `_tzcnt_u64`) and the CPUID-based `cpu_supports_bmi()` are replaced with
  portable GCC/Clang builtins, so the library builds without `-mpopcnt`,
  `-mlzcnt`, `-mbmi` and on non-x86 platforms.
