# Nix development shell for Tarantool,
# https://www.tarantool.io/en/doc/latest/contributing/building_from_source/
#
# Usage:
#   nix-shell              # Enter development environment.
#   nix-shell --pure       # Enter pure (isolated) environment.
#
{
  pkgs ? import <nixpkgs> { },
}:

let
  # The common symbolizer used by sanitizers to symbolize reports.
  # Sanitizers look for the `llvm-symbolizer' binary in PATH, which
  # is provided by the `llvm' package (see buildInputs below).
  symbolizer = "${pkgs.llvmPackages_latest.llvm}/bin/llvm-symbolizer";
  ccLibPath = pkgs.lib.makeLibraryPath [ pkgs.stdenv.cc.cc.lib ];
  pythonEnv = pkgs.python3.withPackages (
    ps: with ps; [
      gevent
      pyyaml
      six
    ]
  );

  # Build dependencies shared with the `tarantool` derivation in
  # flake.nix, which reuses them through `passthru.buildDeps`
  # below. The commented CMake options make Tarantool use these
  # system libraries instead of the bundled ones.
  buildDeps = {
    nativeBuildInputs = with pkgs; [
      autoconf # Builds the bundled libunwind via autotools.
      automake
      cmake
      libtool
    ];
    buildInputs = with pkgs; [
      c-ares # -DBUNDLED_LIBCURL_USE_ARES=ON
      git # third_party/luajit/cmake/SetVersion.cmake
      icu # -DENABLE_BUNDLED_ICU=ON
      nghttp2 # -DBUNDLED_LIBCURL_USE_NGHTTP2=ON
      openssl # -DENABLE_BUNDLED_OPENSSL=ON
      readline # -DENABLE_BUNDLED_READLINE=ON
      zlib # -DENABLE_BUNDLED_ZLIB=ON
    ];
  };

  # Extra Tarantool dependencies for the development shell. They
  # are not required to build the `tarantool` package because
  # Tarantool bundles libcurl and libyaml, and on Linux iconv
  # comes from libc. Providing them lets the bundled libraries be
  # replaced with the system ones via the commented CMake options.
  systemLibs = with pkgs; [
    curl # -DENABLE_BUNDLED_LIBCURL=ON
    libiconv # -DENABLE_BUNDLED_ICONV=ON
    libyaml # -DENABLE_BUNDLED_LIBYAML=ON
  ];
in
pkgs.mkShell {
  name = "tarantool-dev";

  inherit (buildDeps) nativeBuildInputs;

  buildInputs =
    buildDeps.buildInputs
    ++ systemLibs
    ++ (with pkgs; [
      emmylua-check
      gcc
      gnumake
      llvmPackages_latest.clang
      llvmPackages_latest.llvm
      ninja
      stdenv.cc.cc.lib

      # Python environment with packages required for regression
      # testing.
      python3
      pythonEnv
    ]);

  passthru.buildDeps = buildDeps;

  env = {
    LD_LIBRARY_PATH = ccLibPath;

    # clang finds the GCC toolchain but not libstdc++.so from the
    # separate gcc-*-lib output. LuaJIT's ASan test builds LD_PRELOAD
    # via `cc -print-file-name=libstdc++.so`, which needs it.
    COMPILER_PATH = ccLibPath;

    # Point sanitizers (ASan, MSan, UBSan) to the LLVM symbolizer,
    # so that error reports contain function names and line numbers
    # even if PATH is changed within a session.
    MSAN_SYMBOLIZER_PATH = symbolizer;
    ASAN_SYMBOLIZER_PATH = symbolizer;
    UBSAN_SYMBOLIZER_PATH = symbolizer;
  };

  NIX_CFLAGS_COMPILE = "-I${pkgs.gcc}/include/c++/${pkgs.gcc.version}";
  NIX_LDFLAGS = "-L${pkgs.gcc}/lib";

  # luatest is shipped as the `luatest` git submodule and is wired up
  # by test-run's wrapper, which injects ../luatest into package.path.
  # Export the same paths so that the `luatest` command and
  # require('luatest') work in the shell.
  shellHook = ''
    export PATH=$PWD/test-run/bin:$PATH
    export CTEST_PARALLEL_LEVEL=$(getconf _NPROCESSORS_ONLN)
    export LUA_PATH="$PWD/luatest/?.lua;$PWD/luatest/?/init.lua;''${LUA_PATH:-;}"
    export PYTHONPATH=$PWD/src:$PYTHONPATH
    echo
    echo "Tarantool Development Environment"
    echo
    echo "List all available CMake workflow presets:"
    echo "  cmake --workflow --list-presets"
    echo
    echo "Execute a predefined sequence of CMake actions:"
    echo "  cmake --workflow --preset clang_debug_asan"
  '';
}
