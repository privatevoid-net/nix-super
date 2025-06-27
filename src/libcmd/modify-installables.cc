#include "nix/store/globals.hh"
#include "nix/cmd/installables.hh"
#include "nix/cmd/installable-derived-path.hh"
#include "nix/cmd/installable-attr-path.hh"
#include "nix/cmd/installable-flake.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/util/util.hh"
#include "nix/cmd/command.hh"
#include "nix/expr/attr-path.hh"
#include "nix/cmd/common-eval-args.hh"
#include "nix/store/derivations.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/get-drvs.hh"
#include "nix/store/store-api.hh"
#include "nix/main/shared.hh"
#include "nix/flake/flake.hh"
#include "nix/expr/eval-cache.hh"
#include "nix/util/url.hh"
#include "nix/fetchers/registry.hh"
#include "nix/store/build-result.hh"

#include <regex>
#include <queue>

#include <nlohmann/json.hpp>
#include <string_view>

namespace nix {

ref<Installable> SourceExprCommand::modifyInstallable (
    ref<Store> store, ref<EvalState> state,
    ref<InstallableValue> installable,
    std::string_view installableName,
    std::string_view prefix, ExtendedOutputsSpec extendedOutputsSpec
)
{
    auto [v, pos] = installable->toValue(*state);
    auto vApply = state->allocValue();
    auto vRes = state->allocValue();
    auto overrideSet = getOverrideArgs(*state, store);

    if (applyToInstallable) {
        state->eval(state->parseExprFromString(*applyToInstallable, state->rootPath(".")), *vApply);
        state->callFunction(*vApply, *v, *vRes, noPos);
    } else if (overrideSet->size() > 0) {
        Value * overrideValues = state->allocValue();
        auto overrideBinds = state->buildBindings(overrideSet->size());
        for (auto& v : *overrideSet) {
            overrideBinds.insert(v);
        }
        overrideValues->mkAttrs(overrideBinds.finish());
        auto vOverrideFunctorAttr = v->attrs()->get(state->symbols.create("override"));
        if (!vOverrideFunctorAttr) {
            throw Error("%s is not overridable", installableName);
        }
        auto vOverrideFunctor = vOverrideFunctorAttr->value;
        state->callFunction(*vOverrideFunctor, *overrideValues, *vRes, noPos);
    } else if (installableOverrideAttrs) {
        state->eval(state->parseExprFromString(fmt("old: with old; %s",*installableOverrideAttrs), state->rootPath(".")), *vApply);
        auto vOverrideFunctorAttr = v->attrs()->get(state->symbols.create("overrideAttrs"));
        if (!vOverrideFunctorAttr) {
            throw Error("%s is not overrideAttrs-capable", installableName);
        }
        auto vOverrideFunctor = vOverrideFunctorAttr->value;
        state->callFunction(*vOverrideFunctor, *vApply, *vRes, noPos);
    } else if (installableWithPackages) {
        state->eval(state->parseExprFromString(fmt("ps: with ps; %s",*installableWithPackages), state->rootPath(".")), *vApply);
        auto vOverrideFunctorAttr = v->attrs()->get(state->symbols.create("withPackages"));
        if (!vOverrideFunctorAttr) {
            throw Error("%s cannot be extended with additional packages", installableName);
        }
        auto vOverrideFunctor = vOverrideFunctorAttr->value;
        state->callFunction(*vOverrideFunctor, *vApply, *vRes, noPos);
    }
    return
        make_ref<InstallableAttrPath>(InstallableAttrPath::parse(
            state, *this, vRes, std::move(prefix), std::move(extendedOutputsSpec)
        ));
}

}
