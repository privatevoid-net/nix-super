#include "args.hh"
#include "command.hh"
#include "shared.hh"
#include "common-args.hh"
#include "error.hh"
#include "installables.hh"
#include "logging.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "flake/flake.hh"
#include "get-drvs.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "path-with-outputs.hh"
#include "attr-path.hh"
#include "fetchers.hh"
#include "registry.hh"
#include "eval-cache.hh"
#include "markdown.hh"
#include "local-fs-store.hh"
#include "progress-bar.hh"
#include "util.hh"

#include <memory>
#include <queue>
#include <iomanip>
#include <string>
#include <string_view>
#include <unistd.h>

using namespace nix;

void executePrivileged(std::string program, Strings args) {
    stopProgressBar();
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

struct SystemCommand : InstallableCommand
{
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

    std::shared_ptr<Installable> transformInstallable(ref<EvalState> state, std::shared_ptr<Installable> installable) {
        auto installableFlake = std::dynamic_pointer_cast<InstallableFlake>(installable);
        if (installableFlake) {
            auto fragment = *installableFlake->attrPaths.begin();
            return std::make_shared<InstallableFlake>(
                    this,
                    state,
                    std::move(installableFlake->flakeRef),
                    fragment + ".config.system.build.toplevel",
                    installableFlake->outputsSpec,
                    getDefaultFlakeAttrPaths(),
                    getDefaultFlakeAttrPathPrefixes(),
                    installableFlake->lockFlags
                );
        } else {
            return installable;
        }
    }

    StorePath buildSystem(nix::ref<nix::Store> store) {
        std::vector<std::shared_ptr<Installable>> installableContext;
        auto state = getEvalState();
        installableContext.emplace_back(transformInstallable(state, installable));
        auto buildables = Installable::build(
            getEvalStore(), store,
            Realise::Outputs,
            installableContext, bmNormal);

        BuiltPaths buildables2;
        for (auto & b : buildables)
            buildables2.push_back(b.path);

        auto buildable = buildables2.front();
        if (buildables2.size() != 1 || buildable.outPaths().size() != 1) {
            throw UsageError("this command requires that the argument produces a single store path");
        }
        return *buildable.outPaths().begin();
    }
};

struct SystemInstalledActivationCommand : SystemCommand, MixProfile
{
    std::string activationType;
    std::string selfCommandName;

    SystemInstalledActivationCommand(std::string activationType, std::string selfCommandName)
        : activationType(activationType),
          selfCommandName(selfCommandName)
    { }

    void run(nix::ref<nix::Store> store) override
    {
        auto out = buildSystem(store);

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
                profile = systemProfileBase + "/" + profile.value();
            }
            updateProfile(out);
            executePrivileged(profile.value() + "/bin/switch-to-configuration", Strings{activationType});
        }
    }
};

struct CmdSystemBuild : SystemCommand, MixDryRun
{
    Path outLink = "result";
    bool printOutputPaths = false;

    CmdSystemBuild()
    {
        addFlag({
            .longName = "out-link",
            .shortName = 'o',
            .description = "Use *path* as prefix for the symlinks to the build results. It defaults to `result`.",
            .labels = {"path"},
            .handler = {&outLink},
            .completer = completePath
        });

        addFlag({
            .longName = "no-link",
            .description = "Do not create symlinks to the build results.",
            .handler = {&outLink, Path("")},
        });

        addFlag({
            .longName = "print-out-paths",
            .description = "Print the resulting output paths",
            .handler = {&printOutputPaths, true},
        });
    }

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

    void run(nix::ref<nix::Store> store) override
    {
        if (dryRun) {
            std::vector<std::shared_ptr<Installable>> installableContext;
            auto state = getEvalState();
            installableContext.emplace_back(transformInstallable(state, installable));
            std::vector<DerivedPath> pathsToBuild;

            for (auto & i : installableContext) {
                auto b = i->toDerivedPaths();
                pathsToBuild.insert(pathsToBuild.end(), b.begin(), b.end());
            }
            printMissing(store, pathsToBuild, lvlError);
            return;
        }

        auto out = buildSystem(store);

        if (outLink != "") {
            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>()) {
                std::string symlink = outLink;
                store2->addPermRoot(out, absPath(symlink));
            }
        }

        if (printOutputPaths) {
            stopProgressBar();
            std::cout << store->printStorePath(out) << std::endl;
        }
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
    void run(nix::ref<nix::Store> store) override
    {
        auto out = buildSystem(store);

        executePrivileged(store->printStorePath(out) + "/bin/switch-to-configuration", Strings{dryRun ? "dry-activate" : "test"});
    }
};

struct CmdSystemApply : SystemInstalledActivationCommand
{
    CmdSystemApply() : SystemInstalledActivationCommand("switch", "apply")
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

struct CmdSystemBoot : SystemInstalledActivationCommand
{
    CmdSystemBoot() : SystemInstalledActivationCommand("boot","boot")
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
        : MultiCommand({
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
        settings.requireExperimentalFeature(Xp::NixCommand);
        command->second->prepare();
        command->second->run();
    }
};

static auto rCmdSystem = registerCommand<CmdSystem>("system");
