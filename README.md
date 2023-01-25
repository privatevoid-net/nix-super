# Nix Super

## It's [Nix](https://nixos.org), but super!

This is an upstream-tracking fork of Nix that includes various patches, some controversial in nature and not fit for Nix upstream.

Some of the patches included are: 
- nix-flake-default from [nix-dram](https://github.com/dramforever/nix-dram)
- enable-all-experimental from the old [nixExperimental](https://github.com/NixOS/nixpkgs/pull/120141)
- Full thunk evaluation in flake inputs
- Some UI improvements
- New subcommands
  - [nix system](https://cache.privatevoid.net/nix/store/x3iy7mf5ly5yqg4zxm551l9ns151jvr7-nix-super-2.14.0pre20230125_4aaaa3c-doc/share/doc/nix/manual/command-ref/new-cli/nix3-system.html) for managing NixOS, as a replacement for `nixos-rebuild`
  - [nix home](https://cache.privatevoid.net/nix/store/x3iy7mf5ly5yqg4zxm551l9ns151jvr7-nix-super-2.14.0pre20230125_4aaaa3c-doc/share/doc/nix/manual/command-ref/new-cli/nix3-home.html) for managing home-manager configurations, as a replacement for the `home-manager` CLI tool
