with import "${builtins.getEnv "_NIX_TEST_BUILD_DIR"}/config.nix";

mkDerivation {
  name = "gc-runtime";
  builder =
    # Test inline source file definitions.
    builtins.toFile "builder.sh" ''
      mkdir $out

      cat > $out/program <<EOF
      #! ${shell}
      sleep 10000
      EOF

      chmod +x $out/program
    '';
}
