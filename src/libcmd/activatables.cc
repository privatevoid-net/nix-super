#include "command.hh"
#include "progress-bar.hh"

using namespace nix;

ActivatableCommand::ActivatableCommand(std::string activationPackageAttrPath)
    : activationPackageAttrPath(activationPackageAttrPath)
{ }

void ActivatableCommand::prepare() {
    InstallableCommand::prepare();
    auto installableFlake = std::dynamic_pointer_cast<InstallableFlake>(installable);
    if (installableFlake) {
        auto fragment = *installableFlake->attrPaths.begin();
        installable = std::make_shared<InstallableFlake>(
                this,
                getEvalState(),
                std::move(installableFlake->flakeRef),
                activationPackageAttrPath == "" ? fragment : fragment + "." + activationPackageAttrPath,
                installableFlake->extendedOutputsSpec,
                getDefaultFlakeAttrPaths(),
                getDefaultFlakeAttrPathPrefixes(),
                installableFlake->lockFlags
            );
    }
}

StorePath ActivatableCommand::buildActivatable(nix::ref<nix::Store> store, bool dryRun) {
    auto installableFlake = std::dynamic_pointer_cast<InstallableFlake>(installable);
    std::vector<std::shared_ptr<Installable>> installableContext;
    installableContext.emplace_back(installable);
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
