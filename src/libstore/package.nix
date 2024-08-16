{ lib
, stdenv
, mkMesonDerivation
, releaseTools

, meson
, ninja
, pkg-config
, unixtools

, nix-util
, boost
, curl
, aws-sdk-cpp
, libseccomp
, nlohmann_json
, sqlite

, busybox-sandbox-shell ? null

# Configuration Options

, version

, embeddedSandboxShell ? stdenv.hostPlatform.isStatic
}:

let
  inherit (lib) fileset;
in

mkMesonDerivation (finalAttrs: {
  pname = "nix-store";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../build-utils-meson
    ./build-utils-meson
    ../../.version
    ./.version
    ./meson.build
    ./meson.options
    ./linux/meson.build
    ./unix/meson.build
    ./windows/meson.build
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
    (fileset.fileFilter (file: file.hasExt "sb") ./.)
    (fileset.fileFilter (file: file.hasExt "md") ./.)
    (fileset.fileFilter (file: file.hasExt "sql") ./.)
  ];

  outputs = [ "out" "dev" ];

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
  ] ++ lib.optional embeddedSandboxShell unixtools.hexdump;

  buildInputs = [
    boost
    curl
    sqlite
  ] ++ lib.optional stdenv.hostPlatform.isLinux libseccomp
    # There have been issues building these dependencies
    ++ lib.optional (stdenv.hostPlatform == stdenv.buildPlatform && (stdenv.isLinux || stdenv.isDarwin))
      aws-sdk-cpp
  ;

  propagatedBuildInputs = [
    nix-util
    nlohmann_json
  ];

  preConfigure =
    # "Inline" .version so it's not a symlink, and includes the suffix.
    # Do the meson utils, without modification.
    ''
      chmod u+w ./.version
      echo ${version} > ../../.version
    '';

  mesonFlags = [
    (lib.mesonEnable "seccomp-sandboxing" stdenv.hostPlatform.isLinux)
    (lib.mesonBool "embedded-sandbox-shell" embeddedSandboxShell)
  ] ++ lib.optionals stdenv.hostPlatform.isLinux [
    (lib.mesonOption "sandbox-shell" "${busybox-sandbox-shell}/bin/busybox")
  ];

  env = {
    # Needed for Meson to find Boost.
    # https://github.com/NixOS/nixpkgs/issues/86131.
    BOOST_INCLUDEDIR = "${lib.getDev boost}/include";
    BOOST_LIBRARYDIR = "${lib.getLib boost}/lib";
  } // lib.optionalAttrs (stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux")) {
    LDFLAGS = "-fuse-ld=gold";
  };

  separateDebugInfo = !stdenv.hostPlatform.isStatic;

  hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  };

})
