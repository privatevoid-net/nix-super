{ lib
, buildPackages
, stdenv
, mkMesonDerivation
, releaseTools

, meson
, ninja
, pkg-config

, nix-expr
, nix-expr-c
, nix-expr-test-support

, rapidcheck
, gtest
, runCommand

# Configuration Options

, version
, resolvePath
}:

let
  inherit (lib) fileset;
in

mkMesonDerivation (finalAttrs: {
  pname = "nix-expr-tests";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../../build-utils-meson
    ./build-utils-meson
    ../../../.version
    ./.version
    ./meson.build
    # ./meson.options
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
  ];

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
  ];

  buildInputs = [
    nix-expr
    nix-expr-c
    nix-expr-test-support
    rapidcheck
    gtest
  ];

  preConfigure =
    # "Inline" .version so it's not a symlink, and includes the suffix.
    # Do the meson utils, without modification.
    ''
      chmod u+w ./.version
      echo ${version} > ../../../.version
    '';

  mesonFlags = [
  ];

  env = lib.optionalAttrs (stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux")) {
    LDFLAGS = "-fuse-ld=gold";
  };

  enableParallelBuilding = true;

  separateDebugInfo = !stdenv.hostPlatform.isStatic;

  strictDeps = true;

  hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";

  passthru = {
    tests = {
      run = runCommand "${finalAttrs.pname}-run" {
        meta.broken = !stdenv.hostPlatform.emulatorAvailable buildPackages;
      } (lib.optionalString stdenv.hostPlatform.isWindows ''
        export HOME="$PWD/home-dir"
        mkdir -p "$HOME"
      '' + ''
        export _NIX_TEST_UNIT_DATA=${resolvePath ./data}
        ${stdenv.hostPlatform.emulator buildPackages} ${lib.getExe finalAttrs.finalPackage}
        touch $out
      '');
    };
  };

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
    mainProgram = finalAttrs.pname + stdenv.hostPlatform.extensions.executable;
  };

})
