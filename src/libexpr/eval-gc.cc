#include "nix/util/error.hh"
#include "nix/util/environment-variables.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/util/config-global.hh"
#include "nix/util/serialise.hh"
#include "nix/expr/eval-gc.hh"

#include "expr-config-private.hh"

namespace nix {

static bool gcInitialised = false;

void initGC()
{
    if (gcInitialised)
        return;

    // NIX_PATH must override the regular setting
    // See the comment in applyConfig
    if (auto nixPathEnv = getEnv("NIX_PATH")) {
        globalConfig.set("nix-path", concatStringsSep(" ", EvalSettings::parseNixPath(nixPathEnv.value())));
    }

    gcInitialised = true;
}

void assertGCInitialized()
{
    assert(gcInitialised);
}

} // namespace nix
