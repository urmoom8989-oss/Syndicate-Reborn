#include "pch.h"
#include "../../FortniteGame/Public/SyndicateDBNORecovery.h"
#include "../Public/NetDriver.h"
#include "../../Erbium/Public/Finders.h"
#include "../../FortniteGame/Public/FortGameMode.h"
#include "../../Erbium/Public/GUI.h"
#include "../../Erbium/Public/Configuration.h"
#include "../../FortniteGame/Public/BattleRoyaleGamePhaseLogic.h"
#include "../../Erbium/Private/SpectraBots/SpectraBotsIntegration.h"

void QueueSyndicateQuestProgress(AFortPlayerControllerAthena* PC,const char* Quest,const char* Objective,int Count){if(PC)SpectraBots::QueueProgression((AFortPlayerStateAthena*)PC->PlayerState,"quest",Quest,Objective,Count);}

void RecordSyndicateHumanStat(AFortPlayerControllerAthena* PC,int Index,int Count){SpectraBots::RecordHumanStat(PC,Index,Count);}
// Season challenges: batched human stat events and export log lines (see FortQuestManager.cpp).
void SyndicateChallengeTick(float DeltaSeconds);
void SyndicateChallengeLog(const char* Text){SpectraBots::AppendBotLog(Text);}
void QueueSyndicateStats(AFortPlayerControllerAthena* PC,const std::string& StatsJson){
    auto World=UWorld::GetWorld();auto Net=World?(UNetDriver*)World->NetDriver:nullptr;if(!PC || !Net)return;
    bool Connected=false;for(auto C:Net->ClientConnections)if(C && C->PlayerController==PC){Connected=true;break;}
    if(!Connected || !PC->PlayerState)return;
    SpectraBots::QueueProgression((AFortPlayerStateAthena*)PC->PlayerState,"stats","","",0,("\"stats\":"+StatsJson).c_str());
}
void QueueSyndicateQuestDelta(AFortPlayerControllerAthena* PC,const char* Quest,const char* Objective,int Count){if(PC)SpectraBots::QueueProgression((AFortPlayerStateAthena*)PC->PlayerState,"quest_delta",Quest,Objective,Count);}

bool CanStartSyndicateManagedAircraft(){return SpectraBots::CanStartManagedAircraft();}
void SyndicateBotsBeforeDropZoneExit(){SpectraBots::BeforeDropZoneExit();}
void SyndicateOutfitWatchdog();
void SyndicateLeanTick();
void SyndicateNativeHenchmanTick();
void SyndicateSeasonXClientTick();
bool RemoveSyndicateHumanFromBotMatch(AFortPlayerControllerAthena* PC) { return SpectraBots::RemoveHumanFromBotMatch(PC); }
bool SyndicateHumanTeamWantsFill(int Team) { return SpectraBots::HumanTeamWantsFill(Team); }

bool IsSyndicateFinalElimination(AFortPlayerPawnAthena* Pawn) { return Pawn && SpectraBots::FinalizingHumanPawn==Pawn; }

bool HandleSyndicateBotDeathReport(AFortPlayerControllerAthena* PC, FFortPlayerDeathReport& Report) { return SpectraBots::HandleDeathReport(PC, Report); }

int SyndicateBotShotDeathCause(AFortPlayerControllerAthena* PC)
{
    auto B=SpectraBots::ActiveShotBot;
    if(!B || !B->Pawn || !PC || SpectraBots::ActiveShotVictim!=PC->MyFortPawn)return -1;
    return SpectraBots::DeathCauseValue(SpectraBots::WeaponDeathCause(B->Pawn->CurrentWeapon));
}

// Kill feed line for a real player knocked by another real player (bot knocks publish their own).
void SyndicatePublishHumanKnock(AFortPlayerControllerAthena* PC,FFortPlayerDeathReport& Report,int Cause)
{
    if(!PC)return;
    auto Pawn=PC->MyFortPawn?PC->MyFortPawn:(AFortPlayerPawnAthena*)PC->Pawn;
    auto Killer=(AFortPlayerStateAthena*)Report.KillerPlayerState;
    if(!Killer || Killer==(AFortPlayerStateAthena*)PC->PlayerState)return;
    SpectraBots::PublishKnockFeed((AFortPlayerStateAthena*)PC->PlayerState,Pawn,Killer,(AFortPlayerPawnAthena*)Report.KillerPawn,Report.DamageCauser,Cause);
}

uint32_t NetworkObjectListOffset = 0;
uint32_t ReplicationFrameOffset = 0;
uint32_t ClientWorldPackageNameOffset = 0;
uint32_t DestroyedStartupOrDormantActorsOffset = 0;
uint32_t DestroyedStartupOrDormantActorGUIDsOffset = 0;
uint32_t ClientVisibleLevelNamesOffset = 0;

std::unordered_map<UNetConnection*, TArray<FNetViewer*>> ViewerMap;


const ULevel* GetLevel(const AActor* Actor)
{
	auto Outer = Actor->Outer;

	while (Outer)
	{
		if (Outer->Class == ULevel::StaticClass())
			return (ULevel*)Outer;
		else
			Outer = Outer->Outer;
	}

	return nullptr;
}


// Retry briefly while the native DBNO ability finishes its knock transition.
void RecoverNewSeasonFourKnocks(UNetDriver* Driver) {
    struct KnockRecovery { TWeakObjectPtr<AFortPlayerPawnAthena> Pawn; float Next=0; int Remaining=3; };
    static std::vector<KnockRecovery> Seen;
    if(!SyndicateDBNO::SeasonFour())return;
    Seen.erase(std::remove_if(Seen.begin(),Seen.end(),[](auto& W){auto P=W.Pawn.Get();return !P || P->bActorIsBeingDestroyed || !P->IsDBNO();}),Seen.end());
    for(auto Conn:Driver->ClientConnections){
        auto PC=Conn?Conn->PlayerController:nullptr;auto Pawn=PC?PC->MyFortPawn:nullptr;
        if(!Pawn || Pawn->bActorIsBeingDestroyed || !Pawn->IsDBNO())continue;
        float Now=float(UGameplayStatics::GetTimeSeconds(UWorld::GetWorld()));
        auto Entry=std::find_if(Seen.begin(),Seen.end(),[&](auto& W){return W.Pawn.Get()==Pawn;});
        if(Entry==Seen.end()){SyndicateDBNO::RestoreControl(Pawn);Seen.push_back({Pawn,Now+.5f,3});}
        else if(Entry->Remaining>0 && Now>=Entry->Next){SyndicateDBNO::RestoreControl(Pawn);Entry->Next=Now+.5f;--Entry->Remaining;}
    }
}

// Syndicate fix (glider bug): players were seen standing on the ground with their glider
// still open. Bots open the glider by writing bIsParachuteOpen directly, so nothing
// native closes it again on landing; a late/lost landing update can do the same to
// real players. Every 0.25s, any pawn that has been walking on solid ground for two
// consecutive checks gets its glider/skydive state cleared and replicated.
static bool SyndicateClearPawnBit(UObject* Obj,const char* Name){
    auto Prop=Obj->GetProperty(Name);auto Off=Obj->GetOffset(Name);
    if(!Prop || Off==uint32(-1))return false;
    auto& Bits=GetFromOffset<uint8>(Obj,Off);auto Mask=Prop->GetFieldMask();
    if(!(Bits&Mask))return false;
    Bits&=~Mask;return true;
}
static void SyndicateNotify(AFortPlayerPawnAthena* Pawn,const char* RepName){
    auto F=Pawn->GetFunction(RepName);if(!F)return;
    auto Size=F->GetParamsNamed().Size;
    if(Size==0)Pawn->ProcessEvent(F,nullptr);
    else if(Size==1)Pawn->Call<void>(F,true); // previous value: was open / was skydiving
}
static bool SyndicateOnSolidGround(AFortPlayerPawnAthena* Pawn){
    auto Move=Pawn->CharacterMovement;if(!Move)return false;
    auto Off=Move->GetOffset("MovementMode");if(Off==uint32(-1))return false;
    auto Mode=GetFromOffset<uint8>(Move,Off);
    return Mode==1 || Mode==2; // MOVE_Walking / MOVE_NavWalking
}
static void SyndicateSettleLandedGliders(UNetDriver* Driver,float DeltaSeconds){
    static float Accumulated=0.f;Accumulated+=DeltaSeconds;
    if(Accumulated<0.25f)return;
    Accumulated=0.f;
    static std::unordered_map<AFortPlayerPawnAthena*,int> Grounded;
    std::unordered_map<AFortPlayerPawnAthena*,int> Next;
    auto Check=[&](AFortPlayerPawnAthena* Pawn){
        if(!Pawn || Pawn->bActorIsBeingDestroyed || Next.count(Pawn))return;
        if(!SyndicateOnSolidGround(Pawn))return;
        auto Seen=Grounded.find(Pawn);int Count=(Seen==Grounded.end()?0:Seen->second)+1;
        Next[Pawn]=Count;
        if(Count<2)return;
        bool Changed=false;
        if(SyndicateClearPawnBit(Pawn,"bIsParachuteOpen")){SyndicateNotify(Pawn,"OnRep_IsParachuteOpen");Changed=true;}
        if(SyndicateClearPawnBit(Pawn,"bIsSkydiving")){SyndicateNotify(Pawn,"OnRep_IsSkydiving");Changed=true;}
        if(Changed)Pawn->ForceNetUpdate();
    };
    for(auto Conn:Driver->ClientConnections){
        auto PC=Conn?Conn->PlayerController:nullptr;
        if(PC)Check(PC->MyFortPawn);
    }
    for(auto& B:SpectraBots::Bots)if(!B.Eliminated && B.bLanded)Check((AFortPlayerPawnAthena*)B.Pawn);
    Grounded.swap(Next);
}

