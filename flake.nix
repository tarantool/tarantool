# Tarantool flake: development shell, packages and tests.
#
# Usage (x86_64-linux or aarch64-linux, Nix >= 2.27):
#   nix develop                   # Enter the development environment.
#   nix build                     # Build the selected Tarantool release.
#   nix run                       # Build it and run the binary.
#   nix shell .#tarantool-master  # Add the current checkout to PATH.
#   nix build .#tarantool-master  # Build the current checkout.
#
# The tarantool-master build only sees git-tracked files (including the
# submodules), so initialize the submodules
# (`git submodule update --init --recursive') and `git add' new files
# first. Use `nix develop' for incremental builds.
{
  description = "Tarantool: dev shell, packages and tests";

  inputs = {
    # Tarantool has many git submodules (LuaJIT, small, libyaml, ...)
    # that are required to configure and build it. Declaring this makes
    # `self' include their contents. Requires Nix >= 2.27.
    self.submodules = true;

    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    flake-utils.url = "github:numtide/flake-utils";

    # Tarantool release built by the default package. The tag is selected
    # by `ref' below; flake.lock records the exact revision and hash, and
    # flake.nix takes the version from there. To change the release, edit
    # `ref' and run `nix flake lock'.
    tarantool-release = {
      type = "git";
      url = "https://github.com/tarantool/tarantool.git";
      ref = "refs/tags/3.9.0";
      submodules = true;
      flake = false;
    };

    # nixpkgs 26.05 ships emmylua-check 0.23.10 and
    # emmylua-doc-cli 0.23.1, while the CI lint job uses 0.25.1,
    # so track the upstream release to keep local and CI results
    # identical. Following the root nixpkgs and flake-utils avoids
    # pulling a second copy of them.
    emmylua = {
      url = "github:EmmyLuaLs/emmylua-analyzer-rust/0.25.1";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-utils.follows = "flake-utils";
    };

    # checkpatch.pl is not packaged in nixpkgs, so use the Tarantool fork
    # used by CI. The repository has no flake.nix. The revision is pinned
    # by flake.lock and advanced by `nix flake update checkpatch'.
    checkpatch = {
      url = "github:tarantool/checkpatch";
      flake = false;
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      tarantool-release,
      emmylua,
      checkpatch,
    }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # Fetcher metadata omits the tag name, which is kept in the lock file.
        lock = builtins.fromJSON (builtins.readFile ./flake.lock);
        releaseNode = lock.nodes.${lock.root}.inputs.tarantool-release;
        releaseInput = lock.nodes.${releaseNode}.locked;
        releaseTagPattern = "refs/tags/[0-9]+\\.[0-9]+\\.[0-9]+";
        releaseVersion =
          assert pkgs.lib.assertMsg (
            builtins.match releaseTagPattern releaseInput.ref != null
            && releaseInput.rev == tarantool-release.rev
          ) "tarantool-release must match the release tag in flake.lock";
          pkgs.lib.removePrefix "refs/tags/" releaseInput.ref;

        devShell = import ./shell.nix {
          inherit pkgs;
          checkpatchSrc = checkpatch;
          emmyluaCheck = emmylua.packages.${system}.emmylua_check;
          emmyluaDocCli = emmylua.packages.${system}.emmylua_doc_cli;
        };

        tarantoolRelease = pkgs.stdenv.mkDerivation (finalAttrs: {
          pname = "tarantool";
          version = releaseVersion;
          src = tarantool-release;

          # Git inputs omit .git. At a release tag the commit distance is
          # zero, so describe it the way `git describe --long' would.
          tarantoolVersion = pkgs.lib.concatStrings [
            "${finalAttrs.version}-0-g"
            tarantool-release.shortRev
          ];

          # LuaJIT's test target is configured unconditionally and reads
          # /etc/os-release at configure time, which does not exist in the
          # Nix build sandbox. Replace the distro detection macro.
          postPatch = ''
            cat > third_party/luajit/test/cmake/GetLinuxDistro.cmake <<'EOF'
            macro(GetLinuxDistro output)
              set(''${output} linux)
            endmacro()
            EOF
          '';

          inherit (devShell.buildDeps) nativeBuildInputs buildInputs;

          cmakeBuildType = "RelWithDebInfo";

          cmakeFlags = [
            "-DENABLE_DIST=ON"
            # mimalloc has no system-library fallback yet; disable it
            # instead of fetching the tarball at build time. See #13644.
            "-DENABLE_BUNDLED_MIMALLOC=OFF"
            # libunwind is only needed for backtraces; disable backtraces
            # instead of fetching the tarball at build time. See #13644.
            "-DENABLE_BACKTRACE=OFF"
            # There is no .git in the store, so the version must be passed
            # explicitly, otherwise configure fails.
            "-DTARANTOOL_VERSION=${finalAttrs.tarantoolVersion}"
          ];

          enableParallelBuilding = true;

          meta = with pkgs.lib; {
            description = concatStrings [
              "In-memory computing platform consisting of a database "
              "and an application server"
            ];
            homepage = "https://www.tarantool.io/";
            license = licenses.bsd2;
            mainProgram = "tarantool";
            platforms = [
              "x86_64-linux"
              "aarch64-linux"
            ];
          };
        });

        # tarantool-master is built from the current checkout, which has no
        # Git history in the store, so it is versioned as 0.0.0-<rev>. See
        # the tarantool-master package below for the resulting limitations.
        rev = self.dirtyShortRev or self.shortRev or "unknown";
        tarantoolMaster = tarantoolRelease.overrideAttrs (
          finalAttrs: _: {
            version = "0.0.0-${rev}";
            src = self;
            tarantoolVersion = "${finalAttrs.version}.builtByNix";
          }
        );

      in
      {
        packages = {
          tarantool = tarantoolRelease;
          # A development build from the current checkout. Nix does not
          # provide Git history, so the binary reports version 0.0.0 and
          # some version-dependent features are degraded.
          tarantool-master = pkgs.lib.warn ''
            tarantool-master reports version 0.0.0 (the Nix store has no
            Git history) and is not fully functional:
              - Replication from it fails with a nil UUID error.
              - SWIM peers reject its packets, since version 0 is invalid;
              - box.schema.downgrade() and downgrade_issues() fail, because
                the binary version is older than the schema version;
              - clients connecting to it get no IPROTO features: no watches,
                no pagination, and no space/index names without a schema
                fetch;
              - config `if' conditions and app/role `fail_if' checks see
                0.0.0;
              - require('version').fromstr(box.info.version) fails.
            Use the `tarantool' package for a functional build.
          '' tarantoolMaster;
          default = tarantoolRelease;
        };

        devShells.default = devShell;
      }
    );
}
