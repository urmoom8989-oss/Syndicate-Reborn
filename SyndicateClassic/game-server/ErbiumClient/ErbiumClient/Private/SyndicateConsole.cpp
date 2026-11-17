#include "pch.h"
#include "../Public/FortPlayerControllerAthena.h"

// Create the standard UE console on the window/game thread, not the DLL loader
// thread. Initialize the native command manager for each local controller.
namespace {
    constexpr UINT EnableMessage=WM_APP+0x4A2;
    HMODULE SelfModule=nullptr;
    void Trace(const char* Text){
        wchar_t Path[32768]{};if(!GetModuleFileNameW(SelfModule,Path,32768))return;
        auto End=wcsrchr(Path,L'\\');if(!End)return;*(End+1)=0;
        std::wstring File=std::wstring(Path)+L"console-diagnostics.log";FILE* Out=nullptr;
        if(_wfopen_s(&Out,File.c_str(),L"a")==0 && Out){fprintf(Out,"pid=%lu %s\n",GetCurrentProcessId(),Text);fclose(Out);}
    }
    HHOOK MessageHook=nullptr;
    volatile LONG Complete=0;
    UObject* LastCommandManager=nullptr;
    HWND GameWindow=nullptr;
    LRESULT CALLBACK OnMessage(int Code,WPARAM W,LPARAM L) {
        if(Code>=0 && W==PM_REMOVE && reinterpret_cast<MSG*>(L)->message==EnableMessage) {
            auto Engine=UEngine::GetEngine();
            auto View=Engine?Engine->GameViewport:nullptr;
            if(View && Engine->ConsoleClass) {
                if(!View->ViewportConsole)
                    View->ViewportConsole=UGameplayStatics::SpawnObject(Engine->ConsoleClass,View);
                if(View->ViewportConsole) {
                    if(InterlockedExchange(&Complete,1)==0){OutputDebugStringA("Syndicate: in-game console enabled (Tilde/F8).\n");Trace("native viewport console created");}
                }
            }
            auto World=UWorld::GetWorld();
            auto Instance=World?World->OwningGameInstance:nullptr;
            if(Instance)for(auto Player:Instance->LocalPlayers){
                auto PC=Player?(AFortPlayerControllerAthena*)Player->PlayerController:nullptr;
                if(PC && !PC->CheatManager){
                    auto Class=PC->CheatClass.Get();if(!Class)Class=FindClass("FortCheatManager");
                    if(Class){PC->CheatManager=(UFortCheatManager*)UGameplayStatics::SpawnObject(Class,PC);if(PC->CheatManager)Trace("native FortCheatManager created for local player");}
                }
                if(PC && PC->CheatManager && PC->CheatManager!=LastCommandManager){LastCommandManager=PC->CheatManager;Trace("local console command manager available");}
            }
        }
        return CallNextHookEx(MessageHook,Code,W,L);
    }
    BOOL CALLBACK FindWindow(HWND Window,LPARAM) {
        DWORD Pid=0;GetWindowThreadProcessId(Window,&Pid);
        wchar_t Class[128]{};GetClassNameW(Window,Class,128);
        if(Pid==GetCurrentProcessId() && IsWindowVisible(Window) && wcsstr(Class,L"UnrealWindow")) {GameWindow=Window;return FALSE;}
        return TRUE;
    }
    DWORD WINAPI Start(void*) {
        // The launcher loads this after Cobalt and the game window are ready.
        for(int Attempt=0;Attempt<240 && !GameWindow;++Attempt){EnumWindows(FindWindow,0);if(!GameWindow)Sleep(500);}
        if(!GameWindow)return 0;
        SDK::Init();
        if(VersionInfo.FortniteVersion<4 || VersionInfo.FortniteVersion>=5)return 0;
        int MissingWindow=0;
        while(MissingWindow<240) {
            if(!IsWindow(GameWindow)){
                if(MessageHook){UnhookWindowsHookEx(MessageHook);MessageHook=nullptr;}
                GameWindow=nullptr;EnumWindows(FindWindow,0);
            }
            MissingWindow=GameWindow?0:MissingWindow+1;
            if(GameWindow) {
                if(!MessageHook)MessageHook=SetWindowsHookExW(WH_GETMESSAGE,OnMessage,nullptr,GetWindowThreadProcessId(GameWindow,nullptr));
                if(MessageHook)PostMessageW(GameWindow,EnableMessage,0,0);
            }
            Sleep(2000);
        }
        if(MessageHook){UnhookWindowsHookEx(MessageHook);MessageHook=nullptr;}
        return 0;
    }
}
BOOL APIENTRY DllMain(HMODULE Module,DWORD Reason,LPVOID) {
    if(Reason==DLL_PROCESS_ATTACH) {
        SelfModule=Module;
        DisableThreadLibraryCalls(Module);
        if(auto Thread=CreateThread(nullptr,0,Start,nullptr,0,nullptr))CloseHandle(Thread);
    }
    return TRUE;
}
