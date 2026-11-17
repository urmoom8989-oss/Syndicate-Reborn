#pragma once
#include "FortInventory.h"
#include "SyndicateRuntime.h"
#include "BotPopulation.h"
#include "BotDeathLoot.h"
#include "MatchProgression.h"
namespace Elimination
{
struct Record
{
    Syndicate::Ref<AFortPlayerPawnAthena> Victim;
    Syndicate::Ref<AFortPlayerStateAthena> Killer;
    int BeforeKills = 0, BeforeTeamKills = 0;
    float Time = 0;
    bool Completed = false;
    EDeathCause Cause = EDeathCause::Unspecified;
    std::vector<FGameplayTag> Tags, ParentTags;
};
inline void (*CelebrationObserver)(AFortPlayerStateAthena *,AFortPlayerStateAthena *) = nullptr;
inline void (*LootObserver)(AFortPlayerStateAthena *,const FVector &) = nullptr;
inline UWorld *World = nullptr;
inline std::vector<Record> Records;
inline void Reset()
{
    if (World != UWorld::GetWorld())
    {
        Records.clear();
        World = UWorld::GetWorld();
    }
    std::erase_if(Records, [](auto &R) { return !R.Victim.Get(); });
}
inline EDeathCause WeaponCause(std::string Name)
{
    std::transform(Name.begin(), Name.end(), Name.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
    if (Name.contains("pickaxe") || Name.contains("harvest")) return EDeathCause::Melee;
    if (Name.contains("shotgun")) return EDeathCause::Shotgun;
    if (Name.contains("sniper")) return EDeathCause::Sniper;
    if (Name.contains("minigun")) return EDeathCause::Minigun;
    if (Name.contains("rocket")) return EDeathCause::RocketLauncher;
    if (Name.contains("grenade") && Name.contains("launcher")) return EDeathCause::GrenadeLauncher;
    if (Name.contains("grenade")) return EDeathCause::Grenade;
    if (Name.contains("bow")) return EDeathCause::Bow;
    if (Name.contains("smg") || Name.contains("pdw")) return EDeathCause::SMG;
    if (Name.contains("pistol") || Name.contains("revolver") || Name.contains("sixshooter")) return EDeathCause::Pistol;
    if (Name.contains("assault") || Name.contains("rifle")) return EDeathCause::Rifle;
    if (Name.contains("lmg")) return EDeathCause::LMG;
    return EDeathCause::Unspecified;
}
inline bool GenericCause(EDeathCause Cause)
{
    return Cause == EDeathCause::Unspecified || Cause == EDeathCause::RemovedFromGame || Cause == EDeathCause::LoggedOut;
}
inline void Observe(AFortPlayerPawnAthena *Pawn, AController *Instigator, bool DeathEvent = false,
                    const FGameplayTagContainer *DamageTags = nullptr, AActor *DamageCauser = nullptr)
{
    if (!Pawn) return;
    Reset();
    auto Killer = Instigator ? Cast<AFortPlayerStateAthena>(Instigator->PlayerState) : nullptr;
    Record *Found = nullptr;
    for (auto &R : Records)
        if (R.Victim.Get() == Pawn) { Found = &R; break; }
    if (Found && Found->Completed) return;
    bool NewRecord = !Found;
    if (!Found)
    {
        Records.push_back({Syndicate::Ref<AFortPlayerPawnAthena>(Pawn), {}, 0, 0, Syndicate::Now(), false});
        Found = &Records.back();
    }
    // Retain pre-native kill counters during a nested death callback, but
    // capture the fatal event's actual instigator/tags rather than old damage.
    if (!DeathEvent || NewRecord)
    {
        Found->BeforeKills = Killer ? Killer->KillScore : 0;
        Found->BeforeTeamKills = Killer ? Killer->TeamKillScore : 0;
    }
    Found->Killer.Set(Killer);
    Found->Time = Syndicate::Now();
    auto Tags = DamageTags ? DamageTags : &Pawn->DeathTags;
    Found->Cause = AFortPlayerStateAthena::ToDeathCause(*Tags, Pawn->bIsDBNO);
    Found->Tags.clear();
    Found->ParentTags.clear();
    for (auto Tag : Tags->GameplayTags) Found->Tags.push_back(Tag);
    for (auto Tag : Tags->ParentTags) Found->ParentTags.push_back(Tag);
    if (GenericCause(Found->Cause))
    {
        auto CauserName = DamageCauser ? DamageCauser->GetName() : std::string{};
        if (!Instigator && (CauserName.contains("SafeZone") || CauserName.contains("Storm")))
            Found->Cause = EDeathCause::OutsideSafeZone;
        else
        {
            auto Weapon = Cast<AFortWeapon>(DamageCauser);
            auto Attacker = Instigator ? Cast<AFortPlayerPawnAthena>(Instigator->Pawn) : nullptr;
            auto Definition = Weapon ? Weapon->WeaponData :
                (Attacker && Attacker->CurrentWeapon ? Attacker->CurrentWeapon->WeaponData : nullptr);
            if (Definition) Found->Cause = WeaponCause(Definition->GetName());
        }
    }
}
inline void Finish(AFortPlayerPawnAthena *Pawn)
{
    if (!Pawn)
        return;
    Reset();
    auto W = UWorld::GetWorld();
    auto GM = W ? Cast<AFortGameModeAthena>(W->AuthorityGameMode) : nullptr;
    auto GS = W ? Cast<AFortGameStateAthena>(W->GameState) : nullptr;
    auto PS = Cast<AFortPlayerStateAthena>(Pawn->PlayerState);
    if (!GM || !GS || !PS || GS->GamePhase <= EAthenaGamePhase::Warmup || (!Pawn->IsDead() && !PS->IsPlayerDead()))
        return;
    Record *Found = nullptr;
    for (auto &R : Records)
        if (R.Victim.Get() == Pawn)
        {
            Found = &R;
            break;
        }
    if (Found && Found->Completed)
        return;
    auto Killer = Found ? Found->Killer.Get() : nullptr;
    int BeforeKills = Found ? Found->BeforeKills : 0, BeforeTeamKills = Found ? Found->BeforeTeamKills : 0;
    float DamageTime = Found ? Found->Time : -100.f;
    const FVector DeathPoint=Pawn->K2_GetActorLocation(); // Capture before native drop/removal callbacks can retire the pawn.
    if (Found)
        Found->Completed = true;
    else
        Records.push_back({Syndicate::Ref<AFortPlayerPawnAthena>(Pawn), {}, 0, 0, Syndicate::Now(), true});
    // This client executable can omit replicated feed data on real AI death.
    // Preserve a native record and never publish a living/DBNO elimination.
    EDeathCause Cause = Found && !GenericCause(Found->Cause) ? Found->Cause :
        AFortPlayerStateAthena::ToDeathCause(Pawn->DeathTags, false);
    std::vector<FGameplayTag> Tags = Found ? Found->Tags : std::vector<FGameplayTag>{};
    std::vector<FGameplayTag> ParentTags = Found ? Found->ParentTags : std::vector<FGameplayTag>{};
    if (Tags.empty())
        for (auto Tag : Pawn->DeathTags.GameplayTags) Tags.push_back(Tag);
    if (ParentTags.empty())
        for (auto Tag : Pawn->DeathTags.ParentTags) ParentTags.push_back(Tag);
    if ((!PS->DeathInfo.bInitialized || (GenericCause(PS->DeathInfo.DeathCause) && !GenericCause(Cause))) && !Pawn->bIsDBNO)
    {
        auto &Info = PS->DeathInfo;
        Info.DeathLocation = Pawn->K2_GetActorLocation();
        Info.bDBNO = false;
        Info.DeathCause = Cause;
        Info.DeathTags.GameplayTags.Clear();
        Info.DeathTags.ParentTags.Clear();
        for (const auto &Tag : Tags)
            Info.DeathTags.GameplayTags.Add(Tag);
        for (const auto &Tag : ParentTags)
            Info.DeathTags.ParentTags.Add(Tag);
        Info.FinisherOrDowner = Killer;
        Info.Downer = Killer;
        auto Owner = Killer ? Cast<AController>(Killer->Owner) : nullptr;
        auto KillerPawn = Owner ? Cast<AFortPlayerPawn>(Owner->Pawn) : nullptr;
        Info.Distance = KillerPawn ? (KillerPawn->K2_GetActorLocation() - Info.DeathLocation).Magnitude() : 0.f;
        Info.bInitialized = true; // Mark before re-entrant native notification.
        PS->OnRep_DeathInfo();
        PS->ForceNetUpdate();
    }
    auto Controller = Pawn->Controller;
    if (!Controller)
        Controller = Cast<AController>(PS->Owner);
    auto Bot = Cast<AFortAthenaAIBotController>(Controller);
    if (!Bot)
        for (auto Candidate : GM->AliveBots)
            if (Candidate && Candidate->PlayerState == PS)
            {
                Bot = Candidate;
                break;
            }
    if (Bot)
    {
        Syndicate::Ref<AFortPlayerPawnAthena> PawnGuard(Pawn);
        Syndicate::Ref<AFortPlayerStateAthena> StateGuard(PS), KillerGuard(Killer);
        Syndicate::Ref<AFortGameModeAthena> ModeGuard(GM);
        Syndicate::Ref<AFortGameStateAthena> GameStateGuard(GS);
        Syndicate::Ref<AFortAthenaAIBotController> BotGuard(Bot);
        // The native callback ran first; drop only remaining real inventory.
        // Completed is set before any spawn callbacks, preventing duplicate loot.
        BotDeathLoot::Drop(Bot, Pawn);
        Pawn = PawnGuard.Get(); PS = StateGuard.Get(); Killer = KillerGuard.Get();
        GM = ModeGuard.Get(); GS = GameStateGuard.Get(); Bot = BotGuard.Get();
        if (!Pawn || !PS || !GM || !GS || !Bot) return;
        // A real native death has already run. Bound corpse lifetime if the
        // client executable omitted its server cleanup timer, preserving an
        // earlier native timer and never destroying a living/DBNO pawn.
        if (Pawn->IsDead())
        {
            float Remaining = Pawn->GetLifeSpan();
            if (Remaining <= 0.f || Remaining > 5.f)
                Pawn->SetLifeSpan(5.f);
        }
        for (int i = GM->AliveBots.Num() - 1; i >= 0; --i)
            if (GM->AliveBots[i] == Bot)
                GM->AliveBots.Remove(i);
        if (auto Manager = GM->ServerBotManager)
            for (int i = Manager->PlayerBots.Num() - 1; i >= 0; --i)
                if (Manager->PlayerBots[i].BotController == Bot)
                {
                    FFortServerBotInfo Info{};
                    Info.BotController = Bot;
                    bool Recorded = false;
                    for (auto Dead : Manager->DeadPlayerBots)
                        Recorded |= Dead.BotController == Bot;
                    if (!Recorded)
                        Manager->DeadPlayerBots.Add(Info);
                    Manager->PlayerBots.Remove(i);
                }
    }
    else if (auto PC = Cast<AFortPlayerControllerAthena>(Controller))
    {
        bool Alive = false;
        for (auto P : GM->AlivePlayers)
            if (P == PC)
            {
                Alive = true;
                break;
            }
        if (Alive)
        {
            auto Owner = Killer ? Cast<AController>(Killer->Owner) : nullptr;
            auto KillerPawn = Owner ? Cast<AFortPlayerPawn>(Owner->Pawn) : nullptr;
            RemoveFromAlivePlayers(GM, PC, Killer, KillerPawn,
                                   KillerPawn && KillerPawn->CurrentWeapon ? KillerPawn->CurrentWeapon->WeaponData
                                                                           : nullptr,
                                   static_cast<uint8_t>(PS->DeathInfo.DeathCause), 0);
        }
    }
    if (Killer && Killer != PS && Killer->TeamIndex != PS->TeamIndex && Syndicate::Now() - DamageTime < 10.f)
    {
        if (Killer->KillScore == BeforeKills)
        {
            ++Killer->KillScore;
            Killer->OnRep_Kills();
            Killer->ClientReportKill(PS);
            MatchProgression::Kill(Killer); // 100 XP and 25 V-Bucks for a human killer (backend pays none with the menu on)
        }
        if (Killer->TeamKillScore == BeforeTeamKills)
        {
            ++Killer->TeamKillScore;
            Killer->OnRep_TeamKillScore();
        }
    }
    if(CelebrationObserver && Killer && Killer!=PS && Killer->TeamIndex!=PS->TeamIndex && Syndicate::Now()-DamageTime<10.f)CelebrationObserver(Killer,PS);
    if(LootObserver && Killer && Killer!=PS && Killer->TeamIndex!=PS->TeamIndex && Syndicate::Now()-DamageTime<10.f)
        LootObserver(Killer,DeathPoint);
    BotPopulation::Reconcile(GM, GS);
    int Alive = GM->AliveBots.Num() + GM->AlivePlayers.Num();
    if (PS->Place <= 0)
    {
        PS->Place = Alive + 1;
        PS->OnRep_Place();
    }
    MatchProgression::Finished(PS); // placement for a human victim
    // Native health/death/effects/drop processing precedes delayed actor cleanup.
}
} // namespace Elimination