void BuildViewerMap(UNetDriver* Driver)
{
	//Log(L"PC");
	for (auto& Conn : Driver->ClientConnections)
	{
		auto Owner = Conn->OwningActor;
		if (Owner)
		{
			auto OutViewTarget = Owner;
			if (auto Controller = Conn->PlayerController)
				if (auto ViewTarget = Controller->GetViewTarget())
						OutViewTarget = ViewTarget;

			Conn->ViewTarget = OutViewTarget;
            // Supply a continuous target rotation for the client's native spectator smoothing.
            // Do not send ClientSetRotation or repeatedly reset its view target.
            // Every season (was Season 4 only): spectating a bot elsewhere left the camera on a stale or raw aim, so it
            // looked straight down and spun sideways. The bot's aim is normalized first: pitch kept within ±85
            // degrees and no roll, because a raw 270-360 pitch or a syndicate aim rolls the spectator camera.
            if(Conn->PlayerController){
                auto PC=Conn->PlayerController;
                auto Target=OutViewTarget?OutViewTarget->Cast<AFortPlayerPawnAthena>():nullptr;
                auto TargetState=Target && Target->PlayerState?Target->PlayerState->Cast<AFortPlayerStateAthena>():nullptr;
                // Real players: overwriting their rotation here fought the native one (camera shake).
                if(Target && !Target->bActorIsBeingDestroyed && Target!=PC->Pawn && Target->Controller && TargetState && TargetState->HasbIsABot() && TargetState->bIsABot){
                    auto F=Target->Controller->GetFunction("GetControlRotation");auto O=PC->GetOffset("TargetViewRotation");
                    if(F && O!=uint32(-1)){
                        auto R=Target->Controller->Call<FRotator>(F);
                        auto Norm=[](float A){A=fmodf(A,360.f);if(A>180.f)A-=360.f;if(A<-180.f)A+=360.f;return A;};
                        R.Pitch=(std::max)(-85.f,(std::min)(85.f,Norm(R.Pitch)));R.Yaw=Norm(R.Yaw);R.Roll=0.f;
                        if(std::isfinite(R.Pitch) && std::isfinite(R.Yaw))GetFromOffset<FRotator>(PC,O)=R;
                    }
                }
            }

			TArray<FNetViewer*> Viewers;
			Viewers.Reserve(1 + Conn->Children.Num());
			Viewers.Add(FNetViewer::Create(Conn));

			for (auto& Child : Conn->Children)
			{
				if (auto Controller = Child->PlayerController)
				{
					Child->ViewTarget = Controller->GetViewTarget();
					Viewers.Add(FNetViewer::Create(Child));
				}
				else
					Child->ViewTarget = nullptr;
			}
			ViewerMap[Conn] = Viewers;
		}
		else
		{
			Conn->ViewTarget = nullptr;
			for (auto& Child : Conn->Children)
				Child->ViewTarget = nullptr;
		}
	}
}

static FNetworkObjectList& GetNetworkObjectList(UNetDriver* Driver)
{
	return *(*(class TSharedPtr<FNetworkObjectList>*)(__int64(Driver) + NetworkObjectListOffset));
}

UNetConnection* IsActorOwnedByAndRelevantToConnection(const AActor* Actor, TArray<FNetViewer*>& ConnViewers, bool& bOutHasNullViewTarget)
{
	auto IsNetRelevantForIdx = FindIsNetRelevantForVft();
	if (IsNetRelevantForIdx == 0)
		return ConnViewers[0]->Connection;

	bool (*&IsRelevancyOwnerFor)(const AActor*, const AActor*, const AActor*, const AActor*) = decltype(IsRelevancyOwnerFor)(Actor->Vft[IsNetRelevantForIdx + 2]);
	AActor* (*&GetNetOwner)(const AActor*) = decltype(GetNetOwner)(Actor->Vft[IsNetRelevantForIdx + (VersionInfo.EngineVersion >= 4.19 ? 6 : 5)]);

	const AActor* ActorOwner = GetNetOwner(Actor);

	bOutHasNullViewTarget = false;

	for (auto& Viewer : ConnViewers)
	{
		auto Conn = Viewer->Connection;

		if (Conn->ViewTarget == nullptr)
		{
			bOutHasNullViewTarget = true;
		}

		if (ActorOwner == Conn->PlayerController ||
			(Conn->PlayerController && ActorOwner == Conn->PlayerController->Pawn) ||
			(Conn->ViewTarget && IsRelevancyOwnerFor(Conn->ViewTarget, Actor, ActorOwner, Conn->OwningActor)))
		{
			return Conn;
		}
	}

	return nullptr;
}

bool IsActorRelevantToConnection(const AActor* Actor, const TArray<FNetViewer*>& ConnectionViewers)
{
    // Legacy private team actors have no spatial root. Route their roster and
    // health data by team membership, never by the spectator's camera location.
    static auto PrivateTeamClass=FindClass("FortTeamPrivateInfo");
    // This used to apply to Season 4 only; on every other season the private team actor
    // was never sent to players, so the squad list (names + health, top left) stayed empty.
    if(PrivateTeamClass && Actor->Class==PrivateTeamClass){
        auto O=Actor->GetOffset("TeamInfo");
        auto Team=O!=uint32(-1)?GetFromOffset<AActor*>(Actor,O):nullptr;
        if(!Team)return false;
        for(auto Viewer:ConnectionViewers){
            auto PC=Viewer && Viewer->Connection?Viewer->Connection->PlayerController:nullptr;
            auto PS=PC?PC->PlayerState:nullptr;if(!PS)continue;
            auto PO=PS->GetOffset("PlayerTeam");
            if(PO!=uint32(-1) && GetFromOffset<AActor*>(PS,PO)==Team)return true;
        }
        return false;
    }
	auto IsNetRelevantForIdx = FindIsNetRelevantForVft();
	if (IsNetRelevantForIdx == 0)
		return true;

	bool (*&IsNetRelevantFor)(const AActor*, const AActor*, const AActor*, const FVector&) = decltype(IsNetRelevantFor)(Actor->Vft[IsNetRelevantForIdx]);

	for (auto& Viewer : ConnectionViewers)
	{
		if (!Viewer)
			continue;

		if (IsNetRelevantFor(Actor, Viewer->InViewer, Viewer->ViewTarget, Viewer->ViewLocation))
		{
			return true;
		}
	}

	return false;
}

bool IsLevelInitializedForActor(const UNetDriver* NetDriver, const AActor* InActor, UNetConnection* InConnection)
{
	static bool (*ClientHasInitializedLevelFor)(const UNetConnection*, const UObject*) = decltype(ClientHasInitializedLevelFor)(FindClientHasInitializedLevelFor());

	const bool bCorrectWorld = NetDriver->WorldPackage != nullptr && 
		(!ClientWorldPackageNameOffset || *(FName*)(__int64(InConnection) + ClientWorldPackageNameOffset) == NetDriver->WorldPackage->Name) && 
		(!ClientHasInitializedLevelFor || ClientHasInitializedLevelFor(InConnection, InActor));
	const bool bIsConnectionPC = (InActor == InConnection->PlayerController);
	return bCorrectWorld || bIsConnectionPC;
}

struct FPrioActor
{
	FNetworkObjectInfo* ActorInfo;
	UActorChannel* Channel;
	float Priority;
	bool bIsRelevant;
	bool bLevelInitializedForActor;

	bool operator<(FPrioActor& _Rhs)
	{
		return Priority < _Rhs.Priority;
	}
};

std::unordered_map<UNetConnection*, UEAllocatedVector<FPrioActor>> PriorityLists;
// The legacy replication loop ignores NetUpdateFrequency. Explicitly budget
// bot movement per connection so it cannot starve initial character state.
std::unordered_map<UNetConnection*, std::unordered_map<AActor*, double>> BotReplicationTimes;

void (*GetActorLocation)(AActor*, FFrame&, FVector*);

