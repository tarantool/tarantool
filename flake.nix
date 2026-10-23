{
  description = "Tarantool: dev shell, packages and tests";

  inputs = {
    # Tarantool has many git submodules (LuaJIT, small, libyaml, ...)
    # that are required to configure and build it. Declaring this makes
    # `self' include their contents. Requires Nix >= 2.27.
    self.submodules = true;

    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    flake-utils.url = "github:numtide/flake-utils";

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
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      emmylua,
    }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # Nix does not run git and exposes no git tags, so the version is
        # derived from the commit hash only. CMake requires at least four
        # components (see CMakeLists.txt and lib/CMakeLists.txt), hence
        # the `0.0.0-' prefix that marks a non-release build.
        # `dirtyShortRev' already carries the `-dirty' suffix.
        version = "0.0.0-${self.dirtyShortRev or self.shortRev or "unknown"}";

        devShell = import ./shell.nix {
          inherit pkgs;
          emmyluaCheck = emmylua.packages.${system}.emmylua_check;
          emmyluaDocCli = emmylua.packages.${system}.emmylua_doc_cli;
        };

        tarantool = pkgs.stdenv.mkDerivation (finalAttrs: {
          pname = "tarantool";
          inherit version;
          src = self;

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
            # There is no .git in the store, so the version must be passed
            # explicitly, otherwise configure fails.
            "-DTARANTOOL_VERSION=${finalAttrs.version}.builtByNix"
          ];

          enableParallelBuilding = true;

          meta = with pkgs.lib; {
            description = "In-memory computing platform consisting of a database and an application server";
            homepage = "https://www.tarantool.io/";
            license = licenses.bsd2;
            mainProgram = "tarantool";
            platforms = [
              "x86_64-linux"
              "aarch64-linux"
            ];
          };
        });

      in
      {
        packages = {
          inherit tarantool;
          default = tarantool;
        };

        devShells.default = devShell;
      }
    );
}
