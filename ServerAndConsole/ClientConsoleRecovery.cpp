#include "SyndicateRuntime.h"
#include "EngineThread.h"
#include <filesystem>
#include "MatchLaunchOptions.h"
#include "ClientEditRelease.h"
#include "MouseCoalesce.h"

// Player-only companion. It never installs game-server hooks, changes net mode,
// polls global keyboard input or creates a Windows console. It also routes the
// main-lobby Play button to the local server.
namespace ClientConsoleRecovery
{
inline void (*Original)(UObject *,UFunction *,void *)=nullptr;
inline DWORD EngineThread=0;
inline FName TickName{};
inline bool FoundTick=false;
inline std::chrono::steady_clock::time_point Next;
inline unsigned int Repairs=0;
inline std::string LastWorld;
inline bool PendingPlay=false;
inline FName PlayClickName{};
inline bool PlayClickNameReady=false;
inline std::chrono::steady_clock::time_point NextPlay;
inline void Record(const std::string &Text)
{
    wchar_t Temp[MAX_PATH]{};if(!GetTempPathW(MAX_PATH,Temp)) return;
    std::ofstream File(std::filesystem::path(Temp)/(L"SyndicateConsoleRecovery-"+std::to_wstring(GetCurrentProcessId())+L".log"),std::ios::app);
    File<<Text<<std::endl;
}
inline void PrimePlayClickName()
{
    // Conv_StringToName is itself a Blueprint call. Prime it only from
    // Repair(), where Busy prevents this ProcessEvent hook from re-entering.
    if(PlayClickNameReady) return;
    PlayClickName=UKismetStringLibrary::Conv_StringToName(
        L"BndEvt__PlayButton_K2Node_ComponentBoundEvent_0_CommonButtonClicked__DelegateSignature");
    PlayClickNameReady=true;
}
inline bool IsPlayClick(UObject *Object,UFunction *Function)
{
    if(!PlayClickNameReady || !Function || Function->Name!=PlayClickName) return false;
    std::string ClassName;
    if(Object && Object->Class) ClassName=Object->Class->GetName();
    if(ClassName!="AthenaMatchmakingPlayLegacy_C")
    {
        Record("Ignoring Play click from "+ClassName+" (main-lobby Play only).");
        return false;
    }
    Record("Main-lobby Play button clicked.");
    return true;
}
inline void Repair()
{
    PrimePlayClickName();
    auto W=UWorld::GetWorld();
    auto GI=W ? W->OwningGameInstance : nullptr;
    auto Player=GI && GI->LocalPlayers.Num()>0 ? GI->LocalPlayers[0] : nullptr;
    if(PendingPlay)
    {
        PendingPlay=false;
        if(W && W->Name.ToString()=="Frontend")
        {
            auto Now=std::chrono::steady_clock::now();
            if(Now>=NextPlay)
            {
                NextPlay=Now+std::chrono::seconds(5);
                Record("Main-lobby Play button launched local travel to 127.0.0.1.");
                UKismetSystemLibrary::ExecuteConsoleCommand(W,L"open 127.0.0.1",nullptr);
            }
        }
    }
    auto Viewport=Player ? Player->ViewportClient : nullptr;
    if(!Viewport || !UKismetSystemLibrary::IsValid(Viewport)) return;
    auto Console=Viewport->ViewportConsole;
    bool Changed=false;
    if(!Console || !UKismetSystemLibrary::IsValid(Console))
    {
        Console=Cast<UConsole>(UGameplayStatics::SpawnObject(UConsole::StaticClass(),Viewport));
        if(!Console) return;
        Viewport->ViewportConsole=Console;Changed=true;
    }
    if(Console->ConsoleTargetPlayer!=Player) {Console->ConsoleTargetPlayer=Player;Changed=true;}
    auto Input=UInputSettings::GetInputSettings();
    if(Input)
        for(auto Text:{L"Tilde",L"F8"})
        {
            auto Name=UKismetStringLibrary::Conv_StringToName(Text);
            bool Present=false;for(const auto &Key:Input->ConsoleKeys) if(Key.KeyName==Name) {Present=true;break;}
            if(!Present) {FKey Key{};Key.KeyName=Name;Input->ConsoleKeys.Add(Key);Changed=true;}
        }
    static auto Preferences=MatchLaunchOptions::Parse(GetCommandLineW());
    if(Preferences.EditOnRelease.has_value())
        if(auto Local=Cast<UFortLocalPlayer>(Player))
            if(auto Settings=Local->GetClientSettings();Settings&&UKismetSystemLibrary::IsValid(Settings)&&Settings->GetEditConfirmOnReleaseEnabled()!=*Preferences.EditOnRelease)
            {Settings->SetEditConfirmOnReleaseEnabled(*Preferences.EditOnRelease);Record("Native confirm-edit-on-release applied: "+std::to_string(*Preferences.EditOnRelease));}
    auto WorldName=W->GetName();
    if(Changed || LastWorld!=WorldName)
    {
        ++Repairs;
        LastWorld=WorldName;
        Record("world="+WorldName+", console="+Console->GetName()+", currentLocalPlayer="+
            std::string(Console->ConsoleTargetPlayer==Player ? "yes" : "no")+", Tilde/F8=bound, repairs="+std::to_string(Repairs));
        OutputDebugStringA("Syndicate client console rebound to the current local player/viewport.\n");
    }
#ifdef SYNDICATE_LOCAL_TEST_JOIN
    static bool Joined=false;
    if(!Joined && std::wstring_view(GetCommandLineW()).find(L"-SyndicateLocalTestJoin")!=std::wstring_view::npos && W->Name.ToString()=="Frontend")
        if(auto Mode=Cast<AFortGameModeFrontEnd>(W->AuthorityGameMode); Mode && Mode->GetMatchState().ToString()=="InProgress")
        {
            Joined=true;Record("Diagnostic console test requested local travel.");
            UKismetSystemLibrary::ExecuteConsoleCommand(W,L"open 127.0.0.1",nullptr);
        }
#endif
}
inline void Event(UObject *Object,UFunction *Function,void *Parameters)
{
    static thread_local bool Busy=false;
    if(!Busy && FoundTick && Function && GetCurrentThreadId()==EngineThread && IsPlayClick(Object,Function))
    {
        auto PlayWorld=UWorld::GetWorld();
        if(PlayWorld && PlayWorld->Name.ToString()=="Frontend" &&
           std::chrono::steady_clock::now()>=NextPlay)
        {
            PendingPlay=true;
            Record("Main-lobby Play button requested local travel.");
            return; // Suppress stock matchmaking; the next game tick opens the local server.
        }
    }
    if(Original) Original(Object,Function,Parameters);
    if(Busy || !Function || GetCurrentThreadId()!=EngineThread) return;
    if(!FoundTick)
    {
        if(Function->Name.ToString()!="ReceiveTick" || !Cast<AActor>(Object)) return;
        TickName=Function->Name;FoundTick=true;
    }
    if(Function->Name!=TickName) return;
    auto Now=std::chrono::steady_clock::now();if(Now<Next) return;Next=Now+std::chrono::milliseconds(250);
    Busy=true;Repair();Busy=false;
}
// Every message the game thread takes off its queue: high polling rate mouse movement is merged (MouseCoalesce.h).
LRESULT CALLBACK OnGameMessage(int Code,WPARAM W,LPARAM L)
{
    if(Code>=0 && W==PM_REMOVE) MouseCoalesce::Filter(reinterpret_cast<MSG *>(L));
    return CallNextHookEx(nullptr,Code,W,L);
}
DWORD WINAPI Main(LPVOID)
{
    for(int I=0;I<120 && !EngineThread;++I) {EngineThread=Syndicate::EngineThread::FindGameWindowThread();if(!EngineThread) Sleep(250);}
    if(!EngineThread || MH_Initialize()!=MH_OK) return 1;
    auto Address=reinterpret_cast<void *>(ImageBase+Offsets::ProcessEvent);
    if(MH_CreateHook(Address,reinterpret_cast<void *>(Event),reinterpret_cast<void **>(&Original))!=MH_OK) return 1;
    if(MH_EnableHook(Address)!=MH_OK)return 1;
    ClientEditRelease::Install(EngineThread,&Record);
    if(MouseCoalesce::Install(EngineThread) && SetWindowsHookExW(WH_GETMESSAGE,OnGameMessage,nullptr,EngineThread))
        Record("High polling rate mouse input is merged (2000-8000 Hz mice only; stops their stutter).");
    else Record("Mouse polling-rate merge is off.");
    return 0;
}
}
BOOL APIENTRY DllMain(HMODULE Module,DWORD Reason,LPVOID)
{
    if(Reason==DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Module);
        if(auto Worker=CreateThread(nullptr,0,ClientConsoleRecovery::Main,nullptr,0,nullptr)) CloseHandle(Worker);
    }
    return TRUE;
}
