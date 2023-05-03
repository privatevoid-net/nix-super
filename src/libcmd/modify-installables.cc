#include "globals.hh"
#include "installables.hh"
#include "installable-derived-path.hh"
#include "installable-attr-path.hh"
#include "installable-flake.hh"
#include "outputs-spec.hh"
#include "util.hh"
#include "command.hh"
#include "attr-path.hh"
#include "common-eval-args.hh"
#include "derivations.hh"
#include "eval-inline.hh"
#include "eval.hh"
#include "get-drvs.hh"
#include "store-api.hh"
#include "shared.hh"
#include "flake/flake.hh"
#include "eval-cache.hh"
#include "url.hh"
#include "registry.hh"
#include "build-result.hh"

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
        state->eval(state->parseExprFromString(*applyToInstallable, state->rootPath(CanonPath::fromCwd())), *vApply);
        state->callFunction(*vApply, *v, *vRes, noPos);
    } else if (overrideSet->size() > 0) {
        Value * overrideValues = state->allocValue();
        overrideValues->mkAttrs(state->buildBindings(overrideSet->size()).finish());
        for (auto& v : *overrideSet) {
            overrideValues->attrs->push_back(v);
        }
        auto vOverrideFunctorAttr = v->attrs->get(state->symbols.create("override"));
        if (!vOverrideFunctorAttr) {
            throw Error("%s is not overridable", installableName);
        }
        auto vOverrideFunctor = vOverrideFunctorAttr->value;
        state->callFunction(*vOverrideFunctor, *overrideValues, *vRes, noPos);
    } else if (installableOverrideAttrs) {
        state->eval(state->parseExprFromString(fmt("old: with old; %s",*installableOverrideAttrs), state->rootPath(CanonPath::fromCwd())), *vApply);
        auto vOverrideFunctorAttr = v->attrs->get(state->symbols.create("overrideAttrs"));
        if (!vOverrideFunctorAttr) {
            throw Error("%s is not overrideAttrs-capable", installableName);
        }
        auto vOverrideFunctor = vOverrideFunctorAttr->value;
        state->callFunction(*vOverrideFunctor, *vApply, *vRes, noPos);
    } else if (installableWithPackages) {
        state->eval(state->parseExprFromString(fmt("ps: with ps; %s",*installableWithPackages), state->rootPath(CanonPath::fromCwd())), *vApply);
        auto vOverrideFunctorAttr = v->attrs->get(state->symbols.create("withPackages"));
        if (!vOverrideFunctorAttr) {
            throw Error("%s cannot be extended with additional packages", installableName);
        }
        auto vOverrideFunctor = vOverrideFunctorAttr->value;
        state->callFunction(*vOverrideFunctor, *vApply, *vRes, noPos);
    }
    return
        make_ref<InstallableAttrPath>(InstallableAttrPath::parse(
            state, *this, vRes, prefix, extendedOutputsSpec
        ));
}

}
