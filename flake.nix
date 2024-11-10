{
  nixConfig = {
    extra-substituters = "https://cache.privatevoid.net";
    extra-trusted-public-keys = "cache.privatevoid.net:SErQ8bvNWANeAvtsOESUwVYr2VJynfuc9JRwlzTTkVg=";
  };

  description = "The purely functional package manager - but super!";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
  inputs.nixpkgs-regression.url = "github:NixOS/nixpkgs/215d4d0fd80ca5163643b03a33fde804a29cc1e2";
  inputs.nixpkgs-23-11.url = "github:NixOS/nixpkgs/a62e6edd6d5e1fa0329b8653c801147986f8d446";
  inputs.flake-compat = { url = "github:edolstra/flake-compat"; flake = false; };
  inputs.libgit2 = { url = "github:libgit2/libgit2/v1.8.1"; flake = false; };

  # dev tooling
  inputs.flake-parts.url = "github:hercules-ci/flake-parts";
  inputs.git-hooks-nix.url = "github:cachix/git-hooks.nix";
  # work around https://github.com/NixOS/nix/issues/7730
  inputs.flake-parts.inputs.nixpkgs-lib.follows = "nixpkgs";
  inputs.git-hooks-nix.inputs.nixpkgs.follows = "nixpkgs";
  inputs.git-hooks-nix.inputs.nixpkgs-stable.follows = "nixpkgs";
  # work around 7730 and https://github.com/NixOS/nix/issues/7807
  inputs.git-hooks-nix.inputs.flake-compat.follows = "";
  inputs.git-hooks-nix.inputs.gitignore.follows = "";

  outputs = inputs@{ self, nixpkgs, nixpkgs-regression, libgit2, ... }:


    let
      inherit (nixpkgs) lib;

      officialRelease = false;

      linux32BitSystems = [ "i686-linux" ];
      linux64BitSystems = [ "x86_64-linux" "aarch64-linux" ];
      linuxSystems = linux32BitSystems ++ linux64BitSystems;
      darwinSystems = [ "x86_64-darwin" "aarch64-darwin" ];
      systems = linuxSystems ++ darwinSystems;

      crossSystems = [
        "armv6l-unknown-linux-gnueabihf"
        "armv7l-unknown-linux-gnueabihf"
        "riscv64-unknown-linux-gnu"
        "x86_64-unknown-netbsd"
        "x86_64-unknown-freebsd"
        "x86_64-w64-mingw32"
      ];

      stdenvs = [
        "ccacheStdenv"
        "clangStdenv"
        "gccStdenv"
        "libcxxStdenv"
        "stdenv"
      ];

      /**
        `flatMapAttrs attrs f` applies `f` to each attribute in `attrs` and
        merges the results into a single attribute set.

        This can be nested to form a build matrix where all the attributes
        generated by the innermost `f` are returned as is.
        (Provided that the names are unique.)

        See https://nixos.org/manual/nixpkgs/stable/index.html#function-library-lib.attrsets.concatMapAttrs
       */
      flatMapAttrs = attrs: f: lib.concatMapAttrs f attrs;

      forAllSystems = lib.genAttrs systems;

      forAllCrossSystems = lib.genAttrs crossSystems;

      forAllStdenvs = f:
        lib.listToAttrs
          (map
            (stdenvName: {
              name = "${stdenvName}Packages";
              value = f stdenvName;
            })
            stdenvs);


      # We don't apply flake-parts to the whole flake so that non-development attributes
      # load without fetching any development inputs.
      devFlake = inputs.flake-parts.lib.mkFlake { inherit inputs; } {
        imports = [ ./maintainers/flake-module.nix ];
        systems = lib.subtractLists crossSystems systems;
        perSystem = { system, ... }: {
          _module.args.pkgs = nixpkgsFor.${system}.native;
        };
      };

      # Memoize nixpkgs for different platforms for efficiency.
      nixpkgsFor = forAllSystems
        (system: let
          make-pkgs = crossSystem: stdenv: import nixpkgs {
            localSystem = {
              inherit system;
            };
            crossSystem = if crossSystem == null then null else {
              config = crossSystem;
            } // lib.optionalAttrs (crossSystem == "x86_64-unknown-freebsd13") {
              useLLVM = true;
            };
            overlays = [
              (overlayFor (p: p.${stdenv}))
            ];
          };
          stdenvs = forAllStdenvs (make-pkgs null);
          native = stdenvs.stdenvPackages;
        in {
          inherit stdenvs native;
          static = native.pkgsStatic;
          cross = forAllCrossSystems (crossSystem: make-pkgs crossSystem "stdenv");
        });

      binaryTarball = nix: pkgs: pkgs.callPackage ./scripts/binary-tarball.nix {
        inherit nix;
      };

      overlayFor = getStdenv: final: prev:
        let
          stdenv = getStdenv final;
        in
        {
          nixStable = prev.nix;

          # A new scope, so that we can use `callPackage` to inject our own interdependencies
          # without "polluting" the top level "`pkgs`" attrset.
          # This also has the benefit of providing us with a distinct set of packages
          # we can iterate over.
          nixComponents = lib.makeScope final.nixDependencies.newScope (import ./packaging/components.nix {
            inherit (final) lib;
            inherit officialRelease;
            src = self;
          });

          # The dependencies are in their own scope, so that they don't have to be
          # in Nixpkgs top level `pkgs` or `nixComponents`.
          nixDependencies = lib.makeScope final.newScope (import ./packaging/dependencies.nix {
            inherit inputs stdenv;
            pkgs = final;
          });

          nix = final.nixComponents.nix;

          # See https://github.com/NixOS/nixpkgs/pull/214409
          # Remove when fixed in this flake's nixpkgs
          pre-commit =
            if prev.stdenv.hostPlatform.system == "i686-linux"
            then (prev.pre-commit.override (o: { dotnet-sdk = ""; })).overridePythonAttrs (o: { doCheck = false; })
            else prev.pre-commit;

        };

    in {
      # A Nixpkgs overlay that overrides the 'nix' and
      # 'nix-perl-bindings' packages.
      overlays.default = overlayFor (p: p.stdenv);

      hydraJobs = import ./packaging/hydra.nix {
        inherit
          inputs
          binaryTarball
          forAllCrossSystems
          forAllSystems
          lib
          linux64BitSystems
          nixpkgsFor
          self
          officialRelease
          ;
      };

      checks = forAllSystems (system: {
        binaryTarball = self.hydraJobs.binaryTarball.${system};
        installTests = self.hydraJobs.installTests.${system};
        nixpkgsLibTests = self.hydraJobs.tests.nixpkgsLibTests.${system};
        rl-next =
          let pkgs = nixpkgsFor.${system}.native;
          in pkgs.buildPackages.runCommand "test-rl-next-release-notes" { } ''
          LANG=C.UTF-8 ${pkgs.changelog-d}/bin/changelog-d ${./doc/manual/rl-next} >$out
        '';
        repl-completion = nixpkgsFor.${system}.native.callPackage ./tests/repl-completion.nix { };
      } // (lib.optionalAttrs (builtins.elem system linux64BitSystems)) {
        dockerImage = self.hydraJobs.dockerImage.${system};
      } // (lib.optionalAttrs (!(builtins.elem system linux32BitSystems))) {
        # Some perl dependencies are broken on i686-linux.
        # Since the support is only best-effort there, disable the perl
        # bindings

        # Temporarily disabled because GitHub Actions OOM issues. Once
        # the old build system is gone and we are back to one build
        # system, we should reenable this.
        #perlBindings = self.hydraJobs.perlBindings.${system};
      }
      /*
      # Add "passthru" tests
      // flatMapAttrs ({
          "" = nixpkgsFor.${system}.native;
        } // lib.optionalAttrs (! nixpkgsFor.${system}.native.stdenv.hostPlatform.isDarwin) {
          # TODO: enable static builds for darwin, blocked on:
          #       https://github.com/NixOS/nixpkgs/issues/320448
          # TODO: disabled to speed up GHA CI.
          #"static-" = nixpkgsFor.${system}.static;
        })
        (nixpkgsPrefix: nixpkgs:
          flatMapAttrs nixpkgs.nixComponents
            (pkgName: pkg:
              flatMapAttrs pkg.tests or {}
              (testName: test: {
                "${nixpkgsPrefix}${pkgName}-${testName}" = test;
              })
            )
          // lib.optionalAttrs (nixpkgs.stdenv.hostPlatform == nixpkgs.stdenv.buildPlatform) {
            "${nixpkgsPrefix}nix-functional-tests" = nixpkgs.nixComponents.nix-functional-tests;
          }
        )
      */
      // devFlake.checks.${system} or {}
      );

      packages = forAllSystems (system:
        { # Here we put attributes that map 1:1 into packages.<system>, ie
          # for which we don't apply the full build matrix such as cross or static.
          inherit (nixpkgsFor.${system}.native)
            changelog-d;
          default = self.packages.${system}.nix-ng;
          nix-manual = nixpkgsFor.${system}.native.nixComponents.nix-manual;
          nix-internal-api-docs = nixpkgsFor.${system}.native.nixComponents.nix-internal-api-docs;
          nix-external-api-docs = nixpkgsFor.${system}.native.nixComponents.nix-external-api-docs;
        }
        # We need to flatten recursive attribute sets of derivations to pass `flake check`.
        // flatMapAttrs
          { # Components we'll iterate over in the upcoming lambda
            "nix" = { };
            "nix-util" = { };
            "nix-util-c" = { };
            "nix-util-test-support" = { };
            "nix-util-tests" = { };

            "nix-store" = { };
            "nix-store-c" = { };
            "nix-store-test-support" = { };
            "nix-store-tests" = { };

            "nix-fetchers" = { };
            "nix-fetchers-tests" = { };

            "nix-expr" = { };
            "nix-expr-c" = { };
            "nix-expr-test-support" = { };
            "nix-expr-tests" = { };

            "nix-flake" = { };
            "nix-flake-tests" = { };

            "nix-main" = { };
            "nix-main-c" = { };

            "nix-cmd" = { };

            "nix-cli" = { };

            "nix-functional-tests" = { supportsCross = false; };

            "nix-perl-bindings" = { supportsCross = false; };
            "nix-ng" = { };
          }
          (pkgName: { supportsCross ? true }: {
              # These attributes go right into `packages.<system>`.
              "${pkgName}" = nixpkgsFor.${system}.native.nixComponents.${pkgName};
              "${pkgName}-static" = nixpkgsFor.${system}.static.nixComponents.${pkgName};
            }
            // lib.optionalAttrs supportsCross (flatMapAttrs (lib.genAttrs crossSystems (_: { })) (crossSystem: {}: {
              # These attributes go right into `packages.<system>`.
              "${pkgName}-${crossSystem}" = nixpkgsFor.${system}.cross.${crossSystem}.nixComponents.${pkgName};
            }))
            // flatMapAttrs (lib.genAttrs stdenvs (_: { })) (stdenvName: {}: {
              # These attributes go right into `packages.<system>`.
              "${pkgName}-${stdenvName}" = nixpkgsFor.${system}.stdenvs."${stdenvName}Packages".nixComponents.${pkgName};
            })
          )
        // lib.optionalAttrs (builtins.elem system linux64BitSystems) {
        dockerImage =
          let
            pkgs = nixpkgsFor.${system}.native;
            image = import ./docker.nix { inherit pkgs; tag = pkgs.nix.version; };
          in
          pkgs.runCommand
            "docker-image-tarball-${pkgs.nix.version}"
            { meta.description = "Docker image with Nix for ${system}"; }
            ''
              mkdir -p $out/nix-support
              image=$out/image.tar.gz
              ln -s ${image} $image
              echo "file binary-dist $image" >> $out/nix-support/hydra-build-products
            '';
      });

      devShells = let
        makeShell = import ./packaging/dev-shell.nix { inherit lib devFlake; };
        prefixAttrs = prefix: lib.concatMapAttrs (k: v: { "${prefix}-${k}" = v; });
      in
        forAllSystems (system:
          prefixAttrs "native" (forAllStdenvs (stdenvName: makeShell {
            pkgs = nixpkgsFor.${system}.stdenvs."${stdenvName}Packages";
          })) //
          lib.optionalAttrs (!nixpkgsFor.${system}.native.stdenv.isDarwin) (
            prefixAttrs "static" (forAllStdenvs (stdenvName: makeShell {
              pkgs = nixpkgsFor.${system}.stdenvs."${stdenvName}Packages".pkgsStatic;
            })) //
            prefixAttrs "cross" (forAllCrossSystems (crossSystem: makeShell {
              pkgs = nixpkgsFor.${system}.cross.${crossSystem};
            }))
          ) //
          {
            default = self.devShells.${system}.native-stdenvPackages;
          }
        );
      herculesCI.ciSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
  };
}
