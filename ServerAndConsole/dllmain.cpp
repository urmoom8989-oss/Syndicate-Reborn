#include "framework.h"
#include "PE.h"
#include "Misc.h"
#include "GameMode.h"
#include "Controller.h"
#include "AbilitySystemComponent.h"
#include "Pawn.h"
#include "FortWeapon.h"
#include "FortPickup.h"
#include "BuildingActor.h"
#include "BuildingContainer.h"
#include "Net.h"
#include "NetDriver.h"
#include "AIController.h"
#include "ServerBotManager.h"
#include "InteractionLoot.h"
#include "MatchLaunchOptions.h"
#include "BotNames.h"

// The loader worker installs the dispatcher. An actor ReceiveTick identifies the
// engine game thread even with -nullrhi; UObject mutation and travel stay there.
inline void InitConsole()
{
    if (!GetConsoleWindow())
        AllocConsole();
    FILE *Stream = nullptr;
    freopen_s(&Stream, "CONOUT$", "w", stdout);
    SetConsoleTitleA("Syndicate 10.40 | Waiting for login");
    Log("Syndicate 10.40 server initialization queued.");
    auto Options=MatchLaunchOptions::Parse(GetCommandLineW());
    Globals::MinBotsToSpawn=Options.Minimum;Globals::MaxBotsToSpawn=Options.Maximum;
    if(!BotDifficulty::Configure(GetCommandLineW()))Log("Invalid difficulty options; keeping default skill settings.");
    int LoadedBotNames=ConfigureBotNames();
    if(LoadedBotNames>0)Log("Bot names: "+std::to_string(LoadedBotNames)+" name roster loaded.");
    Log("Build: ResourceMovementFix - material limits, harvesting, retained ramp travel and box pressure.");
    Log(BotDifficulty::Mixed ? "Bot difficulty: Mixed (stable per-bot levels)." : "Bot difficulty: custom/fixed slider profile loaded.");
}
inline bool InstallServerHooks()
{
    PE::HookAll();
    PlayerBots::NativePhaseFunction = AFortAthenaAIBotController::StaticClass()->GetFunction(
        "FortAthenaAIBotController", "OnGamePhaseChanged");
    PlayerBots::NativePerceptionFunction=AFortAthenaAIBotController::StaticClass()->GetFunction("FortAthenaAIBotController","OnPerceptionSensed");
    PlayerBots::NativeNoiseFunction=UFortAIFunctionLibrary::StaticClass()->GetFunction("FortAIFunctionLibrary","MakeNoiseEvent");
    PlayerBots::NativeNoiseLocationFunction=UFortAIFunctionLibrary::StaticClass()->GetFunction("FortAIFunctionLibrary","MakeNoiseEventAtLocation");
    PlayerBots::NativeReportNoiseFunction=UAISense_Hearing::StaticClass()->GetFunction("AISense_Hearing","ReportNoiseEvent");
    PE::EventObserver = [](UObject *Object,UFunction *Function,void *Parameters,bool Before)
    {
        if(PE::GameThread.load() && GetCurrentThreadId()==PE::GameThread.load())
        {
            PlayerVehicles::ObserveMove(Object,Function,Parameters,Before);
            PlayerTraps::Observe(Object,Function,Parameters,Before);
            PlayerBots::PhaseEvent(Object,Function,Parameters,Before);
        }
    };
    Elimination::LootObserver=PlayerBots::ObserveKillLoot;
    Elimination::CelebrationObserver=PlayerBots::ObserveCelebration;
    PE::HandledEvent = [](UObject *Object,UFunction *Function,void *Parameters)
    {
        return PE::GameThread.load() && GetCurrentThreadId()==PE::GameThread.load() &&
               InteractionLoot::Handle(Object,Function,Parameters);
    };
    PE::DamageObserver = PlayerBots::ObserveDamage;
    PE::StormDamageObserver = PlayerBots::ObserveStormDamage;
    AbilitySystemComponent::InputObserver = [](UAbilitySystemComponent *C,FGameplayAbilitySpecHandle H,bool Pressed)
    {if(PE::GameThread.load() && GetCurrentThreadId()==PE::GameThread.load()) PlayerBots::ObserveAbilityFire(C,H,Pressed);};
    Misc::HookAll();
    GameMode::HookAll();
    Controller::HookAll();
    AbilitySystemComponent::HookAll();
    Pawn::HookAll();
    FortWeapon::HookAll();
    FortPickup::HookAll();
    BuildingActor::DamageObserver=PlayerBots::ObserveCoverDamage;
    BuildingActor::HookAll();
    BuildingContainer::HookAll();
    Net::HookAll();
    NetDriver::HookAll();
    AIController::HookAll();
    ServerBotManager::HookAll();
    if (HookFailure)
    {
        RollBackVTables();
        Log("Server startup stopped because a required hook failed. Check executable/SDK version.");
        return false;
    }
    auto Status = MH_EnableHook(MH_ALL_HOOKS);
    if (Status != MH_OK)
    {
        RollBackVTables();
        Log("Server hooks could not be enabled: " + std::to_string(static_cast<int>(Status)));
        return false;
    }
    return true;
}
inline bool StartServerOnGameThread()
{
    auto W = UWorld::GetWorld();
    static std::string LastWait;
    auto Waiting = [](const std::string &Reason)
    {
        if (LastWait != Reason)
        {
            LastWait = Reason;
            Log("Startup waiting: " + Reason);
        }
        return false;
    };
    if (!W || !W->OwningGameInstance)
        return Waiting("game viewport world/game instance");
    auto WorldName = W->Name.ToString();
    if (WorldName != "Frontend")
        return Waiting("Frontend world (current: " + WorldName + ")");
    auto Mode = Cast<AFortGameModeFrontEnd>(W->AuthorityGameMode);
    if (!Mode)
        return Waiting("Frontend game mode");
    auto State = Mode->GetMatchState().ToString();
    if (State != "InProgress")
        return Waiting("login completion (match state: " + State + ")");
    SetConsoleTitleA("Syndicate 10.40 | Starting Athena");
    Log("Frontend login complete; installing server hooks.");
    auto Version = UKismetSystemLibrary::GetEngineVersion().ToString();
    Log("Engine build: " + Version);
    if (!Version.contains("10.40") || !Version.contains("9380822"))
    {
        Log("Startup refused: this package requires Fortnite 10.40 CL-9380822.");
        return true;
    }
    if (Globals::bCreativeEnabled || Globals::bSTWEnabled)
    {
        Log("This overhaul supports the Athena solo AI server. Disable Creative/STW options.");
        return true;
    }
    if (!InstallServerHooks())
        return true;
    // These two existing 10.40 globals select dedicated-server operation.
    auto ClientFlag = reinterpret_cast<bool *>(ImageBase + 0x637925B);
    auto ServerFlag = reinterpret_cast<bool *>(ImageBase + 0x637925C);
    MEMORY_BASIC_INFORMATION ClientPage{}, ServerPage{};
    if (!VirtualQuery(ClientFlag, &ClientPage, sizeof(ClientPage)) ||
        !VirtualQuery(ServerFlag, &ServerPage, sizeof(ServerPage)) ||
        !(ClientPage.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) ||
        !(ServerPage.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
    {
        MH_DisableHook(MH_ALL_HOOKS);
        RollBackVTables();
        Log("Server globals were not writable; startup stopped.");
        return true;
    }
    *ClientFlag = false;
    *ServerFlag = true;
    if (W->OwningGameInstance->LocalPlayers.Num() > 0)
        W->OwningGameInstance->LocalPlayers.Remove(0);
    ResolveBuildingAssets();
    NativeNavigation::PrepareBeforeTravel();
    Log("Loading Athena on the game thread.");
    UKismetSystemLibrary::ExecuteConsoleCommand(W, L"open Athena_Terrain", nullptr);
    return true;
}
#ifdef SYNDICATE_LOCAL_TEST_JOIN
// Diagnostic builds only. This follows the same engine command as the human
// console after normal local login; it never installs server hooks in a client.
inline bool JoinLocalTestOnGameThread()
{
    auto W=UWorld::GetWorld();
    if (!W || !W->OwningGameInstance || W->Name.ToString()!="Frontend" ||
        W->OwningGameInstance->LocalPlayers.Num()==0) return false;
    auto Mode=Cast<AFortGameModeFrontEnd>(W->AuthorityGameMode);
    if (!Mode || Mode->GetMatchState().ToString()!="InProgress") return false;
    auto Version=UKismetSystemLibrary::GetEngineVersion().ToString();
    if (!Version.contains("10.40") || !Version.contains("9380822")) return true;
    Log("Diagnostic client requested native local travel after login.");
    UKismetSystemLibrary::ExecuteConsoleCommand(W,L"open 127.0.0.1",nullptr);
    return true;
}
#endif
DWORD WINAPI Main(LPVOID)
{
    bool TestClient=false;
#ifdef SYNDICATE_LOCAL_TEST_JOIN
    TestClient=std::wstring_view(GetCommandLineW()).find(L"-SyndicateLocalTestJoin")!=std::wstring_view::npos;
#endif
    if (!TestClient) InitConsole();
    if (MH_Initialize() != MH_OK)
    {
        Log("MinHook initialization failed.");
        return 1;
    }
    auto EngineThreadId = Syndicate::EngineThread::FindGameWindowThread();
    if (!EngineThreadId)
    {
        Log("Server startup stopped: could not identify the engine window thread.");
        return 1;
    }
    PE::EngineThreadId.store(EngineThreadId);
    Log("Startup engine thread: " + std::to_string(EngineThreadId));
    PE::Bootstrap.store(StartServerOnGameThread);
#ifdef SYNDICATE_LOCAL_TEST_JOIN
    if (TestClient) PE::Bootstrap.store(JoinLocalTestOnGameThread);
#endif
    if (!PE::InstallDispatcher() || MH_EnableHook(reinterpret_cast<void *>(ImageBase + Offsets::ProcessEvent)) != MH_OK)
    {
        PE::Bootstrap.store(nullptr);
        Log("Game-thread startup dispatcher failed.");
        return 1;
    }
    Log("Startup dispatcher installed; waiting for native actor tick (headless mode supported).");
    for (int Attempt = 0; Attempt < 18 && PE::Bootstrap.load(); ++Attempt)
    {
        Sleep(10000);
        if (!PE::Bootstrap.load())
            break;
        auto Thread = PE::GameThread.load();
        Log(Thread ? "Startup callback active; waiting for Frontend login."
                   : "Waiting for an actor ReceiveTick; reflected events observed: " +
                         std::to_string(PE::StartupEvents.load()));
    }
    return 0;
}
BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Module);
        HANDLE Worker = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr);
        if (Worker)
            CloseHandle(Worker);
    }
    return TRUE;
}
