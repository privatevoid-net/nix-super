#include "command.hh"
#include "local-fs-store.hh"
#include "progress-bar.hh"
#include "shared.hh"

using namespace nix;


template<class ActivatableCommand>
ActivatableBuildCommand<ActivatableCommand>::ActivatableBuildCommand()
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
};

template<class ActivatableCommand>
void ActivatableBuildCommand<ActivatableCommand>::run(nix::ref<Store> store)
{
    auto installableFlake = std::dynamic_pointer_cast<InstallableFlake>(ActivatableCommand::installable);
    if (dryRun) {
        std::vector<std::shared_ptr<Installable>> installableContext;
        installableContext.emplace_back(ActivatableCommand::installable);
        std::vector<DerivedPath> pathsToBuild;

        for (auto & i : installableContext) {
            auto b = i->toDerivedPaths();
            for (auto & b : i->toDerivedPaths())
                pathsToBuild.push_back(b.path);
        }
        printMissing(store, pathsToBuild, lvlError);
        return;
    }

    auto out = ActivatableCommand::buildActivatable(store);

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

template<class ActivatableCommand>
ActivationCommand<ActivatableCommand>::ActivationCommand(std::string activationType, std::string selfCommandName)
    : activationType(activationType),
      selfCommandName(selfCommandName)
{ }
