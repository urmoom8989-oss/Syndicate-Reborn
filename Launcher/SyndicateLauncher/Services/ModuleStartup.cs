using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace SyndicateLauncher.Services;

public static class ModuleStartup
{
    public const string MenuModule = "SyndicateMenu.dll";
    public static IReadOnlyList<string> RequiredModules { get; } = Array.AsReadOnly(new[]
        { "memory.dll", "Erbium.dll", "cobalt.dll", "console.dll", "Syndicate1040.dll", "SyndicateConsoleRecovery.dll", MenuModule });
    // GameLauncher loads Erbium only in the host and cobalt only in the player.
    // These additional 10.40 player modules still wait for the engine and login.
    public static async Task InitializeClientAsync(bool enableMenu, Func<string, Task> load,
        Func<Task> waitForEngine, Func<Task> waitForLogin)
    {
        await waitForEngine();
        await waitForLogin();
        await load("console.dll");
        await load("SyndicateConsoleRecovery.dll");
        if (enableMenu) await load(MenuModule);
    }
}
