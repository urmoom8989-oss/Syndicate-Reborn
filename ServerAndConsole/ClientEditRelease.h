#pragma once
#include "MatchLaunchOptions.h"
// 10.40 binds EditSelect / IE_Released directly to EndSelection. Its unused
// release-and-confirm wrapper demonstrates the intended native call order.
namespace ClientEditRelease
{
using Handler=void (*)(AFortPlayerController *);
inline Handler OriginalRelease=nullptr,Confirm=nullptr;
inline bool Enabled=false;
inline DWORD RequiredThread=0;
inline void (*Note)(const std::string &)=nullptr;
struct Context
{
    Syndicate::Ref<AFortPlayerControllerAthena> Controller;
    Syndicate::Ref<AFortPlayerPawnAthena> Pawn;
    Syndicate::Ref<AFortWeap_EditingTool> Tool;
    Syndicate::Ref<ABuildingSMActor> Building;
    bool Valid()const{return Controller.Get()&&Pawn.Get()&&Tool.Get()&&Building.Get();}
    bool Same(const Context &Other)const
    {return Valid()&&Other.Valid()&&Controller.Get()==Other.Controller.Get()&&Pawn.Get()==Other.Pawn.Get()&&Tool.Get()==Other.Tool.Get()&&Building.Get()==Other.Building.Get();}
};
inline Context Capture(AFortPlayerController *Controller)
{
    Context C;auto PC=Cast<AFortPlayerControllerAthena>(Controller);
    auto W=UWorld::GetWorld();auto GI=W?W->OwningGameInstance:nullptr;
    if(!PC||!UKismetSystemLibrary::IsValid(PC)||!GI||!W->NetDriver||!W->NetDriver->ServerConnection)return C;
    bool Local=false;for(auto Player:GI->LocalPlayers)
        if(Player&&Player==PC->Player&&Player->PlayerController==PC){Local=true;break;}
    if(!Local)return C;
    auto P=Cast<AFortPlayerPawnAthena>(PC->Pawn);
    auto B=PC->EditBuildingActor;
    if(!P||!UKismetSystemLibrary::IsValid(P)||P->IsDead()||P->Controller!=PC||
       !B||!UKismetSystemLibrary::IsValid(B)||B->bDestroyed||B->IsActorBeingDestroyed())return C;
    auto Tool=Cast<AFortWeap_EditingTool>(P->CurrentWeapon);
    if(!Tool||!UKismetSystemLibrary::IsValid(Tool)||Tool->EditActor!=B||Tool->bEditConfirmed)return C;
    auto Support=B->EditModeSupport;
    if(!Support||!UKismetSystemLibrary::IsValid(Support)||Support->OwnerBuilding!=B||Support->EditingController!=PC)return C;
    C.Controller.Set(PC);C.Pawn.Set(P);C.Tool.Set(Tool);C.Building.Set(B);return C;
}
inline void Release(AFortPlayerController *PC)
{
    static thread_local bool Busy=false;
    if(!OriginalRelease)return;
    if(Busy||!Enabled||!Confirm||GetCurrentThreadId()!=RequiredThread){OriginalRelease(PC);return;}
    Busy=true;
    auto Before=Capture(PC);
    OriginalRelease(PC); // Finish the real tile stroke before committing its pattern.
    auto After=Capture(PC);
    if(Before.Same(After))
    {
        Confirm(PC); // Native manual-confirm path owns prediction, validation and RPCs.
        static bool Reported=false;if(!Reported&&Note){Reported=true;Note("Edit-on-release: native confirm requested after edit-selection release.");}
    }
    else
    {static int Reports=0;if(Note&&Reports++<3)Note("Edit-on-release: selection release observed; no matching active local edit session.");}
    Busy=false;
}
#ifndef SYNDICATE_EDIT_RELEASE_TEST
inline void Install(DWORD Thread,void (*Logger)(const std::string &))
{
    RequiredThread=Thread;Note=Logger;
    auto Preference=MatchLaunchOptions::Parse(GetCommandLineW()).EditOnRelease;
    if(!Preference.value_or(false)){if(Note)Note("Edit-on-release bridge disabled by launcher setting.");return;}
    constexpr uintptr_t ReleaseRVA=0x19B1320,ConfirmRVA=0x19B50A0;
    constexpr unsigned char ReleaseBytes[]={0x48,0x8b,0x89,0x58,0x13,0,0,0x48,0x85,0xc9,0x74,0x0a,0x48,0x8b,0x01,0x48,0xff,0xa0,0xa0,0x0b,0,0};
    constexpr unsigned char ConfirmBytes[]={0x48,0x89,0x5c,0x24,0x10,0x56,0x48,0x83,0xec,0x30,0x48,0x8b,0x19,0x48,0x8b,0xf1,0x48,0x81,0xc3,0xb8};
    auto Base=reinterpret_cast<unsigned char *>(ImageBase);
    auto DOS=reinterpret_cast<IMAGE_DOS_HEADER *>(Base);
    if(DOS->e_magic!=IMAGE_DOS_SIGNATURE||DOS->e_lfanew<=0||DOS->e_lfanew>0x1000)return;
    auto PE=reinterpret_cast<IMAGE_NT_HEADERS *>(Base+DOS->e_lfanew);
    if(PE->Signature!=IMAGE_NT_SIGNATURE||PE->OptionalHeader.SizeOfImage<=ConfirmRVA+sizeof(ConfirmBytes)||
       std::memcmp(Base+ReleaseRVA,ReleaseBytes,sizeof(ReleaseBytes))||std::memcmp(Base+ConfirmRVA,ConfirmBytes,sizeof(ConfirmBytes)))
    {if(Note)Note("Edit-on-release bridge unavailable: 10.40 native input signatures do not match.");return;}
    auto Address=Base+ReleaseRVA;
    if(MH_CreateHook(Address,reinterpret_cast<void *>(Release),reinterpret_cast<void **>(&OriginalRelease))!=MH_OK)
    {if(Note)Note("Edit-on-release bridge could not install its release hook.");return;}
    Confirm=reinterpret_cast<Handler>(Base+ConfirmRVA);Enabled=true;
    if(MH_EnableHook(Address)!=MH_OK)
    {Enabled=false;Confirm=nullptr;MH_RemoveHook(Address);if(Note)Note("Edit-on-release bridge could not enable its release hook.");return;}
    // Tell SyndicateClient.dll (same process) that select release is confirmed natively here, so it does not
    // send a second, key-press confirm for the same release (that one could reopen the edit). Kept open for the
    // life of the game.
    {wchar_t Name[64];swprintf_s(Name,L"Local\\SyndicateEditBridge_%lu",GetCurrentProcessId());CreateEventW(nullptr,TRUE,FALSE,Name);}
    if(Note)Note("Edit-on-release bridge installed for native EditSelect release (10.40).");
}
#endif
}
