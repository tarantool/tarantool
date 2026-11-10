# Nix development shell for Tarantool,
# https://www.tarantool.io/en/doc/latest/contributing/building_from_source/
#
# Usage without flakes:
#   nix-shell              # Enter development environment.
#   nix-shell --pure       # Enter pure (isolated) environment.
#
# With flakes, `nix develop' imports this file; see flake.nix.
#
{
  pkgs ? import <nixpkgs> { },
  # Taken from the upstream flake through flake.nix to get a version
  # newer than the one in nixpkgs. The defaults keep `nix-shell` usable.
  emmyluaCheck ? pkgs.emmylua-check,
  emmyluaDocCli ? pkgs.emmylua-doc-cli,
  # Taken from the upstream flake through flake.nix. The default keeps
  # `nix-shell` usable with the same pinned revision.
  checkpatchSrc ? pkgs.fetchFromGitHub {
    owner = "tarantool";
    repo = "checkpatch";
    rev = "4801da8d408fc4c511c690c3575768b9a7c5810c";
    hash = "sha256-P3WUKgJl6NLNPPqOi0YTLNuEyh6n2J8ZpU3AWMP0cfo=";
  },
}:

let
  # The common symbolizer used by sanitizers to symbolize reports.
  # Sanitizers look for the `llvm-symbolizer' binary in PATH, which
  # is provided by the `llvm' package (see buildInputs below).
  symbolizer = "${pkgs.llvmPackages_latest.llvm}/bin/llvm-symbolizer";
  ccLibPath = pkgs.lib.makeLibraryPath [ pkgs.stdenv.cc.cc.lib ];
  pythonEnv = pkgs.python3.withPackages (
    # numpy and scipy are needed by perf/tools/compare/compare.py,
    # gevent, pyyaml and six are needed by test-run/test-run.py.
    ps: with ps; [
      gevent
      numpy
      pyyaml
      scipy
      six
    ]
  );

  # checkpatch.pl is not packaged in nixpkgs, so take the Tarantool fork
  # used by CI. `--codespellfile' is passed explicitly because the script
  # otherwise looks for the dictionary under /usr/share, which does not
  # exist in Nix.
  codespellDict = pkgs.lib.concatStrings [
    "${pkgs.codespell}/${pkgs.python3.sitePackages}"
    "/codespell_lib/data/dictionary.txt"
  ];
  checkpatch = pkgs.writeShellScriptBin "checkpatch.pl" ''
    exec ${pkgs.perl}/bin/perl ${checkpatchSrc}/checkpatch.pl \
      --codespellfile ${codespellDict} \
      "$@"
  '';

  # Build dependencies shared with the Tarantool derivations in
  # flake.nix, which reuse them through `passthru.buildDeps'
  # below. The commented CMake options select the system libraries;
  # for a shared build most of them are the default.
  buildDeps = {
    nativeBuildInputs = with pkgs; [
      cmake
      git # third_party/luajit/cmake/SetVersion.cmake
    ];
    buildInputs = with pkgs; [
      c-ares # Used by the bundled libcurl sub-build.
      icu # -DENABLE_BUNDLED_ICU=OFF
      nghttp2 # Used by the bundled libcurl sub-build.
      openssl # -DENABLE_BUNDLED_OPENSSL=OFF
      readline # -DENABLE_BUNDLED_READLINE=OFF
      zlib # -DENABLE_BUNDLED_ZLIB=OFF
    ];
  };

  # Extra Tarantool dependencies for the development shell. They
  # are not required to build the `tarantool` package because
  # Tarantool bundles libcurl and libyaml, and on Linux iconv
  # comes from libc. Providing them lets the bundled libraries be
  # replaced with the system ones via the commented CMake options.
  systemLibs = with pkgs; [
    curl # -DENABLE_BUNDLED_LIBCURL=OFF
    libiconv # -DENABLE_BUNDLED_ICONV=OFF
    libyaml # -DENABLE_BUNDLED_LIBYAML=OFF
  ];
in
pkgs.mkShell {
  name = "tarantool-dev";

  inherit (buildDeps) nativeBuildInputs;

  buildInputs =
    buildDeps.buildInputs
    ++ systemLibs
    ++ (with pkgs; [
      checkpatch
      codespell
      doxygen
      emmyluaCheck
      emmyluaDocCli
      gbenchmark
      gcc
      gnumake
      lcov
      llvmPackages_latest.clang
      llvmPackages_latest.llvm
      luajitPackages.luacheck
      ninja
      stdenv.cc.cc.lib

      # Python environment with packages required for regression
      # testing.
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

  # luatest is shipped as the `luatest` git submodule and is wired up
  # by test-run's wrapper, which injects ../luatest into package.path.
  # Export the same paths so that the `luatest` command and
  # require('luatest') work in the shell.
  shellHook = ''
    REPO=$(git rev-parse --show-toplevel 2>/dev/null) || REPO=$PWD

    # The nixpkgs cmake hook only passes the build input prefixes to
    # cmakeConfigurePhase, so export them for a manually run cmake.
    # Config-mode packages (benchmark, for instance) need this.
    cmake_prefix_path="$NIXPKGS_CMAKE_PREFIX_PATH"
    if [ -n "''${CMAKE_PREFIX_PATH:-}" ]; then
      cmake_prefix_path="$cmake_prefix_path:$CMAKE_PREFIX_PATH"
    fi
    export CMAKE_PREFIX_PATH="$cmake_prefix_path"

    export PATH="$REPO/test-run/bin:$PATH"
    export CTEST_PARALLEL_LEVEL=$(getconf _NPROCESSORS_ONLN)
    export LUA_PATH="''${LUA_PATH:-;}"
    export LUA_PATH="$REPO/luatest/?.lua;$REPO/luatest/?/init.lua;$LUA_PATH"
    export PYTHONPATH="$REPO/src:$PYTHONPATH"
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
