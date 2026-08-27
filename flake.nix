{
  description = "Tarantool: dev shell, packages and tests";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        devShell = import ./shell.nix { inherit pkgs; };

        tarantool = pkgs.stdenv.mkDerivation (finalAttrs: {
          pname = "tarantool";
          version = "3.8.1";

          # Latest release tag. Submodules are fetched too.
          src = pkgs.fetchFromGitHub {
            owner = "tarantool";
            repo = "tarantool";
            tag = "3.8.1";
            hash = "sha256-ZdGbBOp34H+n0DHn+Ocbj2T5Kzta+mQSEhu7pRrQy64=";
            fetchSubmodules = true;
          };

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