// Syndicate perf. Telemetry showed replication taking 20-120 ms a frame (server down to 9 ticks/s in a
// 2-player duo) while the bots took ~4 ms. The loop looked up each actor's channel by scanning the
// connection's whole OpenChannels list, for every actor, every connection, every frame. That grows
// with actors x channels as the match goes on. The channel of each actor is now looked up in a map
// built once per frame, the relevancy test runs once per actor and connection, and world clutter
// (player builds, pickups) that already has a channel is sent at most 10 times a second.
std::unordered_map<UNetConnection*, std::unordered_map<AActor*, UActorChannel*>> ChannelMaps;
static std::unordered_map<AActor*, std::pair<const UClass*, uint8_t>> ActorKinds; // 0 normal, 1 build/pickup
static std::unordered_map<AActor*, double> ClutterNextSend;
static uint8_t SyndicateActorKind(AActor* Actor)
{
	static const UClass* BuildingClass = (const UClass*)FindClass("BuildingActor");
	static const UClass* PickupClass = (const UClass*)FindClass("FortPickup");
	auto Found = ActorKinds.find(Actor);
	if (Found != ActorKinds.end() && Found->second.first == Actor->Class)
		return Found->second.second;
	uint8_t Kind = ((BuildingClass && Actor->IsA((UClass*)BuildingClass)) || (PickupClass && Actor->IsA((UClass*)PickupClass))) ? 1 : 0;
	ActorKinds[Actor] = { Actor->Class, Kind };
	return Kind;
}

