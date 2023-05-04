# Nix Super

## It's [Nix](https://nixos.org), but super!

This is an upstream-tracking fork of Nix that includes various patches, some controversial in nature and not fit for Nix upstream.

Some of the patches included are: 
- nix-flake-default from [nix-dram](https://github.com/dramforever/nix-dram)
- experimental features enabled by default:
  - `Xp::Flakes`
  - `Xp::NixCommand`
  - `Xp::ReplFlake`
- Full thunk evaluation in flake inputs
- Some UI improvements, particularly around `nix profile`
- New subcommands
  - [nix system](https://cache.privatevoid.net/nix/store/6wq71q0lwgkr4l900flf26cn0lk79miw-nix-super-2.16.0pre20230504_3822d33-doc/share/doc/nix/manual/command-ref/new-cli/nix3-system.html) for managing NixOS, as a replacement for `nixos-rebuild`
  - [nix home](https://cache.privatevoid.net/nix/store/6wq71q0lwgkr4l900flf26cn0lk79miw-nix-super-2.16.0pre20230504_3822d33-doc/share/doc/nix/manual/command-ref/new-cli/nix3-home.html) for managing home-manager configurations, as a replacement for the `home-manager` CLI tool
- [The `$` operator](https://github.com/NixOS/nix/pull/5577)
