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
        std::vector<std::shared_ptr<Installable>> installableContext;
        auto state = getEvalState();
        installableContext.emplace_back(transformInstallable(state, installable));
        if (dryRun) {
            std::vector<DerivedPath> pathsToBuild;

            for (auto & i : installableContext) {
                auto b = i->toDerivedPaths();
                pathsToBuild.insert(pathsToBuild.end(), b.begin(), b.end());
            }
            printMissing(store, pathsToBuild, lvlError);
            return;
        }

        auto buildables = Installable::build(
            getEvalStore(), store,
            Realise::Outputs,
            installableContext, bmNormal);

        if (outLink != "")
            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
                for (const auto & [_i, buildable] : enumerate(buildables)) {
                    auto i = _i;
                    std::visit(overloaded {
                        [&](const BuiltPath::Opaque & bo) {
                            std::string symlink = outLink;
                            if (i) symlink += fmt("-%d", i);
                            store2->addPermRoot(bo.path, absPath(symlink));
                        },
                        [&](const BuiltPath::Built & bfd) {
                            for (auto & output : bfd.outputs) {
                                std::string symlink = outLink;
                                if (i) symlink += fmt("-%d", i);
                                if (output.first != "out") symlink += fmt("-%s", output.first);
                                store2->addPermRoot(output.second, absPath(symlink));
                            }
                        },
                    }, buildable.path.raw());
                }

        if (printOutputPaths) {
            stopProgressBar();
            for (auto & buildable : buildables) {
                std::visit(overloaded {
                    [&](const BuiltPath::Opaque & bo) {
                        std::cout << store->printStorePath(bo.path) << std::endl;
                    },
                    [&](const BuiltPath::Built & bfd) {
                        for (auto & output : bfd.outputs) {
                            std::cout << store->printStorePath(output.second) << std::endl;
                        }
                    },
                }, buildable.path.raw());
            }
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
        auto state = getEvalState();
        auto inst = transformInstallable(state, installable);
        auto cursor = inst->getCursor(*state);

        auto drvPath = cursor->forceDerivation();
        auto outPath = cursor->getAttr(state->sOutPath)->getString();
        auto outputName = cursor->getAttr(state->sOutputName)->getString();

        auto app = UnresolvedApp{App {
            .context = { { drvPath, {outputName} } },
            .program = outPath + "/bin/switch-to-configuration",
        }}.resolve(getEvalStore(), store);

        executePrivileged(app.program, Strings{dryRun ? "dry-activate" : "test"});
    }
};

struct CmdSystem : NixMultiCommand
{
    CmdSystem()
        : MultiCommand({
                {"build", []() { return make_ref<CmdSystemBuild>(); }},
                {"activate", []() { return make_ref<CmdSystemActivate>(); }},
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