void ServerReplicateActors(UNetDriver* Driver, float DeltaSeconds)
{
	if (!ReplicationFrameOffset)
		return;

	(*(int*)(__int64(Driver) + ReplicationFrameOffset))++;

	BuildViewerMap(Driver);
	if (ViewerMap.size() == 0)
		return;

	static FName ActorName = FName(L"Actor");

	auto& NetworkObjectList = GetNetworkObjectList(Driver);
	auto& ActiveNetworkObjects = NetworkObjectList.ActiveNetworkObjects;
	auto IsNetReady = (int32(*)(UNetConnection*, bool))FindIsNetReady();
	static auto CloseActorChannel = (void(*)(UActorChannel*, uint8_t)) FindCloseActorChannel();

	for (auto& ViewerPair : ViewerMap)
	{
		auto& Conn = ViewerPair.first;

		UEAllocatedVector<FPrioActor> List;
		List.reserve(ActiveNetworkObjects.Num());

		PriorityLists[Conn] = List;

		auto& Channels = ChannelMaps[Conn];
		Channels.clear();
		Channels.reserve(Conn->OpenChannels.Num());
		for (auto& Chan : Conn->OpenChannels)
			if (Chan && Chan->Class == UActorChannel::StaticClass() && Chan->Actor)
				Channels.emplace(Chan->Actor, Chan);
	}
	// Forget destroyed actors now and then (the maps are keyed by pointer).
	if (ActorKinds.size() > 200000) ActorKinds.clear();
	if (ClutterNextSend.size() > 200000) ClutterNextSend.clear();

	auto TimeSeconds = UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());

	auto Scale = Driver->NetServerMaxTickRate / FConfiguration::MaxTickRate;
	FFrame FakeStack;
	for (auto& ActorInfo : ActiveNetworkObjects)
	{
		//if (/*!ActorInfo->bPendingNetUpdate && */TimeSeconds <= ActorInfo->NextUpdateTime)
		//	continue;

		auto Actor = ActorInfo->Actor;

		if (!Actor || /*!Actor->bActorInitialized || */Actor->NetDriverName != Driver->NetDriverName)
		{
			continue;
		}

		auto Outer = Actor->Outer;
		if (Actor->bActorIsBeingDestroyed || (TUObjectArray::GetItemByIndex(Actor->Index)->Flags & ((1 << 29) | (1 << 21))) || Actor->RemoteRole == 0 || ((Actor->HasbNetStartup() ? Actor->bNetStartup : false) && Actor->NetDormancy == 4))
		{
			ActorInfo->NextUpdateTime = 43857458734643857485478534.f; // never gonna update lol
			//RemoveNetworkActor(&NetworkObjectList, Actor);
			continue;
		}

		if (!Actor->bReplicates)
			continue;

		const bool bClutter = SyndicateActorKind(Actor) == 1;
		bool bClutterWaiting = false;
		if (bClutter)
		{
			auto Next = ClutterNextSend.find(Actor);
			bClutterWaiting = Next != ClutterNextSend.end() && TimeSeconds < Next->second;
		}

		bool bAnyRelevant = false;
		for (auto& ViewerPair : ViewerMap)
		{
			auto Conn = ViewerPair.first;
			auto& Viewers = ViewerPair.second;
			UActorChannel* Channel = nullptr;
			{
				auto& Channels = ChannelMaps[Conn];
				auto Found = Channels.find(Actor);
				if (Found != Channels.end())
					Channel = Found->second;
			}
			// A build or pickup the player already has is refreshed at most 10 times a second;
			// new ones (no channel yet) still go out at once.
			if (Channel && bClutterWaiting)
				continue;

			// IsNetRelevantFor is a virtual call per viewer; it was made twice for most actors.
			int RelevantCache = -1;
			auto Relevant = [&]() { if (RelevantCache < 0) RelevantCache = IsActorRelevantToConnection(Actor, Viewers) ? 1 : 0; return RelevantCache == 1; };

			bool bLevelInitializedForActor = IsLevelInitializedForActor(Driver, Actor, Conn);
			if (Channel && SpectraBots::BotPawns.count(Actor)
                && !(VersionInfo.FortniteVersion>=4 && VersionInfo.FortniteVersion<5 && Actor==Conn->ViewTarget))
			{
				auto& Times = BotReplicationTimes[Conn];
				auto Previous = Times.find(Actor);
				if (Previous != Times.end() && TimeSeconds - Previous->second < 1.f / 15.f)
					continue;
			}
			if (!Channel && (!bLevelInitializedForActor || !Relevant()))
			{
				continue;
			}

			auto PriorityConn = Conn;
			bool bDoCullCheck = true;

			if (Actor->bAlwaysRelevant || Actor->bTearOff)
				bDoCullCheck = false;

			if (Actor->bOnlyRelevantToOwner)
			{
				bDoCullCheck = false;
				bool bHasNullViewTarget = false;

				if (!(PriorityConn = IsActorOwnedByAndRelevantToConnection(Actor, Viewers, bHasNullViewTarget)))
				{
					if (!bHasNullViewTarget && Channel != NULL && Driver->GetTime() - Channel->GetRelevantTime() >= Driver->RelevantTimeout)
						CloseActorChannel(Channel, 3);
					
					continue;
				}
			}
			else if (VersionInfo.FortniteVersion >= 20) // its broken on legacy builds. idk why
			{
				if (VersionInfo.FortniteVersion == 1.72 || VersionInfo.FortniteVersion == 0.00)
				{
					auto& DormantConnections = *(TSet<TWeakObjectPtr<UNetConnection>>*)(__int64(ActorInfo.Get()) + 0x28);
					
					if (DormantConnections.Contains(Conn))
						continue;
				}
				else if (ActorInfo->DormantConnections.Contains(Conn))
					continue;

				static auto FlushDormancy = FindFlushDormancy();
				if (VersionInfo.FortniteVersion != 1.72 && VersionInfo.FortniteVersion != 0.00 && (VersionInfo.FortniteVersion >= 20 || FlushDormancy))
					if (Actor->GetNetDormancy() > 1 && Channel && !Channel->IsPendingDormancy() && !Channel->IsDormant())
						((int32(*)(UActorChannel*))FindStartBecomingDormant())(Channel);
			}

			bool bIsRelevant = bLevelInitializedForActor && !Actor->bTearOff && (!Channel || Driver->GetTime() - Channel->GetRelevantTime() > 1.0) && Relevant();
			bool bIsRecentlyRelevant = bIsRelevant || (Channel && Driver->GetTime() - Channel->GetRelevantTime() < Driver->RelevantTimeout);

			if (bIsRecentlyRelevant && (!Channel || Channel->Actor))
			{
				bAnyRelevant = true;
				auto Priority = 0.f;

				bool bIsAController = false;
				for (auto& Viewer : Viewers)
				{
					if (Actor == Viewer->InViewer)
						bIsAController = true;
				}
				if (Actor->RootComponent && bIsAController)
				{
					FVector Loc;

					GetActorLocation(Actor, FakeStack, &Loc);

					float SmallestDisSquared = (std::numeric_limits<float>::max)();
					int32 ViewersThatSkipActor = 0;

					for (auto& Viewer : Viewers)
					{
						auto DistanceSquared = (Loc - Viewer->ViewLocation).SizeSquared();
						SmallestDisSquared = (float)min(SmallestDisSquared, DistanceSquared);

						if (bDoCullCheck && DistanceSquared > Actor->NetCullDistanceSquared)
							ViewersThatSkipActor++;
					}

					if (bDoCullCheck && ViewersThatSkipActor == Viewers.Num())
						continue;

					const float MaxDistanceScaling = 60000.f * 60000.f;

					const float DistanceFactor = std::clamp((SmallestDisSquared) / MaxDistanceScaling, 0.f, 1.f);

					Priority += DistanceFactor;
				}

				if (Actor->NetDormancy > 1)
					Priority -= 1.5f;

				if (VersionInfo.FortniteVersion >= 2.0 && VersionInfo.FortniteVersion < 3.0)
				{
					if (Actor->IsA(AFortPlayerStateAthena::StaticClass())) Priority -= 2.f;
					if (!Channel) Priority -= 3.f;
				}

				for (auto& Viewer : Viewers)
					if (Actor == Viewer->ViewTarget || Actor == Viewer->InViewer)
						Priority = -(std::numeric_limits<float>::max)();
				// The player's own held weapon goes out first too: after a weapon swap the client can't fire
				// until the new weapon actor arrives, and with ~100 bots and their builds in the list it
				// could wait behind a saturated connection for several frames (slow pullout).
				for (auto& Viewer : Viewers)
					if (auto PC = Viewer->Connection ? Viewer->Connection->PlayerController : nullptr)
						if (auto Pawn = ((AFortPlayerControllerAthena*)PC)->MyFortPawn; Pawn && (AActor*)Pawn->CurrentWeapon == Actor)
							Priority = -(std::numeric_limits<float>::max)();


				auto& PriorityList = PriorityLists[Conn];
				PriorityList.push_back({ ActorInfo.Get(), Channel, Priority, bIsRelevant, bLevelInitializedForActor });
			}

			if (Channel && !bIsRecentlyRelevant && (Actor->bTearOff || !bLevelInitializedForActor || !(Actor->HasbNetStartup() ? Actor->bNetStartup : false)))
			{
				CloseActorChannel(Channel, Actor->bTearOff ? 4 : 3);
				continue;
			}
		}

		//ActorInfo->NextUpdateTime = TimeSeconds + Scale / Actor->NetUpdateFrequency;

		if (bAnyRelevant)
			((void(*)(AActor*, UNetDriver*)) FindCallPreReplication())(Actor, Driver);
		if (bClutter && bAnyRelevant && !bClutterWaiting)
			ClutterNextSend[Actor] = TimeSeconds + 0.1;
	}


	for (auto& PriorityListPair : PriorityLists)
	{
		auto Conn = PriorityListPair.first;
		auto& Viewers = ViewerMap[Conn];
		auto& PriorityActors = PriorityListPair.second;
		int i = 0;

		if (!Conn->ViewTarget)
			goto _out;

		std::sort(PriorityActors.begin(), PriorityActors.end());

		if (IsNetReady && VersionInfo.FortniteVersion < 22 &&  !IsNetReady(Conn, false))
			goto _out;

		if (DestroyedStartupOrDormantActorGUIDsOffset)
		{
			static auto& DestroyedStartupOrDormantActors = *(TMap<uint32, FActorDestructionInfo*>*)(__int64(Driver) + DestroyedStartupOrDormantActorsOffset);
			static auto& DestroyedStartupOrDormantActors_UE53 = *(TMap<uint64, FActorDestructionInfo*>*)(__int64(Driver) + DestroyedStartupOrDormantActorsOffset);
			auto& DestroyedStartupOrDormantActorGUIDs = *(TSet<uint32>*)(__int64(Conn) + DestroyedStartupOrDormantActorGUIDsOffset);
			auto& DestroyedStartupOrDormantActorGUIDs_UE53 = *(TSet<uint64>*)(__int64(Conn) + DestroyedStartupOrDormantActorGUIDsOffset);
			auto& ClientVisibleLevelNames = *(TSet<int32>*)(__int64(Conn) + ClientVisibleLevelNamesOffset);
			static auto SetChannelActorForDestroy = (void(*)(UActorChannel*, FActorDestructionInfo*)) FindSetChannelActorForDestroy();
			static auto SendDestructionInfo = (void(*)(UNetDriver*, UNetConnection*, FActorDestructionInfo*)) FindSendDestructionInfo();

			if (VersionInfo.EngineVersion >= 5.3)
			{
				for (auto& NetGUID : DestroyedStartupOrDormantActorGUIDs_UE53)
				{
					auto DestructionInfoPtr = DestroyedStartupOrDormantActors_UE53.Search([&](uint64& GUID, FActorDestructionInfo*& InfoUPtr)
						{
							return GUID == NetGUID/* && (InfoUPtr->StreamingLevelName == FName(0) || ClientVisibleLevelNames.Contains(InfoUPtr->StreamingLevelName.ComparisonIndex))*/;
						});

					if (DestructionInfoPtr)
					{
						auto DestructionInfo = *DestructionInfoPtr;

						if (SetChannelActorForDestroy)
						{
							auto Channel = ((UActorChannel * (*)(UNetConnection*, FName*, uint8_t, int))FindCreateChannel())(Conn, &ActorName, 2, -1);

							if (Channel)
								SetChannelActorForDestroy(Channel, DestructionInfo);
						}
						else if (SendDestructionInfo)
							SendDestructionInfo(Driver, Conn, DestructionInfo);
						//printf("Path: %s\n", DestructionInfo->PathName.ToString().c_str());
					}
				}
				DestroyedStartupOrDormantActorGUIDs_UE53.Reset();
			}
			else
			{
				for (auto& NetGUID : DestroyedStartupOrDormantActorGUIDs)
				{
					auto DestructionInfoPtr = DestroyedStartupOrDormantActors.Search([&](uint32& GUID, FActorDestructionInfo*& InfoUPtr)
						{
							return GUID == NetGUID/* && (InfoUPtr->StreamingLevelName == FName(0) || ClientVisibleLevelNames.Contains(InfoUPtr->StreamingLevelName.ComparisonIndex))*/;
						});

					if (DestructionInfoPtr)
					{
						auto DestructionInfo = *DestructionInfoPtr;

						if (SetChannelActorForDestroy)
						{
							auto Channel = ((UActorChannel * (*)(UNetConnection*, FName*, uint8_t, int))FindCreateChannel())(Conn, &ActorName, 2, -1);

							if (Channel)
								SetChannelActorForDestroy(Channel, DestructionInfo);
						}
						else if (SendDestructionInfo)
							SendDestructionInfo(Driver, Conn, DestructionInfo);
						//printf("Path: %s\n", DestructionInfo->PathName.ToString().c_str());
					}
				}
				DestroyedStartupOrDormantActorGUIDs.Reset();
			}
		}


		static auto SendClientAdjustment = FindSendClientAdjustment();
		if (SendClientAdjustment)
			for (auto& Viewer : Viewers)
			{
				if (auto PC=Viewer->Connection->PlayerController) {
                    // A retained eliminated pawn is not the spectator's movement authority.
                    auto Pawn=PC->Pawn;
                    if(SyndicateDBNO::SeasonFour() && (PC->StateName==FName(L"Spectating") || !Pawn || Pawn->bActorIsBeingDestroyed || Pawn->GetHealth()<=0.f))continue;
                    ((void(*)(AFortPlayerControllerAthena*)) SendClientAdjustment)(PC);
                }
			}

		for (auto& PriorityActor : PriorityActors)
		{
			auto ActorInfo = PriorityActor.ActorInfo;

			UActorChannel* Channel = PriorityActor.Channel;

			if (!Channel || Channel->Actor)
			{
				auto Actor = ActorInfo->Actor;

				if (!Channel)
				{
					if (VersionInfo.FortniteVersion >= 20)
						Channel = ((UActorChannel * (*)(UNetConnection*, FName*, uint8_t, int))FindCreateChannel())(Conn, &ActorName, 2, -1);
					else
						Channel = ((UActorChannel*(*)(UNetConnection*, int, bool, int32_t))FindCreateChannel())(Conn, 2, true, -1);

					if (Channel)
						((void(*)(UActorChannel*, AActor*, uint8_t))FindSetChannelActor())(Channel, Actor, 0);
				}

				if (Channel)
				{
					if (PriorityActor.bIsRelevant)
						Channel->GetRelevantTime() = Driver->GetTime() + 0.5 * ((float)rand() / 32767.f);

					if (VersionInfo.FortniteVersion >= 22 || (IsNetReady && IsNetReady(Conn, false))) // actually uchannel::isnetready
					{
						((int32(*)(UActorChannel*))FindReplicateActor())(Channel);
						if (SpectraBots::BotPawns.count(Actor))
							BotReplicationTimes[Conn][Actor] = TimeSeconds;
					}
					else
						Actor->ForceNetUpdate();

					if (IsNetReady && VersionInfo.FortniteVersion < 22 && !IsNetReady(Conn, false))
					{
						break;
					}
				}

				if (Channel && Actor->bTearOff && (!PriorityActor.bLevelInitializedForActor || !(Actor->HasbNetStartup() ? Actor->bNetStartup : false)))
					CloseActorChannel(Channel, 4);
			}
			i++;
		}

	_out:
		PriorityActors.clear();
	}
	PriorityLists.clear();
	ChannelMaps.clear();
	// Bot movement timestamps: drop entries of actors that no longer have a recent send.
	for (auto& Times : BotReplicationTimes)
		if (Times.second.size() > 4096)
			Times.second.clear();

	for (auto& ViewerPair : ViewerMap)
	{
		for (auto& Viewer : ViewerPair.second)
			free(Viewer);

		ViewerPair.second.Free();
	}

	ViewerMap.clear();
}

