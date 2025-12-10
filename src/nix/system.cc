#include "nix/cmd/command.hh"
#include "nix/cmd/activatables.hh"
#include "nix/store/globals.hh"
#include "nix/util/current-process.hh"

using namespace nix;

void executePrivileged(std::string program, Strings args) {
    logger->stop();
    restoreProcessContext();
    args.push_front(program);
    auto exe = program;
    auto privCmds = Strings {
        "doas",
        "sudo"
    };
    bool isRoot = getuid() == 0;
    for (auto privCmd : privCmds) {
        if(!isRoot) {
            args.push_front(privCmd);
            exe = privCmd;
        }
        execvp(exe.c_str(), stringsToCharPtrs(args).data());
        if(!isRoot)
            args.pop_front();
    }

    throw SysError("unable to execute privilege elevation helper (tried %s)", concatStringsSep(", ", privCmds));
}

struct SystemCommand : ActivatableCommand
{
    SystemCommand()
        : ActivatableCommand("config.system.build.toplevel")
    {}

    Strings getDefaultFlakeAttrPaths() override
    {
        char hostname[1024];
        hostname[1023] = '\0';
        gethostname(hostname, 1024);
        Strings res{
            "nixosConfigurations." + std::string(hostname),
        };
        return res;
    }

    Strings getDefaultFlakeAttrPathPrefixes() override
    {
        Strings res{"nixosConfigurations."};
        return res;
    }

};

struct SystemActivationCommand : ActivationCommand<SystemCommand>, MixProfile
{
    SystemActivationCommand(std::string activationType, std::string selfCommandName)
        : ActivationCommand<SystemCommand>(activationType, selfCommandName)
    { }

    void runActivatable(ref<Store> store, ref<Installable> installable) override
    {
        auto out = buildActivatable(store, installable);

        if (getuid() != 0) {
            Strings args {
                "system", selfCommandName,
                store->printStorePath(out)
            };
            if (profile) {
                args.push_back("--profile");
                args.push_back(profile.value());
            }
            executePrivileged(getSelfExe().value_or("nix"), args);
        } else {
            if (!profile) {
                profile = settings.nixStateDir + "/profiles/system";
            } else {
                auto systemProfileBase = settings.nixStateDir + "/profiles/system-profiles";
                if (!pathExists(systemProfileBase)) {
                    createDirs(systemProfileBase);
                }
                profile = systemProfileBase + "/" + profile.value().string();
            }
            updateProfile(out);
            executePrivileged(profile.value() + "/bin/switch-to-configuration", Strings{activationType});
        }
    }
};

struct CmdSystemBuild : ActivatableBuildCommand<SystemCommand>
{
    std::string description() override
    {
        return "build a NixOS system configuration";
    }

    std::string doc() override
    {
        return
          #include "system-build.md"
          ;
    }
};

struct CmdSystemActivate : SystemCommand, MixDryRun
{
    CmdSystemActivate()
    {
    }

    std::string description() override
    {
        return "activate a NixOS system configuration";
    }

    std::string doc() override
    {
        return
          #include "system-activate.md"
          ;
    }
    void runActivatable(ref<Store> store, ref<Installable> installable) override
    {
        auto out = buildActivatable(store, installable);

        executePrivileged(store->printStorePath(out) + "/bin/switch-to-configuration", Strings{dryRun ? "dry-activate" : "test"});
    }
};

struct CmdSystemApply : SystemActivationCommand
{
    CmdSystemApply() : SystemActivationCommand("switch", "apply")
    {
    }

    std::string description() override
    {
        return "activate a NixOS system configuration and make it the default boot configuration";
    }

    std::string doc() override
    {
        return
          #include "system-apply.md"
          ;
    }
};

struct CmdSystemBoot : SystemActivationCommand
{
    CmdSystemBoot() : SystemActivationCommand("boot","boot")
    {
    }

    std::string description() override
    {
        return "make a NixOS configuration the default boot configuration";
    }

    std::string doc() override
    {
        return
          #include "system-boot.md"
          ;
    }
};

struct CmdSystem : NixMultiCommand
{
    CmdSystem()
        : NixMultiCommand("system", {
                {"build", []() { return make_ref<CmdSystemBuild>(); }},
                {"activate", []() { return make_ref<CmdSystemActivate>(); }},
                {"apply", []() { return make_ref<CmdSystemApply>(); }},
                {"boot", []() { return make_ref<CmdSystemBoot>(); }},
            })
    {
    }

    std::string description() override
    {
        return "manage NixOS systems";
    }

    std::string doc() override
    {
        return
          #include "system.md"
          ;
    }

    void run() override
    {
        if (!command)
            throw UsageError("'nix system' requires a sub-command.");
        command->second->run();
    }
};

static auto rCmdSystem = registerCommand<CmdSystem>("system");
