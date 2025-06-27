#include "nix/cmd/command.hh"
#include "nix/cmd/activatables.hh"
#include "nix/util/users.hh"
#include "nix/util/current-process.hh"

using namespace nix;

void execute(std::string program, Strings args) {
    logger->stop();
    restoreProcessContext();
    args.push_front(program);
    execvp(program.c_str(), stringsToCharPtrs(args).data());

    throw SysError("unable to execute '%s'", program);
}

struct HomeCommand : ActivatableCommand
{
    HomeCommand()
        : ActivatableCommand("activationPackage")
    {}

    Strings getDefaultFlakeAttrPaths() override
    {
        auto username = getUserName();
        char hostname[1024];
        hostname[1023] = '\0';
        gethostname(hostname, 1024);

        Strings res{
            "homeConfigurations." + username,
            "homeConfigurations." + username + "@" + std::string(hostname),
        };
        return res;
    }

    Strings getDefaultFlakeAttrPathPrefixes() override
    {
        Strings res{"homeConfigurations."};
        return res;
    }

};

struct HomeActivationCommand : ActivationCommand<HomeCommand>
{
    HomeActivationCommand(std::string activationType, std::string selfCommandName)
        : ActivationCommand<HomeCommand>(activationType, selfCommandName)
    { }

    void runActivatable(nix::ref<Store> store, ref<Installable> installable) override
    {
        auto out = buildActivatable(store, installable);
        execute(store->printStorePath(out) + "/activate", Strings{});
    }
};

struct CmdHomeBuild : ActivatableBuildCommand<HomeCommand>
{
    std::string description() override
    {
        return "build a home-manager configuration";
    }

    std::string doc() override
    {
        return
          #include "home-build.md"
          ;
    }
};

struct CmdHomeApply : HomeActivationCommand
{
    CmdHomeApply() : HomeActivationCommand("switch", "apply")
    {
    }

    std::string description() override
    {
        return "activate a home-manager configuration";
    }

    std::string doc() override
    {
        return
          #include "home-apply.md"
          ;
    }
};

struct CmdHome : NixMultiCommand
{
    CmdHome()
        : NixMultiCommand("home", {
                {"build", []() { return make_ref<CmdHomeBuild>(); }},
                {"apply", []() { return make_ref<CmdHomeApply>(); }},
            })
    {
    }

    std::string description() override
    {
        return "manage home-manager configurations";
    }

    std::string doc() override
    {
        return
          #include "home.md"
          ;
    }

    void run() override
    {
        if (!command)
            throw UsageError("'nix home' requires a sub-command.");
        command->second->run();
    }
};

static auto rCmdHome = registerCommand<CmdHome>("home");