// Bandwidth per player. Unreal's defaults cap a connection from another PC (Radmin, LAN) at about
// 10 KB/s; with ~100 bots to replicate that connection saturated, updates queued behind each other,
// ping rose above the VPN ping and players rubber-banded. Raise the cap on the driver and on every
// connection (TILTED_NET_RATE bytes/s, default 200000 = 1.6 Mbit/s per player). Checked once a second.
static void SyndicateTuneBandwidth(UNetDriver* Driver)
{
    static int Target = [] { wchar_t B[16]{}; int V = 200000; if (GetEnvironmentVariableW(L"TILTED_NET_RATE", B, 16)) V = _wtoi(B); return V < 10000 ? 10000 : V > 2000000 ? 2000000 : V; }();
    static ULONGLONG Next = 0; auto Now = GetTickCount64(); if (!Driver || Now < Next) return; Next = Now + 1000;
    if (Driver->HasMaxClientRate() && Driver->MaxClientRate < Target) Driver->MaxClientRate = Target;
    if (Driver->HasMaxInternetClientRate() && Driver->MaxInternetClientRate < Target) Driver->MaxInternetClientRate = Target;
    // The game's network manager hands each player TotalNetBandwidth / players, clamped to
    // [MinDynamicBandwidth, MaxDynamicBandwidth] (Unreal defaults: 32000 total, 7000 max), when they join.
    if (auto World = UWorld::GetWorld())
    {
        auto Off = World->GetOffset("NetworkManager");
        auto Manager = Off != uint32(-1) ? GetFromOffset<UObject*>(World, Off) : nullptr;
        if (Manager)
            for (auto [Name, Value] : { std::pair<const char*, int>{"TotalNetBandwidth", Target * 64}, {"MaxDynamicBandwidth", Target}, {"MinDynamicBandwidth", Target} })
            {
                auto P = Manager->GetOffset(Name);
                if (P != uint32(-1) && GetFromOffset<int32>(Manager, P) < Value) GetFromOffset<int32>(Manager, P) = Value;
            }
    }
    static int Raised = 0;
    for (auto Conn : Driver->ClientConnections)
        if (Conn && Conn->HasCurrentNetSpeed() && Conn->CurrentNetSpeed < Target) { Conn->CurrentNetSpeed = Target; if (Raised++ < 20) { char L[128]; snprintf(L, sizeof(L), "network: raised a connection to %d bytes/s\n", Target); SpectraBots::AppendBotLog(L); } }
}
static double SyndicateNowMs(){LARGE_INTEGER C,F;QueryPerformanceCounter(&C);QueryPerformanceFrequency(&F);return 1000.0*double(C.QuadPart)/double(F.QuadPart);}

// Save the World (dev launcher): the lobby beacon's connections have no player controller, so the replication
// above never sends them anything, and the client never sees its lobby state (mission generator, "world record
// loaded") and refuses to join the game. Send each lobby connection its beacon client, the lobby state and the
// lobby player states directly, ten times a second.
UObject* SyndicateStwLobbyStateForRep();
static void SyndicateStwReplicateBeacon(UNetDriver* Driver)
{
	static double Last = 0; auto Now = SyndicateNowMs();
	if (Now - Last < 100.0) return;
	Last = Now;
	auto State = (AActor*)SyndicateStwLobbyStateForRep();
	auto IsNetReady = (int32(*)(UNetConnection*, bool))FindIsNetReady();
	static auto CreateChannel = (UActorChannel*(*)(UNetConnection*, int, bool, int32_t))FindCreateChannel();
	static auto SetChannelActor = (void(*)(UActorChannel*, AActor*, uint8_t))FindSetChannelActor();
	static auto ReplicateActor = (int32(*)(UActorChannel*))FindReplicateActor();
	static auto PreReplication = (void(*)(AActor*, UNetDriver*))FindCallPreReplication();
	static int Logged = 0;
	for (auto Conn : Driver->ClientConnections)
	{
		if (!Conn || !Conn->OwningActor) continue;
		std::vector<AActor*> Actors{ (AActor*)Conn->OwningActor };
		if (State)
		{
			Actors.push_back(State);
			// LobbyBeaconState::Players (+0x3a8): items at +0xb0 (0x18 bytes each, player state at +0x10).
			auto Items = *(uint8_t**)((uint8_t*)State + 0x3a8 + 0xb0); int Count = *(int*)((uint8_t*)State + 0x3a8 + 0xb8);
			for (int i = 0; Items && i < Count && i < 16; i++) if (auto P = *(AActor**)(Items + i * 0x18 + 0x10)) Actors.push_back(P);
		}
		for (auto Actor : Actors)
		{
			if (!Actor || Actor->bActorIsBeingDestroyed) continue;
			UActorChannel* Channel = nullptr;
			for (auto& Chan : Conn->OpenChannels)
				if (Chan && Chan->Class == UActorChannel::StaticClass() && Chan->Actor == Actor) { Channel = Chan; break; }
			if (IsNetReady && !IsNetReady(Conn, false)) break;
			if (!Channel)
			{
				if (auto O = Actor->GetOffset("NetDriverName"); O != uint32(-1)) GetFromOffset<FName>(Actor, O) = Driver->NetDriverName;
				Channel = CreateChannel(Conn, 2, true, -1);
				if (!Channel) continue;
				SetChannelActor(Channel, Actor, 0);
				if (Logged++ < 20) printf("[Syndicate STW] lobby replication: opened a channel for %s\n", Actor->Class ? std::string(Actor->Class->Name.ToSDKString().c_str()).c_str() : "?");
			}
			PreReplication(Actor, Driver);
			ReplicateActor(Channel);
		}
	}
}

