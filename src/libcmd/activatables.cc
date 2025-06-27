#include "nix/cmd/command.hh"
#include "nix/cmd/installable-flake.hh"

using namespace nix;

ActivatableCommand::ActivatableCommand(std::string activationPackageAttrPath)
    : activationPackageAttrPath(activationPackageAttrPath)
{ }

void ActivatableCommand::run(ref<Store> store, ref<Installable> installable) {
    auto installableFlake = installable.dynamic_pointer_cast<InstallableFlake>();;
    if (installableFlake) {
        auto fragment = *installableFlake->attrPaths.begin();
        auto _installable = make_ref<InstallableFlake>(
                this,
                getEvalState(),
                std::move(installableFlake->flakeRef),
                activationPackageAttrPath == "" ? fragment : fragment + "." + activationPackageAttrPath,
                installableFlake->extendedOutputsSpec,
                getDefaultFlakeAttrPaths(),
                getDefaultFlakeAttrPathPrefixes(),
                installableFlake->lockFlags
            );
        runActivatable(store, _installable);
    } else {
        runActivatable(store, installable);
    }
}

StorePath ActivatableCommand::buildActivatable(nix::ref<nix::Store> store, ref<Installable> installable, bool dryRun) {
    std::vector<ref<Installable>> installableContext;
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
