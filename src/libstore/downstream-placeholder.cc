#include "downstream-placeholder.hh"
#include "derivations.hh"

namespace nix {

std::string DownstreamPlaceholder::render() const
{
    return "/" + hash.to_string(Base32, false);
}


DownstreamPlaceholder DownstreamPlaceholder::unknownCaOutput(
    const StorePath & drvPath,
    std::string_view outputName,
    const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(Xp::CaDerivations);
    auto drvNameWithExtension = drvPath.name();
    auto drvName = drvNameWithExtension.substr(0, drvNameWithExtension.size() - 4);
    auto clearText = "nix-upstream-output:" + std::string { drvPath.hashPart() } + ":" + outputPathName(drvName, outputName);
    return DownstreamPlaceholder {
        hashString(htSHA256, clearText)
    };
}

DownstreamPlaceholder DownstreamPlaceholder::unknownDerivation(
    const DownstreamPlaceholder & placeholder,
    std::string_view outputName,
    const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(Xp::DynamicDerivations);
    auto compressed = compressHash(placeholder.hash, 20);
    auto clearText = "nix-computed-output:"
        + compressed.to_string(Base32, false)
        + ":" + std::string { outputName };
    return DownstreamPlaceholder {
        hashString(htSHA256, clearText)
    };
}

DownstreamPlaceholder DownstreamPlaceholder::fromSingleDerivedPathBuilt(
    const SingleDerivedPath::Built & b)
{
    return std::visit(overloaded {
        [&](const SingleDerivedPath::Opaque & o) {
            return DownstreamPlaceholder::unknownCaOutput(o.path, b.output);
        },
        [&](const SingleDerivedPath::Built & b2) {
            return DownstreamPlaceholder::unknownDerivation(
                DownstreamPlaceholder::fromSingleDerivedPathBuilt(b2),
                b.output);
        },
    }, b.drvPath->raw());
}

}