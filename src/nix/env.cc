#include <unordered_set>
#include <queue>

#include "nix/cmd/command.hh"
#include "nix/expr/eval.hh"
#include "run.hh"
#include "nix/util/strings.hh"
#include "nix/util/executable-path.hh"

using namespace nix;

struct CmdEnv : NixMultiCommand
{
    CmdEnv()
        : NixMultiCommand("env", RegisterCommand::getCommandsFor({"env"}))
    {
    }

    std::string description() override
    {
        return "manipulate the process environment";
    }

    Category category() override
    {
        return catUtility;
    }
};

static auto rCmdEnv = registerCommand<CmdEnv>("env");

struct CmdShell : InstallablesCommand, MixEnvironment
{

    using InstallablesCommand::run;

    std::vector<std::string> command = { getEnv("SHELL").value_or("bash") };

    CmdShell()
    {
        addFlag({
            .longName = "command",
            .shortName = 'c',
            .description = "Command and arguments to be executed, defaulting to `$SHELL`",
            .labels = {"command", "args"},
            .handler = {[&](std::vector<std::string> ss) {
                if (ss.empty())
                    throw UsageError("--command requires at least one argument");
                command = ss;
            }},
        });
    }

    std::string description() override
    {
        return "run a shell in which the specified packages are available";
    }

    std::string doc() override
    {
        return
          #include "shell.md"
          ;
    }

    void run(ref<Store> store, Installables && installables) override
    {
        auto state = getEvalState();

        auto outPaths =
            Installable::toStorePaths(getEvalStore(), store, Realise::Outputs, OperateOn::Output, installables);

        std::unordered_set<StorePath> done;
        std::queue<StorePath> todo;
        for (auto & path : outPaths) todo.push(path);

        setEnviron();

        // extra PATH-like environment variables
        std::map<std::string,std::string> extraPathVarMapping {
              { "CUPS_DATADIR",         "/share/cups" }
            , { "DICPATH",              "/share/hunspell" }
            , { "GIO_EXTRA_MODULES",    "/lib/gio/modules" }
            , { "GI_TYPELIB_PATH",      "/lib/girepository-1.0" }
            , { "GST_PLUGIN_PATH_1_0",  "/lib/gstreamer-1.0" }
            , { "GTK_PATH",             "/lib/gtk-3.0" } // TODO: gtk-2.0, gtk-4.0 support
            , { "INFOPATH",             "/share/info" }
            , { "LADSPA_PATH",          "/lib/ladspa" }
            , { "LIBEXEC_PATH",         "/libexec" }
            , { "LV2_PATH",             "/lib/lv2" }
            , { "MOZ_PLUGIN_PATH",      "/lib/mozilla/plugins" }
            , { "QTWEBKIT_PLUGIN_PATH", "/lib/mozilla/plugins" }
            , { "TERMINFO_DIRS",        "/share/terminfo" }
            , { "XDG_CONFIG_DIRS",      "/etc/xdg" }
            , { "XDG_DATA_DIRS",        "/share" }
        };

        std::vector<std::string> pathAdditions;

        std::map<std::string,Strings> extraPathVars;

        for (auto const& pathV : extraPathVarMapping) {
            extraPathVars[pathV.first] = tokenizeString<Strings>(getEnv(pathV.first).value_or(""), ":");
        }


        while (!todo.empty()) {
            auto path = todo.front();
            todo.pop();
            if (!done.insert(path).second) continue;

            auto binDir = state->storeFS->resolveSymlinks(CanonPath(store->printStorePath(path)) / "bin");
            if (!store->isInStore(binDir.abs()))
                throw Error("path '%s' is not in the Nix store", binDir);

            pathAdditions.push_back(binDir.abs());

            auto pathString = store->printStorePath(path);

            for (auto const& pathV : extraPathVarMapping) {
                auto realPath = state->storeFS->resolveSymlinks(CanonPath(pathString + pathV.second));
                if (auto st = state->storeFS->maybeLstat(realPath); st)
                    extraPathVars[pathV.first].push_front(pathString + pathV.second);
            }

            auto propPath = state->storeFS->resolveSymlinks(
                CanonPath(store->printStorePath(path)) / "nix-support" / "propagated-user-env-packages");
            if (auto st = state->storeFS->maybeLstat(propPath); st && st->type == SourceAccessor::tRegular) {
                for (auto & p : tokenizeString<Paths>(state->storeFS->readFile(propPath)))
                    todo.push(store->parseStorePath(p));
            }
        }

        // TODO: split losslessly; empty means .
        auto unixPath = ExecutablePath::load();
        unixPath.directories.insert(unixPath.directories.begin(), pathAdditions.begin(), pathAdditions.end());
        auto unixPathString = unixPath.render();
        setEnvOs(OS_STR("PATH"), unixPathString.c_str());

        for (auto const& pathV : extraPathVarMapping) {
            if (!extraPathVars[pathV.first].empty()) {
                setEnvOs(pathV.first, concatStringsSep(":", extraPathVars[pathV.first]).c_str());
            }
        }

        setenv("IN_NIX3_SHELL", "1", 1);

        Strings args;
        for (auto & arg : command) args.push_back(arg);

        // Release our references to eval caches to ensure they are persisted to disk, because
        // we are about to exec out of this process without running C++ destructors.
        state->evalCaches.clear();

        execProgramInStore(store, UseLookupPath::Use, *command.begin(), args);
    }
};

static auto rCmdShell = registerCommand2<CmdShell>({"env", "shell"});
static auto rCmdNixinate = registerCommand<CmdShell>("inate");