void UNetDriver::TickFlush(UNetDriver* Driver, float DeltaSeconds)
{
    // Save the World (dev launcher) zone: none of the Battle Royale logic applies. Skip the island's driver while
    // its world is gone (it is moved to the zone world once the zone game mode runs), otherwise replicate + flush.
    {
        extern bool SyndicateStwZoneTravel;
        void SyndicateStwTravelTick();
        if (UWorld::GetWorld() && Driver == UWorld::GetWorld()->NetDriver) SyndicateStwTravelTick();
        if (SyndicateStwZoneTravel)
        {
            auto World = UWorld::GetWorld();
            if (!World || Driver->World != World)
                return TickFlushOG(Driver, DeltaSeconds);
            if (Driver != World->NetDriver)
            {
                if (Driver->ClientConnections.Num() > 0) SyndicateStwReplicateBeacon(Driver);
                return TickFlushOG(Driver, DeltaSeconds);
            }
            if (Driver->ClientConnections.Num() > 0)
                ServerReplicateActors(Driver, DeltaSeconds);
            return TickFlushOG(Driver, DeltaSeconds);
        }
    }
    if (UWorld::GetWorld() && Driver == UWorld::GetWorld()->NetDriver)
        { auto T0=SyndicateNowMs(); SpectraBots::Tick(DeltaSeconds); SyndicateNativeHenchmanTick(); SyndicateOutfitWatchdog(); SyndicateLeanTick(); SyndicateSeasonXClientTick(); SyndicateChallengeTick(DeltaSeconds); RecoverNewSeasonFourKnocks(Driver); SyndicateSettleLandedGliders(Driver,DeltaSeconds); SyndicateTuneBandwidth(Driver); SpectraBots::PerfBotMs+=SyndicateNowMs()-T0; ++SpectraBots::PerfFrames; SpectraBots::PerfRepPath="custom"; }
	// Launcher-managed Season 2 hosts have no operator window to start the bus.
	static const bool syndicateManaged = GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", nullptr, 0) > 0;
	static const bool syndicateStudio = []{wchar_t V[4]{};return GetEnvironmentVariableW(L"TILTED_STUDIO",V,4)==1 && V[0]==L'1';}();
	static float syndicateWarmup = 0.f;
	static bool syndicateBusStarted = false;
	if (syndicateManaged && VersionInfo.FortniteVersion < 3.0 && !syndicateBusStarted && !syndicateStudio)
	{
		auto World = UWorld::GetWorld();
		auto Mode = World ? (AFortGameMode*)World->AuthorityGameMode : nullptr;
		if (World && Driver == World->NetDriver && Mode && GUI::gsStatus == Joinable)
		{
			bool playerLoaded = false;
			for (auto& Player : Mode->AlivePlayers)
			{
				if (!Player) continue;
				auto Human = (AFortPlayerControllerAthena*)Player;
				// Spectra bots must not arm the bus countdown; wait for a real player.
				if (Human->PlayerState && ((AFortPlayerStateAthena*)Human->PlayerState)->bIsABot) continue;
				if (Human->MyFortPawn) { playerLoaded = true; break; }
			}
			syndicateWarmup = playerLoaded ? syndicateWarmup + DeltaSeconds : 0.f;
			if (syndicateWarmup >= 60.f)
			{
				syndicateBusStarted = true;
				printf("PROJECT_TILTED_AUTO_START_BUS\n");
				UKismetSystemLibrary::ExecuteConsoleCommand(World, FString(L"startaircraft"), nullptr);
			}
		}
	}
	if (VersionInfo.FortniteVersion >= 25.20)
	{
		auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(UWorld::GetWorld());
		GamePhaseLogic->Tick();
	}

	if (Driver->ClientConnections.Num() > 0)
	{ auto R0=SyndicateNowMs(); ServerReplicateActors(Driver, DeltaSeconds); SpectraBots::PerfRepMs+=SyndicateNowMs()-R0; }

    if (GUI::gsStatus == Joinable && VersionInfo.FortniteVersion >= 11.00)
    { 
        auto Time = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
		auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
		static auto bSkipAircraft = GameState->HasCurrentPlaylistInfo() && GameState->CurrentPlaylistInfo.BasePlaylist ? GameState->CurrentPlaylistInfo.BasePlaylist->bSkipAircraft : false;
		auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
		if (!bSkipAircraft && GameState->HasWarmupCountdownEndTime() && GameMode->MatchState == FName(L"InProgress") && GameState->WarmupCountdownEndTime <= Time)
        {
            UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"startaircraft"), nullptr);
        }
	}
	else if (GUI::gsStatus == StartedMatch && (FConfiguration::bAutoRestart || (FConfiguration::WebhookURL && *FConfiguration::WebhookURL) || (VersionInfo.FortniteVersion >= 18 && VersionInfo.FortniteVersion < 25.20)))
	{
		auto WorldNetDriver = UWorld::GetWorld()->NetDriver;
		auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
		if (Driver == WorldNetDriver && Driver->ClientConnections.Num() == 0)
		{
			static bool stopped = false;

			if (!stopped)
			{
				stopped = true;

				if constexpr (FConfiguration::WebhookURL && *FConfiguration::WebhookURL)
				{
					auto curl = curl_easy_init();

					curl_easy_setopt(curl, CURLOPT_URL, FConfiguration::WebhookURL);
					curl_slist* headers = curl_slist_append(NULL, "Content-Type: application/json");
					curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

					char version[6];

					sprintf_s(version, VersionInfo.FortniteVersion >= 5.00 || VersionInfo.FortniteVersion < 1.2 ? "%.2f" : "%.1f", VersionInfo.FortniteVersion);

    				auto Playlist = VersionInfo.FortniteVersion >= 3.5 && GameMode->HasWarmupRequiredPlayerCount() ? (GameMode->GameState->HasCurrentPlaylistInfo() ? GameMode->GameState->CurrentPlaylistInfo.BasePlaylist : GameMode->GameState->CurrentPlaylistData) : nullptr;
					auto payload = UEAllocatedString("{\"embeds\": [{\"title\": \"Match has ended!\", \"fields\": [{\"name\":\"Version\",\"value\":\"") + version + "\"}, {\"name\":\"Playlist\",\"value\":\"" + (Playlist ? Playlist->PlaylistName.ToString() : "Playlist_DefaultSolo") + "\"}], \"color\": " + "\"7237230\", \"footer\": {\"text\":\"Erbium\", \"icon_url\":\"https://cdn.discordapp.com/attachments/1341168629378584698/1436803905119064105/L0WnFa.png.png?ex=6910ef69&is=690f9de9&hm=01a0888b46647959b38ee58df322048ab49e2a5a678e52d4502d9c5e3978d805&\"}, \"timestamp\":\"" + iso8601() + "\"}] }";

					curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());

					curl_easy_perform(curl);

					curl_easy_cleanup(curl);
				}

				if (FConfiguration::bAutoRestart)
					TerminateProcess(GetCurrentProcess(), 0);
			}
		}
		else if (Driver == WorldNetDriver && VersionInfo.FortniteVersion >= 18 && VersionInfo.FortniteVersion < 25.20)
		{
			for (auto& UncastedPlayer : GameMode->AlivePlayers)
			{
				auto Player = (AFortPlayerControllerAthena*)UncastedPlayer;
				if (auto Pawn = Player->MyFortPawn)
				{
					bool bInZone = GameMode->IsInCurrentSafeZone(Player->MyFortPawn->K2_GetActorLocation(), false);

					if (Pawn->bIsInsideSafeZone != bInZone || Pawn->bIsInAnyStorm != !bInZone)
					{
						printf("Pawn %s new storm status: %s\n", Pawn->Name.ToString().c_str(), bInZone ? "true" : "false");
						Pawn->bIsInAnyStorm = !bInZone;
						Pawn->OnRep_IsInAnyStorm();
						Pawn->bIsInsideSafeZone = bInZone;
						Pawn->OnRep_IsInsideSafeZone();
					}
				}
			}
		}
	}

    return TickFlushOG(Driver, DeltaSeconds);
}

uint64_t ServerReplicateActors_;
void UNetDriver::TickFlush__RepGraph(UNetDriver* Driver, float DeltaSeconds)
{
    // Season 4 uses this replication path, rather than the legacy tick.
    if (UWorld::GetWorld() && Driver == UWorld::GetWorld()->NetDriver)
        { auto T0=SyndicateNowMs(); SpectraBots::Tick(DeltaSeconds); SyndicateNativeHenchmanTick(); SyndicateOutfitWatchdog(); SyndicateLeanTick(); SyndicateSeasonXClientTick(); SyndicateChallengeTick(DeltaSeconds); SyndicateSettleLandedGliders(Driver,DeltaSeconds); SyndicateTuneBandwidth(Driver); SpectraBots::PerfBotMs+=SyndicateNowMs()-T0; ++SpectraBots::PerfFrames; SpectraBots::PerfRepPath="repgraph"; }
	if (Driver->ReplicationDriver)
	{
		// this is our main netdriver
		if (Driver->ClientConnections.Num() > 0)
		{ auto R0=SyndicateNowMs(); ((void (*)(UObject*, float)) ServerReplicateActors_)(Driver->ReplicationDriver, DeltaSeconds); SpectraBots::PerfRepMs+=SyndicateNowMs()-R0; }


		if (GUI::gsStatus == Joinable && VersionInfo.FortniteVersion >= 11.00)
		{
			auto Time = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
			auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
			auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
			static auto bSkipAircraft = GameState->HasCurrentPlaylistInfo() && GameState->CurrentPlaylistInfo.BasePlaylist ? GameState->CurrentPlaylistInfo.BasePlaylist->bSkipAircraft : false;
			if (!bSkipAircraft && GameState->HasWarmupCountdownEndTime() && GameMode->MatchState == FName(L"InProgress") && GameState->WarmupCountdownEndTime <= Time)
			{
				UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"startaircraft"), nullptr);
			}
		}
		else if (GUI::gsStatus == StartedMatch && (FConfiguration::bAutoRestart || (FConfiguration::WebhookURL && *FConfiguration::WebhookURL) || VersionInfo.FortniteVersion >= 18))
		{
			auto WorldNetDriver = UWorld::GetWorld()->NetDriver;
			auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
			if (Driver == WorldNetDriver && Driver->ClientConnections.Num() == 0)
			{
				static bool stopped = false;

				if (!stopped)
				{
					stopped = true;

					if constexpr (FConfiguration::WebhookURL && *FConfiguration::WebhookURL)
					{
						auto curl = curl_easy_init();

						curl_easy_setopt(curl, CURLOPT_URL, FConfiguration::WebhookURL);
						curl_slist* headers = curl_slist_append(NULL, "Content-Type: application/json");
						curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

						char version[6];

						sprintf_s(version, VersionInfo.FortniteVersion >= 5.00 || VersionInfo.FortniteVersion < 1.2 ? "%.2f" : "%.1f", VersionInfo.FortniteVersion);

        				auto Playlist = VersionInfo.FortniteVersion >= 3.5 && GameMode->HasWarmupRequiredPlayerCount() ? (GameMode->GameState->HasCurrentPlaylistInfo() ? GameMode->GameState->CurrentPlaylistInfo.BasePlaylist : GameMode->GameState->CurrentPlaylistData) : nullptr;
						auto payload = UEAllocatedString("{\"embeds\": [{\"title\": \"Match has ended!\", \"fields\": [{\"name\":\"Version\",\"value\":\"") + version + "\"}, {\"name\":\"Playlist\",\"value\":\"" + (Playlist ? Playlist->PlaylistName.ToString() : "Playlist_DefaultSolo") + "\"}], \"color\": " + "\"7237230\", \"footer\": {\"text\":\"Erbium\", \"icon_url\":\"https://cdn.discordapp.com/attachments/1341168629378584698/1436803905119064105/L0WnFa.png.png?ex=6910ef69&is=690f9de9&hm=01a0888b46647959b38ee58df322048ab49e2a5a678e52d4502d9c5e3978d805&\"}, \"timestamp\":\"" + iso8601() + "\"}] }";

						curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());

						curl_easy_perform(curl);

						curl_easy_cleanup(curl);
					}

					if (FConfiguration::bAutoRestart)
						TerminateProcess(GetCurrentProcess(), 0);
				}
			}
			else if (Driver == WorldNetDriver && VersionInfo.FortniteVersion >= 18)
			{
				for (auto& UncastedPlayer : GameMode->AlivePlayers)
				{
					auto Player = (AFortPlayerControllerAthena*)UncastedPlayer;
					if (auto Pawn = Player->MyFortPawn)
					{
						bool bInZone = GameMode->IsInCurrentSafeZone(Player->MyFortPawn->K2_GetActorLocation(), false);

						if (Pawn->bIsInsideSafeZone != bInZone || Pawn->bIsInAnyStorm != !bInZone)
						{
							printf("Pawn %s new storm status: %s\n", Pawn->Name.ToString().c_str(), bInZone ? "true" : "false");
							Pawn->bIsInAnyStorm = !bInZone;
							Pawn->OnRep_IsInAnyStorm();
							Pawn->bIsInsideSafeZone = bInZone;
							Pawn->OnRep_IsInsideSafeZone();
						}
					}
				}
			}
		}
	}

	return TickFlushOG(Driver, DeltaSeconds);
}


