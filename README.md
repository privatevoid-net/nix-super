# Nix Super

## It's [Nix](https://nixos.org), but super!

This is an upstream-tracking fork of Nix that includes various patches, some controversial in nature and not fit for Nix upstream.

## Features

### `nix-flake-default.patch` from [nix-dram](https://github.com/dramforever/nix-dram)

This uses an older version of the patch, when the name of the default installable was not yet configurable.

If you have an entry called `default` in your Nix registry, you can do things like:

```shell-session
$ nix shell jq gron kubectl
# equivalent to nix shell default#jq default#gron default#kubectl
```

[More information](https://github.com/dramforever/nix-dram#changes-to-installable)

### Experimental features enabled by default

The following experimental features are enabled by default:
- `flakes` (`Xp::Flakes`)
- `nix-command` (`Xp::NixCommand`)
- `repl-flake` (`Xp::ReplFlake`)

### Full thunk evaluation in `flake.nix`

In stock Nix, only the outputs section of `flake.nix` is able to make full use of the Nix language.
The inputs section as well as the top-level attribute set are required to be *trivial*.
This is for good reason, as it prevents arbitrarily complex computations during operations where you would not expect this,
such as `nix flake metadata`.
Nonetheless, people were often annoyed by this limitation. Nix Super includes patches to disable the triviality checks,
to encourage experimentation with fancy new ways of handling flake inputs.

### UI improvements around `nix profile`

`nix profile list` looks like this for profile entries coming from a flake:

```
005:
	Installable: github:EmaApps/emanote#packages.x86_64-linux.default
	Store paths: /nix/store/hdkbmj480nn2c5v9whzm2p1ip2cwqlpx-emanote-0.7.9.0
006:
	Installable: flake:default#legacyPackages.x86_64-linux.just
	Store paths: /nix/store/8x9yfyhs9innj3y3g6q953fqbjfiqnp4-just-1.13.0
```

Non-flake entries still look like normal to preserve compatibility with home-manager.

### Activatables

Nix Super introduces the concept of *activatables*; applications that are installed solely in their own profile
and rely on an activation script to perform actions outside of the Nix store.

Two new subcommands are implemented to make use of activatables:

- [nix system](https://cache.privatevoid.net/nix/store/6wq71q0lwgkr4l900flf26cn0lk79miw-nix-super-2.16.0pre20230504_3822d33-doc/share/doc/nix/manual/command-ref/new-cli/nix3-system.html) for managing NixOS, as a replacement for `nixos-rebuild`
- [nix home](https://cache.privatevoid.net/nix/store/6wq71q0lwgkr4l900flf26cn0lk79miw-nix-super-2.16.0pre20230504_3822d33-doc/share/doc/nix/manual/command-ref/new-cli/nix3-home.html) for managing home-manager configurations, as a replacement for the `home-manager` CLI tool


### [The `$` operator](https://github.com/NixOS/nix/pull/5577)

The `$` operator or function application operator can be used to reduce parentheses hell in some situations,
though its semantics are slightly different from the Haskell variant, making this one less useful.

```nix
builtins.trace "asdf" $ map toString [ 1 2 3 ]
```

### Easy use of `callPackage` from the CLI

The flag `-C`/`--call-package` allows you to directly build *callPackageable expressions* from the CLI.
This invokes `import <nixpkgs> {}` to get access to `callPackage`.


```shell-session
$ cat hello.nix
{
  stdenv,
  hello
}:

stdenv.mkDerivation {
  name = "hello";

  nativeBuildInputs = [ hello ];

  buildCommand = "hello > $out";
}

$ nix build -C hello.nix
$ cat result
Hello, world!
```

### CLI overrides

Various CLI flags have been added to allow on-the-fly overriding of installables.


#### Override expression arguments
Allows overriding any argument usually overridable via `.override`. Can be used multiple times.
```shell-session
$ nix build ffmpeg --override withMfx true
```

#### Override packages
Like `--override`, but for overriding packages. This can be any installable from any flake. Can be used multiple times.
```shell-session
$ nix build nil --override-pkg nix github:privatevoid-net/nix-super
```

#### Override attributes
The previous attributes are available in `old`, but are also in scope via `with`.
```shell-session
$ nix build hello --override-attrs '{ name = "my-${name}"; src = ./.; }' --impure
```

#### Use `withPackages`
The packages are available in `ps`, but are also in scope via `with`.
```shell-session
$ nix shell python3 --with '[ numpy pandas matplotlib ]'
```

#### Do anything
`--apply-to-installabe` gives you direct access to the installable in a function
```shell-session
$ nix build writeText --apply-to-installable 'writeText: writeText "test" "hello"'
```

### Additional environment variables for `nix shell`

`nix shell` will prepend the `/bin` directory of a given package to `PATH`, but what about other environment variables?

Nix Super configures many other environment variables, including:

- `CUPS_DATADIR`
- `DICPATH`
- `GIO_EXTRA_MODULES`
- `GI_TYPELIB_PATH`
- `GST_PLUGIN_PATH_1_0`
- `GTK_PATH`
- `INFOPATH`
- `LADSPA_PATH`
- `LIBEXEC_PATH`
- `LV2_PATH`
- `MOZ_PLUGIN_PATH`
- `QTWEBKIT_PLUGIN_PATH`
- `TERMINFO_DIRS`
- `XDG_CONFIG_DIRS`
- `XDG_DATA_DIRS`

It also sets `IN_NIX3_SHELL=1` to allow external processes to detect when you're in a Nix shell,
for scripting or shell prompt customization.

### Support for the `git+ipld` fetcher scheme

Adds `git+ipld` to the list of supported URL schemes for the `git` fetcher. Allows you to use Nix with [git-remote-ipld](https://github.com/ipfs-shipyard/git-remote-ipld).

NOTE: This does not mean that Nix Super itself has any IPFS capabilities (yet).
