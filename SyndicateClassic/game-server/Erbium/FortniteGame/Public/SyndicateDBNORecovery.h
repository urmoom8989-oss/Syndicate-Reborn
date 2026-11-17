#pragma once
#include "FortPlayerPawnAthena.h"
#include "FortPlayerControllerAthena.h"

namespace SyndicateDBNO {
inline bool SeasonFour() { return VersionInfo.FortniteVersion>=4 && VersionInfo.FortniteVersion<5; }
// Seasons whose custom knock-recovery paths have been validated. Used by the Oct 20
// bot integration; kept equal to Season 4 so behavior matches the last tested build.
inline bool RecoverySupported() { return SeasonFour(); }
// Seasons 3-10: knocked bots have no client to report a finished revive, so the server
// times human->bot revives itself (was Season 4 only, so bots could not be revived).
inline bool RevivePathSupported() { return VersionInfo.FortniteVersion>=3.5 && VersionInfo.FortniteVersion<12; }
// Pre-season to Season 2 (1.7.2-1.11): a knocked BOT can only be revived by a real player through this server-timed
// path (the game finishes revives through the knocked pawn's own client, and a bot has none). Real players knocked
// in these seasons keep the game's own revive.
inline bool LegacyBotRevive() { return VersionInfo.FortniteVersion<3.5; }
inline bool HumanBotRevivePath() { return RevivePathSupported() || LegacyBotRevive(); }
inline void RestoreControl(AFortPlayerPawnAthena* Pawn) {
    if(!HumanBotRevivePath() || !Pawn || Pawn->bActorIsBeingDestroyed)return;
    Pawn->bTearOff=false;
    for(auto Name:{"bIsDying","bPlayedDying","bIsHiddenForDeath"})
        if(auto P=Pawn->GetProperty(Name)){auto O=Pawn->GetOffset(Name);if(O!=uint32(-1))GetFromOffset<uint8>(Pawn,O)&=~P->GetFieldMask();}
    if(auto F=Pawn->GetFunction("SetLifeSpan"))Pawn->Call<void>(F,0.f);
    if(auto F=Pawn->GetFunction("SetStasisMode"))if(F->GetParamsNamed().Size==1)Pawn->Call<void>(F,uint8(0));
    if(auto Move=Pawn->CharacterMovement){auto O=Move->GetOffset("MovementMode");
        if(O!=uint32(-1) && GetFromOffset<uint8>(Move,O)==0)
            if(auto F=Move->GetFunction("SetMovementMode"))Move->Call<void>(F,uint8(1),uint8(0));}
    auto PC=Pawn->Controller?Pawn->Controller->Cast<AFortPlayerControllerAthena>():nullptr;
    if(PC){
        if(auto F=PC->GetFunction("ResetIgnoreMoveInput"))if(F->GetParamsNamed().Size==0)PC->ProcessEvent(F,nullptr);
        PC->ClientIgnoreMoveInput(false);
    }
    Pawn->ForceNetUpdate();
}
// Revive diagnostics: one line per decision, so a failed revive shows why.
inline void ReviveLog(const char* Text){static ULONGLONG Last=0;static std::string Prev;auto Now=GetTickCount64();if(Prev==Text && Now-Last<2000)return;Prev=Text;Last=Now;printf("[SyndicateRevive] %s\n",Text);}
inline bool ValidReviver(AFortPlayerPawnAthena* Pawn,AFortPlayerControllerAthena* Instigator) {
    if(!Pawn || !Instigator || !Instigator->IsA(AFortPlayerControllerAthena::StaticClass()) || Pawn->bActorIsBeingDestroyed){ReviveLog("reject: no target or reviver");return false;}
    if(!Pawn->IsDBNO()){ReviveLog("reject: target is not knocked");return false;}
    auto Other=Instigator->MyFortPawn;
    if(!Other || Other==Pawn || Other->bActorIsBeingDestroyed || Other->IsDBNO() || Other->GetHealth()<=0){ReviveLog("reject: reviver cannot revive (no pawn, knocked or dead)");return false;}
    // Range: a reviver can reach a knocked player from further than the old 350 limit.
    if(Pawn->GetDistanceTo(Other)>500.f){char L[96];snprintf(L,sizeof(L),"reject: too far (%.0f)",Pawn->GetDistanceTo(Other));ReviveLog(L);return false;}
    auto State=Pawn->PlayerState?Pawn->PlayerState->Cast<AFortPlayerStateAthena>():nullptr;
    auto OtherState=Other->PlayerState?Other->PlayerState->Cast<AFortPlayerStateAthena>():nullptr;
    if(!(State && OtherState && State->TeamIndex>=2 && State->TeamIndex==OtherState->TeamIndex)){char L[96];snprintf(L,sizeof(L),"reject: not teammates (%d vs %d)",State?int(State->TeamIndex):-1,OtherState?int(OtherState->TeamIndex):-1);ReviveLog(L);return false;}
    return true;
}
// Called only after the completed revive RPC, never by proximity or the knock tick.
inline bool CompleteRevive(AFortPlayerPawnAthena* Pawn,AFortPlayerControllerAthena* Instigator) {
    if(!HumanBotRevivePath() || !ValidReviver(Pawn,Instigator))return false;
    auto State=(AFortPlayerStateAthena*)Pawn->PlayerState;
    auto ASC=State->AbilitySystemComponent;
    if(ASC){
        std::vector<UObject*> Instances;
        std::vector<const UClass*> Effects;
        auto SpecType=FindStruct("GameplayAbilitySpec");
        if(SpecType)for(int I=0;I<ASC->ActivatableAbilities.Items.Num();++I){
            auto& Spec=ASC->ActivatableAbilities.Items.Get(I,FGameplayAbilitySpec::Size());
            auto A=Spec.Ability;if(!A)continue;auto N=A->Class->Name.ToString();
            if(N.find("DBNO")==std::string::npos || N.find("Revive")!=std::string::npos || N.find("Resurrect")!=std::string::npos)continue;
            for(auto Field:{"NonReplicatedInstances","ReplicatedInstances"}){auto O=SpecType->GetOffset(Field);if(O==uint32(-1))continue;
                auto& List=GetFromOffset<TArray<UObject*>>(&Spec,O);
                if(List.Num()>=0 && List.Num()<16)for(auto Item:List)if(Item && std::find(Instances.begin(),Instances.end(),Item)==Instances.end())Instances.push_back(Item);
            }
        }
        for(auto Instance:Instances)if(auto End=Instance->GetFunction("K2_EndAbility"))if(End->GetParamsNamed().Size==0)Instance->ProcessEvent(End,nullptr);
        if(ASC->HasActiveGameplayEffects())for(int I=0;I<ASC->ActiveGameplayEffects.GameplayEffects_Internal.Num();++I){
            auto Def=ASC->ActiveGameplayEffects.GameplayEffects_Internal.Get(I,FActiveGameplayEffect::Size()).Spec.Def;
            if(Def && Def->Class && Def->Class->Name.ToString().find("GE_Athena_DBNO")!=std::string::npos)Effects.push_back(Def->Class);
        }
        if(auto Remove=ASC->GetFunction("RemoveActiveGameplayEffectBySourceEffect"))for(auto Type:Effects){
            auto Params=Remove->GetParamsNamed();std::vector<uint8> Data(Params.Size,0);bool HasEffect=false;
            for(auto& P:Params.NameOffsetMap){if(P.Offset+P.ElementSize>Data.size())continue;
                if(P.Name=="GameplayEffect" && P.ElementSize==sizeof(void*)){memcpy(Data.data()+P.Offset,&Type,sizeof(Type));HasEffect=true;}
                if(P.Name=="StacksToRemove" && P.ElementSize==4){int All=-1;memcpy(Data.data()+P.Offset,&All,4);}
            }
            if(HasEffect)ASC->ProcessEvent(Remove,Data.data());
        }
    }
    auto P=Pawn->GetProperty("bIsDBNO");auto O=Pawn->GetOffset("bIsDBNO");if(!P || O==uint32(-1))return false;
    GetFromOffset<uint8>(Pawn,O)&=~P->GetFieldMask();Pawn->SetHealth(30.f);
    if(auto F=Pawn->GetFunction("OnRep_IsDBNO"))if(F->GetParamsNamed().Size==0)Pawn->ProcessEvent(F,nullptr);
    RestoreControl(Pawn);
    // Pre-season to Season 2 bots: the knock left them on the crawl speed and the knocked movement, so a revived bot
    // got up but could not move normally. Back to walking at the normal speed, and send it to players now.
    if(LegacyBotRevive() && State->HasbIsABot() && State->bIsABot)if(auto Move=Pawn->CharacterMovement){
        auto O=Move->GetOffset("MovementMode");
        if(O!=uint32(-1) && GetFromOffset<uint8>(Move,O)!=1)if(auto F=Move->GetFunction("SetMovementMode"))Move->Call<void>(F,uint8(1),uint8(0));
        auto S=Move->GetOffset("MaxWalkSpeed");
        if(S!=uint32(-1) && Move->Class){auto Default=Move->Class->GetDefaultObj();float Normal=Default?GetFromOffset<float>(Default,S):600.f;if(Normal<300.f)Normal=600.f;
            float& Speed=GetFromOffset<float>(Move,S);char L[120];snprintf(L,sizeof(L),"legacy: revived bot walk speed %.0f -> %.0f",double(Speed),double(Speed<300.f?Normal:Speed));ReviveLog(L);if(Speed<300.f)Speed=Normal;}
        for(auto Name:{"bIsDBNO","bIsBeingRevived","bIsInDBNOState"})if(auto P=Pawn->GetProperty(Name)){auto Off=Pawn->GetOffset(Name);if(Off!=uint32(-1))GetFromOffset<uint8>(Pawn,Off)&=~P->GetFieldMask();}
        Pawn->bCanBeDamaged=true;Pawn->ForceNetUpdate();
    }
    if(auto PC=Pawn->Controller?Pawn->Controller->Cast<AFortPlayerControllerAthena>():nullptr)
        if(auto F=PC->GetFunction("ClientOnPawnRevived"))if(F->GetParamsNamed().Size==sizeof(void*))PC->Call<void>(F,Instigator);
    State->ForceNetUpdate();ReviveLog(Pawn->IsDBNO()?"complete: target still knocked after revive":"complete: revived");return !Pawn->IsDBNO();
}

// Legacy clients finish revives through the downed pawn's owner. Bot pawns have
// no owning client, so retain the explicit human start/interrupt RPC on authority.
struct HumanBotRevive { TWeakObjectPtr<AFortPlayerControllerAthena> Medic; TWeakObjectPtr<AFortPlayerPawnAthena> Target; ULONGLONG Started=0; float Duration=10, Health=0; FVector Origin{}; };
inline std::vector<HumanBotRevive> HumanBotRevives;
inline bool BotReviveTarget(AFortPlayerPawnAthena* Pawn) {
    auto State=Pawn && Pawn->PlayerState?Pawn->PlayerState->Cast<AFortPlayerStateAthena>():nullptr;
    return State && State->HasbIsABot() && State->bIsABot;
}
inline bool HumanReviveStillValid(const HumanBotRevive& R) {
    auto PC=R.Medic.Get();auto Target=R.Target.Get();
    if(!ValidReviver(Target,PC))return false;
    auto P=PC->MyFortPawn;auto At=P->K2_GetActorLocation();
    auto X=At.X-R.Origin.X,Y=At.Y-R.Origin.Y,Z=At.Z-R.Origin.Z;
    // Only taking damage or walking away cancels; small position corrections are allowed.
    bool Ok=P->GetHealth()+P->GetShield()>=R.Health-.01f && X*X+Y*Y+Z*Z<=200*200;
    if(!Ok)ReviveLog("cancel: reviver took damage or moved away");
    return Ok;
}
inline void InterruptHumanBotRevive(AFortPlayerControllerAthena* PC,AFortPlayerPawnAthena* Target) {
    for(size_t I=0;I<HumanBotRevives.size();){auto& R=HumanBotRevives[I];
        if(R.Medic.Get()!=PC || (Target && R.Target.Get()!=Target)){++I;continue;}
        // The native completion can send its interrupt before the next server tick.
        if(GetTickCount64()-R.Started>=ULONGLONG(R.Duration*1000) && HumanReviveStillValid(R))CompleteRevive(R.Target.Get(),PC);
        HumanBotRevives.erase(HumanBotRevives.begin()+I);
    }
}
inline void StartHumanBotRevive(AFortPlayerControllerAthena* PC,AFortPlayerPawnAthena* Target) {
    if(!HumanBotRevivePath() || !ValidReviver(Target,PC))return;
    if(!RevivePathSupported() && !BotReviveTarget(Target))return; // legacy seasons: real players use the game's revive
    // Real players are revived the same way: the game never reports a finished revive to
    // this server, so the server times it (the old "the game handles it" path never finished).
    ReviveLog(BotReviveTarget(Target)?"start: human started reviving a bot":"start: human started reviving a real player");
    auto State=PC->MyFortPawn->PlayerState?PC->MyFortPawn->PlayerState->Cast<AFortPlayerStateAthena>():nullptr;
    if(!State || (State->HasbIsABot() && State->bIsABot))return;
    for(auto& R:HumanBotRevives)if(R.Medic.Get()==PC && R.Target.Get()==Target)return;
    InterruptHumanBotRevive(PC,nullptr);
    HumanBotRevive R;R.Medic=PC;R.Target=Target;R.Started=GetTickCount64();R.Health=PC->MyFortPawn->GetHealth()+PC->MyFortPawn->GetShield();R.Origin=PC->MyFortPawn->K2_GetActorLocation();
    if(auto F=Target->GetFunction("GetDefaultReviveFromDBNOTime"))if(F->GetParamsNamed().Size==4){float Duration=Target->Call<float>(F);if(std::isfinite(Duration) && Duration>=1 && Duration<=60)R.Duration=Duration;}
    HumanBotRevives.push_back(R);
}
inline void TickHumanBotRevives() {
    for(size_t I=0;I<HumanBotRevives.size();){auto& R=HumanBotRevives[I];
        if(!HumanBotRevivePath() || !HumanReviveStillValid(R)){HumanBotRevives.erase(HumanBotRevives.begin()+I);continue;}
        if(GetTickCount64()-R.Started<ULONGLONG(R.Duration*1000)){++I;continue;}
        CompleteRevive(R.Target.Get(),R.Medic.Get());HumanBotRevives.erase(HumanBotRevives.begin()+I);
    }
}
}