enum class EReplicationSystemSendPass : unsigned
{
	Invalid,
	PostTickDispatch,
	TickFlush,
};

struct FSendUpdateParams
{
	EReplicationSystemSendPass SendPass = EReplicationSystemSendPass::TickFlush;
	float DeltaSeconds = 0.f;
};

void SendClientMoveAdjustments(UNetDriver* Driver)
{
	static auto SendClientAdjustment = (void(*)(AFortPlayerControllerAthena*)) FindSendClientAdjustment();
	if (SendClientAdjustment)
	{
		for (UNetConnection* Connection : Driver->ClientConnections)
		{
			if (Connection == nullptr || Connection->ViewTarget == nullptr)
				continue;

			if (AFortPlayerControllerAthena* PC = Connection->PlayerController)
				SendClientAdjustment(PC);

			for (UNetConnection* ChildConnection : Connection->Children)
			{
				if (ChildConnection == nullptr)
					continue;

				if (AFortPlayerControllerAthena* PC = ChildConnection->PlayerController)
					SendClientAdjustment(PC);
			}
		}
	}
}

void UNetDriver::TickFlush__Iris(UNetDriver* Driver, float DeltaSeconds)
{
	if (VersionInfo.FortniteVersion >= 25.20)
	{
		auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(UWorld::GetWorld());
		if (GamePhaseLogic)
			GamePhaseLogic->Tick();
	}

	if (Driver->ClientConnections.Num() > 0)
	{
		auto ReplicationSystem = *(UObject**)(__int64(&Driver->ReplicationDriver) + 8);

		if (ReplicationSystem)
		{
			static void(*UpdateIrisReplicationViews)(UNetDriver*) = decltype(UpdateIrisReplicationViews)(FindUpdateIrisReplicationViews());
			static void(*PreSendUpdate)(UObject*, FSendUpdateParams&) = decltype(PreSendUpdate)(FindPreSendUpdate());

			UpdateIrisReplicationViews(Driver);
			SendClientMoveAdjustments(Driver);
			FSendUpdateParams Params;
			Params.DeltaSeconds = DeltaSeconds;
			PreSendUpdate(ReplicationSystem, Params);
		}
	}

	if (GUI::gsStatus == Joinable && VersionInfo.FortniteVersion < 25.20)
	{
		auto Time = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
		auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
		auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
		static auto bSkipAircraft = GameState->HasCurrentPlaylistInfo() && GameState->CurrentPlaylistInfo.BasePlaylist ? GameState->CurrentPlaylistInfo.BasePlaylist->bSkipAircraft : false;
		if (!bSkipAircraft && GameState->HasWarmupCountdownEndTime() && GameMode->MatchState == FName(L"InProgress") && GameState->WarmupCountdownEndTime <= Time)
		{
			UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"startaircraft"), nullptr);
		}
	}
	else if (GUI::gsStatus == StartedMatch && (FConfiguration::bAutoRestart || (FConfiguration::WebhookURL && *FConfiguration::WebhookURL) || VersionInfo.FortniteVersion < 25.20))
	{
		auto WorldNetDriver = UWorld::GetWorld()->NetDriver;
		auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
		if (Driver == WorldNetDriver && Driver->ClientConnections.Num() == 0)
		{
			static bool stopped = false;

			if (!stopped)
			{
				stopped = true;

				if constexpr (FConfiguration::WebhookURL && *FConfiguration::WebhookURL)
				{
					auto curl = curl_easy_init();

					curl_easy_setopt(curl, CURLOPT_URL, FConfiguration::WebhookURL);
					curl_slist* headers = curl_slist_append(NULL, "Content-Type: application/json");
					curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

					char version[6];

					sprintf_s(version, VersionInfo.FortniteVersion >= 5.00 || VersionInfo.FortniteVersion < 1.2 ? "%.2f" : "%.1f", VersionInfo.FortniteVersion);

					
        			auto Playlist = VersionInfo.FortniteVersion >= 3.5 && GameMode->HasWarmupRequiredPlayerCount() ? (GameMode->GameState->HasCurrentPlaylistInfo() ? GameMode->GameState->CurrentPlaylistInfo.BasePlaylist : GameMode->GameState->CurrentPlaylistData) : nullptr;
					auto payload = UEAllocatedString("{\"embeds\": [{\"title\": \"Match has ended!\", \"fields\": [{\"name\":\"Version\",\"value\":\"") + version + "\"}, {\"name\":\"Playlist\",\"value\":\"" + (Playlist ? Playlist->PlaylistName.ToString() : "Playlist_DefaultSolo") + "\"}], \"color\": " + "\"7237230\", \"footer\": {\"text\":\"Erbium\", \"icon_url\":\"https://cdn.discordapp.com/attachments/1341168629378584698/1436803905119064105/L0WnFa.png.png?ex=6910ef69&is=690f9de9&hm=01a0888b46647959b38ee58df322048ab49e2a5a678e52d4502d9c5e3978d805&\"}, \"timestamp\":\"" + iso8601() + "\"}] }";

					curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());

					curl_easy_perform(curl);

					curl_easy_cleanup(curl);
				}

				if (FConfiguration::bAutoRestart)
					TerminateProcess(GetCurrentProcess(), 0);
			}
		}
		else if (Driver == WorldNetDriver && VersionInfo.FortniteVersion < 25.20)
		{
			for (auto& UncastedPlayer : GameMode->AlivePlayers)
			{
				auto Player = (AFortPlayerControllerAthena*)UncastedPlayer;
				if (auto Pawn = Player->MyFortPawn)
				{
					bool bInZone = GameMode->IsInCurrentSafeZone(Player->MyFortPawn->K2_GetActorLocation(), false);

					if (Pawn->bIsInsideSafeZone != bInZone || Pawn->bIsInAnyStorm != !bInZone)
					{
						printf("Pawn %s new storm status: %s\n", Pawn->Name.ToString().c_str(), bInZone ? "true" : "false");
						Pawn->bIsInAnyStorm = !bInZone;
						Pawn->OnRep_IsInAnyStorm();
						Pawn->bIsInsideSafeZone = bInZone;
						Pawn->OnRep_IsInsideSafeZone();
					}
				}
			}
		}
	}

	return TickFlushOG(Driver, DeltaSeconds);
}

void (*SetNetDormancyOG)(AActor* Actor, int NewDormancy);
void SetNetDormancy(AActor* Actor, int NewDormancy)
{
	auto Driver = (UNetDriver*)UWorld::GetWorld()->NetDriver;


	SetNetDormancyOG(Actor, NewDormancy);

	if (Driver)
		if (NewDormancy <= 1)
			for (auto& Conn : Driver->ClientConnections)
				((void(*)(UNetConnection*, AActor*)) FindFlushDormancy())(Conn, Actor);
}

void (*FlushNetDormancyOG)(AActor* Actor);
void FlushNetDormancy(AActor* Actor)
{
	auto Driver = (UNetDriver*)UWorld::GetWorld()->NetDriver;

	FlushNetDormancyOG(Actor);

	if (Driver)
		if (Actor->NetDormancy > 1)
			for (auto& Conn : Driver->ClientConnections)
				((void(*)(UNetConnection*, AActor*)) FindFlushDormancy())(Conn, Actor);
}

void UNetDriver::PostLoadHook()
{
	if (VersionInfo.EngineVersion == 4.16)
	{
		NetworkObjectListOffset = 0x3f8;
		ReplicationFrameOffset = 0x288;
	}
	else if (VersionInfo.EngineVersion == 4.19)
	{
		NetworkObjectListOffset = 0x490;
		ReplicationFrameOffset = 0x2c8;
	}
	else if (VersionInfo.FortniteVersion >= 2.5 && VersionInfo.FortniteVersion <= 3.1)
	{
		NetworkObjectListOffset = VersionInfo.FortniteVersion == 3.1 ? 0x4F8 : 0x4F0;
		ReplicationFrameOffset = 0x328;
	}
	else if (VersionInfo.FortniteVersion <= 3.3)
	{
		NetworkObjectListOffset = VersionInfo.FortniteVersion == 3.3 ? 0x508 : 0x500;
		ReplicationFrameOffset = 0x330;
	}
	else if (VersionInfo.FortniteVersion >= 20.40 && VersionInfo.FortniteVersion < 22)
	{
		NetworkObjectListOffset = 0x6b8;
		ReplicationFrameOffset = 0x3d8;
	}
	else if (std::floor(VersionInfo.FortniteVersion) == 20)
	{
		NetworkObjectListOffset = 0x6c8;
		ReplicationFrameOffset = 0x3e0;
	}
	else if (std::floor(VersionInfo.FortniteVersion) == 22)
	{
		NetworkObjectListOffset = 0x708;
		ReplicationFrameOffset = 0x428;
	}
	else if (VersionInfo.FortniteVersion >= 23 && VersionInfo.FortniteVersion < 24.30)
	{
		ReplicationFrameOffset = VersionInfo.FortniteVersion == 24.20 ? 0x438 : 0x440;
		NetworkObjectListOffset = VersionInfo.FortniteVersion < 24 ? 0x720 : 0x730;
	}
	else if (VersionInfo.FortniteVersion >= 24.30 && VersionInfo.FortniteVersion < 28)
	{
		NetworkObjectListOffset = VersionInfo.FortniteVersion < 25.11 ? 0x738 : 0x750;
		ReplicationFrameOffset = VersionInfo.FortniteVersion < 25.11 ? 0x440 : 0x458;
	}
	else if (VersionInfo.FortniteVersion >= 28)
	{
		NetworkObjectListOffset = 0x760;
		ReplicationFrameOffset = 0x468;
	}

	if (VersionInfo.FortniteVersion <= 1.72 && VersionInfo.FortniteVersion != 1.1 && VersionInfo.FortniteVersion != 1.11)
		ClientWorldPackageNameOffset = 0x336A8;
	else if (VersionInfo.FortniteVersion == 1.8 || VersionInfo.FortniteVersion == 1.81 || VersionInfo.FortniteVersion == 1.82 || VersionInfo.FortniteVersion == 1.9)
		ClientWorldPackageNameOffset = 0x33788;
	else if (VersionInfo.FortniteVersion == 1.10)
		ClientWorldPackageNameOffset = 0x337A8;
	else if (VersionInfo.FortniteVersion == 1.11)
		ClientWorldPackageNameOffset = 0x337B8;
	else if (VersionInfo.FortniteVersion >= 2.2 && VersionInfo.FortniteVersion <= 2.4)
		ClientWorldPackageNameOffset = 0xA17A8;
	else if (VersionInfo.FortniteVersion == 2.42 || VersionInfo.FortniteVersion == 2.5)
		ClientWorldPackageNameOffset = 0x17F8;
	else if (VersionInfo.FortniteVersion == 3.1)
		ClientWorldPackageNameOffset = 0x1818;
	else if (VersionInfo.FortniteVersion == 3.2)
		ClientWorldPackageNameOffset = 0x1820;
	else if (VersionInfo.FortniteVersion == 3.3)
		ClientWorldPackageNameOffset = 0x1828;
	else if (VersionInfo.FortniteVersion < 24 && VersionInfo.FortniteVersion > 23.20)
		ClientWorldPackageNameOffset = 0x17D0;
	else if (VersionInfo.FortniteVersion >= 23 && VersionInfo.FortniteVersion <= 23.20)
		ClientWorldPackageNameOffset = 0x1780;
	else if (std::floor(VersionInfo.FortniteVersion) == 22)
		ClientWorldPackageNameOffset = 0x1730;
	else if (VersionInfo.FortniteVersion >= 28)
		ClientWorldPackageNameOffset = 0x1828;
	else if (VersionInfo.FortniteVersion >= 25.30)
		ClientWorldPackageNameOffset = 0x1820;
	else if (VersionInfo.EngineVersion == 5.2)
		ClientWorldPackageNameOffset = 0x1818;
	else if (VersionInfo.FortniteVersion >= 24)
		ClientWorldPackageNameOffset = 0x1820;
	else if (VersionInfo.FortniteVersion >= 20.20)
		ClientWorldPackageNameOffset = 0x16b8;
	else if (VersionInfo.FortniteVersion >= 20)
		ClientWorldPackageNameOffset = 0x1698;

	if (VersionInfo.FortniteVersion >= 25.10)
	{
		DestroyedStartupOrDormantActorsOffset = VersionInfo.FortniteVersion >= 28 ? 0x328 : 0x318;
		DestroyedStartupOrDormantActorGUIDsOffset = VersionInfo.FortniteVersion >= 28 ? 0x14b8 : (VersionInfo.FortniteVersion < 25.30 ? 0x14a8 : 0x14b0);
		ClientVisibleLevelNamesOffset = DestroyedStartupOrDormantActorGUIDsOffset + (VersionInfo.FortniteVersion < 24 ? 0x190 : 0x1e0);
	}
	else if (VersionInfo.FortniteVersion >= 23)
	{
		DestroyedStartupOrDormantActorsOffset = std::floor(VersionInfo.FortniteVersion) == 24 && VersionInfo.FortniteVersion < 24.30 ? 0x2f8 : 0x300;
		DestroyedStartupOrDormantActorGUIDsOffset = VersionInfo.EngineVersion == 5.2 ? 0x14a8 : 0x14b0;
		ClientVisibleLevelNamesOffset = DestroyedStartupOrDormantActorGUIDsOffset + (VersionInfo.FortniteVersion < 24 ? 0x190 : 0x1e0);
	}
	else if (VersionInfo.FortniteVersion >= 20.40)
	{
		DestroyedStartupOrDormantActorsOffset = 0x2e8;
		DestroyedStartupOrDormantActorGUIDsOffset = VersionInfo.EngineVersion >= 5.1 ? 0x14b0 : 0x1488;
		ClientVisibleLevelNamesOffset = DestroyedStartupOrDormantActorGUIDsOffset + (VersionInfo.EngineVersion >= 5.1 ? 0xf0 : 0xa0);
	}
	else if (VersionInfo.FortniteVersion >= 20)
	{
		DestroyedStartupOrDormantActorsOffset = 0x2f0;
		DestroyedStartupOrDormantActorGUIDsOffset = VersionInfo.FortniteVersion >= 20.20 ? 0x1488 : 0x1468;
		ClientVisibleLevelNamesOffset = DestroyedStartupOrDormantActorGUIDsOffset + 0xa0;
	}

	if (!FindServerReplicateActors())
	{
		if (VersionInfo.EngineVersion >= 5.3 && FConfiguration::bEnableIris)
		{
			FindSendClientAdjustment();
			FindUpdateIrisReplicationViews();
			FindPreSendUpdate();
			Utils::Hook(FindTickFlush(), TickFlush__Iris, TickFlushOG);
			return;
		}
		// cache
		FindCreateChannel();
		FindSetChannelActor();
		FindReplicateActor();
		FindSendClientAdjustment();
		FindIsNetRelevantForVft();
		FindCallPreReplication();
		FindCloseActorChannel();
		FindStartBecomingDormant();
		FindClientHasInitializedLevelFor();
		FindSetChannelActorForDestroy();
		FindSendDestructionInfo();
		FindIsNetReady();

		if (VersionInfo.FortniteVersion < 3.4)
			FindFlushDormancy();
		else
		{
			FindGetNamePool();
		}

		GetActorLocation = (void(*)(AActor*, FFrame&, FVector*))AActor::GetDefaultObj()->GetFunction("K2_GetActorLocation")->GetNativeFunc();

		Utils::Hook(FindTickFlush(), TickFlush, TickFlushOG);
	}
	else
	{
		ServerReplicateActors_ = FindServerReplicateActors();

		Utils::Hook(FindTickFlush(), TickFlush__RepGraph, TickFlushOG);
	}

	if (VersionInfo.FortniteVersion < 3.4 && FindFlushDormancy())
	{
		Utils::Hook(__int64(AActor::GetDefaultObj()->GetFunction("FlushNetDormancy")->GetImpl()), FlushNetDormancy, FlushNetDormancyOG);
		Utils::Hook(__int64(AActor::GetDefaultObj()->GetFunction("SetNetDormancy")->GetImpl()), SetNetDormancy, SetNetDormancyOG);
	}
}
