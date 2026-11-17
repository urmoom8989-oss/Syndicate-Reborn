#pragma once
#include "BotMission.h"
#include "BotDifficulty.h"
#include "CombatSkillPolicy.h"
#include "SyndicateRuntime.h"
#include "FortInventory.h"
#include "PlayerTraps.h"
#include "BotDevicePolicy.h"
#include "PickupFlight.h"
#include "Elimination.h"
#include "BuildingContainer.h"
#include "InteractionLoot.h"
#include "AbilitySystemComponent.h"
#include "BotPhysics.h"
#include "NativeBotAircraft.h"
#include "BotAppearance.h"
#include "BotTactics.h"
#include "BotReload.h"
#include "NativeNavigation.h"
#include "BotSpatial.h"
#include "BotWork.h"
#include "BotLanding.h"
#include "LateGame.h"
#include "BotRoute.h"
#include "BotInteraction.h"
#include "BotBrain.h"
#include "BotPlayerStyle.h"
#include "BotNavigation.h"
#include "UnseenStormRecovery.h"
#include "SDK/SDK/FortniteGame_parameters.hpp"
#include "SDK/SDK/AIModule_parameters.hpp"
namespace PlayerBots
{
enum class EBotState
{
    Warmup,
    PreBus,
    Bus,
    Skydiving,
    Gliding,
    Landed,
    Looting,
    MovingToSafeZone,
    Combat,
    Healing,
    MAX
};
enum class EWarmup
{
    Idle,
    Emote,
    Watch,
    Roam,
    Fight
};
struct PlayerBot;
inline std::vector<PlayerBot *> PlayerBotArray;
inline std::vector<Syndicate::Ref<AFortPickup>> PickupCache;
inline std::vector<Syndicate::Ref<ABuildingContainer>> ContainerCache;
inline std::vector<Syndicate::Ref<AFortPlayerPawnAthena>> PawnCache;
inline std::vector<Syndicate::Ref<ABuildingSMActor>> ResourceCache;
inline float NextResourceScan = 0, NextContainerScan = 0;
inline std::vector<Syndicate::Ref<ABuildingFoundation>> FoundationCache;
inline std::vector<Syndicate::Ref<AFortPoiVolume>> POICache;
inline float NextRegionScan=0.f;
inline float NextScan = 0.f, LastTick = -1.f;
inline UWorld *ActiveWorld = nullptr;
inline UFunction *NativePhaseFunction = nullptr;
inline UFunction *NativePerceptionFunction = nullptr;
inline UFunction *NativeNoiseFunction=nullptr,*NativeNoiseLocationFunction=nullptr,*NativeReportNoiseFunction=nullptr;
struct SoundEvent { Syndicate::Ref<AFortPlayerPawnAthena> Source; FVector Point; float Time,Range; };
inline std::vector<SoundEvent> RecentSounds;
inline unsigned NativeSoundCount=0,NativeHearingCount=0,InvestigationsStarted=0;
struct ShotCue { Syndicate::Ref<AFortPlayerPawnAthena> Source; FVector Start,Direction; float Time; };
inline std::vector<ShotCue> RecentShots;
inline bool PropCrouchArea(const FVector &Here)
{
    for(const auto &R:BotLanding::Regions)
        if((R.Key.contains("Moisty") || R.Key.contains("Paradise")) &&
           std::abs(Here.X-R.Center.X)<=R.Extent.X && std::abs(Here.Y-R.Center.Y)<=R.Extent.Y) return true;
    return false;
}
inline void ObserveSound(AActor *Source,const FVector &Point,float Range)
{
    auto Pawn=Syndicate::As<AFortPlayerPawnAthena>(Source);
    if(!Pawn || Pawn->IsDead() || !Syndicate::Finite(Point) || !std::isfinite(Range) || Range<=0.f) return;
    float N=Syndicate::Now();
    std::erase_if(RecentSounds,[N](const auto &E){return !E.Source.Get() || N-E.Time>2.f;});
    for(const auto &E:RecentSounds) if(E.Source.Get()==Pawn && N-E.Time<.2f) return;
    if(RecentSounds.size()>=64) RecentSounds.erase(RecentSounds.begin());
    RecentSounds.push_back({{Pawn},Point,N,std::min(Range,3500.f)}); ++NativeSoundCount;
}
struct RecentBuild { Syndicate::Ref<ABuildingSMActor> Actor; float Time; };
inline std::vector<RecentBuild> RecentBuilds;
inline void ObserveBuild(ABuildingSMActor *Building)
{
    if(!Building || !Building->bPlayerPlaced || Building->bDestroyed) return;
    float N=Syndicate::Now();
    std::erase_if(RecentBuilds,[N](const auto &B){return !B.Actor.Get() || N-B.Time>10.f;});
    for(auto &B:RecentBuilds) if(B.Actor.Get()==Building) return;
    if(RecentBuilds.size()>=64) RecentBuilds.erase(RecentBuilds.begin());
    RecentBuilds.push_back({Syndicate::Ref<ABuildingSMActor>(Building),N});
}
inline float NextReport = 0.f;
struct Reservation
{
    Syndicate::Ref<AActor> Actor;
    PlayerBot *Owner;
    float Until;
};
inline std::vector<Reservation> Reservations;
inline bool Reserved(AActor *A, PlayerBot *B)
{
    for (auto &R : Reservations)
        if (R.Actor.Get() == A && R.Owner != B && R.Until > Syndicate::Now())
            return true;
    return false;
}
inline void Release(PlayerBot *B)
{
    std::erase_if(Reservations, [B](const Reservation &R) { return R.Owner == B; });
}
inline void ScanWorld()
{
    float N = Syndicate::Now();
    if (N < NextScan)
        return;
    BotWork::Scope Measure(BotWork::Scan);
    NextScan = N + 1.f;
    PickupCache = Syndicate::ActorsOf<AFortPickup>();
    PawnCache = Syndicate::ActorsOf<AFortPlayerPawnAthena>();
    BotSpatial::Pickups.Build(PickupCache);
    BotSpatial::Pawns.Build(PawnCache);
    if (FoundationCache.empty())
        FoundationCache = Syndicate::ActorsOf<ABuildingFoundation>();
    if (POICache.empty())
        POICache = Syndicate::ActorsOf<AFortPoiVolume>();
    if (N>=NextRegionScan)
    {
        NextRegionScan=N+8.f;
        for (auto &Ref : FoundationCache) BotLanding::RegisterFoundation(Ref.Get());
    }
    auto World = UWorld::GetWorld();
    auto State = World ? Syndicate::As<AFortGameStateAthena>(World->GameState) : nullptr;
    if (State && N >= NextContainerScan)
    {
        NextContainerScan = N + 8.f;
        ContainerCache = Syndicate::ActorsOf<ABuildingContainer>();
        BotSpatial::Containers.Build(ContainerCache);
        BotSpatial::Supplies.Build(Syndicate::ActorsOf<AFortAthenaSupplyDrop>());
    }
    if (State && State->GamePhase > EAthenaGamePhase::Warmup && N >= NextResourceScan)
    {
        NextResourceScan = N + 8.f;
        ResourceCache = Syndicate::ActorsOf<ABuildingSMActor>();
        std::erase_if(ResourceCache,
                      [](auto &R)
                      {
                          auto B = R.Get();
                          return !B || B->bPlayerPlaced || B->bDestroyed || !B->bAllowResourceDrop ||
                                 B->IsA(ABuildingContainer::StaticClass());
                      });
        BotSpatial::Resources.Build(ResourceCache);
    }
    std::erase_if(Reservations, [N](const Reservation &R) { return !R.Actor.Get() || R.Until < N; });
}
inline int Role(UFortItemDefinition *D)
{
    if (!D)
        return -1;
    auto N = D->GetName();
    std::transform(N.begin(), N.end(), N.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
    if(N.contains("badgergrape") || N.contains("badger_grape") || N.contains("grappl") || N.contains("propomatic") || N.contains("prop_o_matic")) return 3;
    // Consumables can use a ranged item definition. Class inheritance alone is not a gun test.
    if (N.contains("shield") || N.contains("slurp") || N.contains("chug"))
        return 4;
    if (N.contains("medkit") || N.contains("bandage") || (N.contains("grenade") && !N.contains("launcher")) ||
        N.contains("smoke"))
        return 3;
    if (D->IsA(UFortWeaponRangedItemDefinition::StaticClass()))
    {
        if (N.contains("shotgun"))
            return 0;
        if (N.contains("sniper") || N.contains("bow"))
            return 2;
        return 1;
    }
    return 3;
}
inline bool IsGun(UFortItemDefinition *D)
{
    int R = Role(D);
    auto Ranged = Cast<UFortWeaponRangedItemDefinition>(D);
    // Throwables and utility items also inherit the ranged definition. Real
    // guns consume native ammo items; utilities often consume themselves.
    if (!Ranged || R < 0 || R > 2 || D->IsStackable()) return false;
    // Use the native soft reference when already resolved. Scanning every
    // pickup for every bot must not invoke an ammo blueprint each time.
    auto Ammo = Ranged->AmmoData.Get();
    if (!Ammo) Ammo = Ranged->GetAmmoWorldItemDefinition_BP();
    return Cast<UFortAmmoItemDefinition>(Ammo) != nullptr;
}

inline int LoadoutKind(UFortItemDefinition *D)
{
    int R=Role(D);
    if(IsGun(D))
    {
        if(R==0) return 0;
        if(R==2) return 2;
        auto N=D->GetName();
        std::transform(N.begin(),N.end(),N.begin(),[](unsigned char C){return static_cast<char>(std::tolower(C));});
        if(N.contains("smg") || N.contains("submachine") || N.contains("pistol_auto") || N.contains("pdw")) return 4;
        if(N.contains("pistol")) return 3;
        return 1;
    }
    return R==3 ? 5 : (R==4 ? 6 : -1);
}
inline bool UsableHealthHeal(UFortItemDefinition *D,float Health)
{
    if(!D || Health>=100.f) return false;
    auto N=D->GetName();
    std::transform(N.begin(),N.end(),N.begin(),[](unsigned char C){return static_cast<char>(std::tolower(C));});
    return (Health<75.f && N.contains("bandage")) || N.contains("medkit") || N.contains("slurp") || N.contains("chug");
}

inline bool EligiblePickup(UFortItemDefinition *Definition, bool NeedGun, bool Warmup)
{
    if (!Definition || (NeedGun && !IsGun(Definition)))
        return false;
    return !Warmup || IsGun(Definition) || Definition->IsA(UFortAmmoItemDefinition::StaticClass());
}
inline bool EligibleContainer(const std::string &Group, bool NeedGun)
{
    return Group.contains("Treasure") || (!NeedGun && Group.contains("Ammo"));
}

struct PlayerBot
{
    AFortPlayerPawnAthena *Pawn = nullptr;
    AFortAthenaAIBotController *PC = nullptr;
    AFortPlayerStateAthena *PlayerState = nullptr;
    Syndicate::Ref<AFortPlayerPawnAthena> PawnRef;
    Syndicate::Ref<AFortAthenaAIBotController> ControllerRef;
    Syndicate::Ref<AFortPlayerStateAthena> StateRef;
    EBotState BotState = EBotState::Warmup;
    EWarmup WarmupChoice = EWarmup::Idle;
    bool bHasJumpedFromBus = false, bPauseTicking = false, bPotentiallyUnderAttack = false, bNativeAircraft = false,
         bNativeWarning = false;
    bool Firing = false, HealingStarted = false, SprintHeld = false;
    float CombatShootUntil=0.f,CombatBaseZ=0.f,LandedAt=0.f;
    CombatSkill::Materials MaterialIncome;
    CombatSkill::AmmoIncome AmmoIncome;
    CombatSkill::TravelHops TravelHop;
    CombatSkill::SingleShot SingleShot;Syndicate::Ref<AFortWeapon> SingleShotWeapon;
    std::string DeviceResult;
    BotDevices::Kind DeviceKind=BotDevices::Kind::None;
    int DeviceStage=0,DeviceRamps=0;float DeviceUntil=0.f,NextDevice=0.f,DeviceAirUntil=0.f,NextDeviceTrigger=0.f;bool DeviceLaunched=false;
    FVector DeviceCenter{},DeviceHalf{},DeviceGoal{};float DeviceStory=0.f;
    Syndicate::Ref<UFortTrapItemDefinition> DeviceDefinition;
    Syndicate::Ref<ABuildingSMActor> DeviceSupport;
    Syndicate::Ref<ABuildingTrap> ActiveDevice;
    Syndicate::Ref<AFortPlayerPawnAthena> DeviceVictim;
    unsigned int DevicesPlaced=0,DeviceFlights=0;
    bool CombatActorFocus=false;float NextCombatHop=0.f;unsigned int RoamTrips=0;
    CombatSkill::HeightBudget CombatHeightBudget;
    CombatSkill::Prediction CombatPrediction;
    CombatSkill::Celebration Celebration;
    Syndicate::Ref<AFortPlayerPawnAthena> PressureTarget;
    float LastPressureHit=-100.f,PressureUntil=0.f;
    int PressureHits=0;
    bool PreferSMG=Syndicate::Pick(100)<70;
    float NextTour=0.f,TourUntil=0.f,NextSupplyScan=0.f;
    FVector TourGoal{};
    std::vector<std::pair<FVector,float>> SearchedAreas;

    uint64_t tick_counter = 0;
    float NextDropPlanning=0.f;
    FVector TargetDropZone{}, MoveGoal{}, LastSeen{}, AimError{}, LastProgressLocation{}, BoxCenter{}, BoxHalf{};
    float BoxHeight = 0;
    FRotator Aim{};
    bool AimReady = false, HasGoal = false, BoxActive = false;
    uint64_t AimTick = 0;
    bool LocalSteering = false, GroundGoal = false;
    bool AircraftBoarded = false, NativeExitAccepted = false;
    bool DropRouteReady = false;
    FVector DropRouteStart{}, DropRouteEnd{};
    bool WillThankDriver = false;
    float ThankDriverAt = 0.f;
    BotTactics::Interest Investigation;
    float NextInvestigation=0.f,NextAwarenessScan=0.f;
    float NextCueCheck=0.f,LastShotCueAt=-100.f;
    Syndicate::Ref<ABuildingSMActor> HeldCover;
    FVector HeldCoverLocation{}; float HeldCoverYaw=0.f,HeldCoverHealth=0.f,HoldCoverUntil=0.f,CoverHitAt=-100.f;
    float PursuitUntil=0.f;
    FVector SpoilsCenter{}; float SpoilsUntil=0.f,NextSpoilsSearch=0.f;
    float JumpAt = 0.f, NextLootSearch = 0.f, NextNearbyPickup = 0.f, NextHealSearch = 0.f, JumpReleaseAt = 0.f;
    float LootProgressAt = 0.f, LootBestDistance = 0.f, LootBlockerHealth = 0.f;
    int LootMaterials = 0;
    Syndicate::Ref<ABuildingSMActor> LootProgressBlocker;
    Syndicate::Ref<UAthenaGliderItemDefinition> FlightGlider;
    Syndicate::Ref<UAthenaCharacterItemDefinition> FlightCharacter;
    Syndicate::Ref<UAthenaSkyDiveContrailItemDefinition> FlightContrail;
    BotAppearance::Snapshot Appearance;
    FVector SteeringDirection{}, BlockedGoal{};
    float BestGoalDistance = 0.f;
    FVector MotionLocation{};
    float MotionSampleAt = 0.f, LastMotionAt = 0.f, RouteStartedAt = 0.f, SteerEntryDistance = 0.f;
    int RouteRecoveries = 0, MotionReversals = 0;
    FVector MotionDirection{};
    BotRoute::Memory RouteMemory;
    float SteeringSide = 0.f, SteerCommitUntil = 0.f, BlockedUntil = 0.f, MoveRadius = 160.f;
    float NextGliderRequest = 0.f;
    float NextSteer = 0.f, NextPhysicsCheck = 0.f, NextAircraftAttempt = 0.f, NextDropKeys = 0.f;
    EAthenaGamePhase ObservedPhase = EAthenaGamePhase::None;
    Syndicate::Ref<AFortPlayerPawnAthena> Enemy, Threat;
    float ThreatUntil=0.f, NextPursuitSearch=0.f;
    bool PursuitSearching=false;
    FVector PursuitGoal{};
    Syndicate::Ref<AActor> LootTarget, IgnoredLoot;
    Syndicate::Ref<ABuildingSMActor> HarvestTarget;
    Syndicate::Ref<AFortPoiVolume> DropVolume;
    float HarvestDeadline = 0, NextHarvest = 0;
    bool GatheringBurst=false; int GatherGoal=60; float GatherUntil=0.f, NextGather=0.f;
    std::string DropRegionKey;
    float Delta = 1.f / 30.f, NextSense = 0.f, NextPath = 0.f, NextStrafe = 0.f, LastSeenTime = -100.f, ReactionReady = 0.f,
          NextAimError = 0.f;
    float NextWarmup = 0.f, NextEmote = 0.f, NextJump = 0.f, NextBuild = 0.f, NextReload = 0.f, HealDeadline = 0.f,
          HealHealth = 0.f, HealShield = 0.f;
    float BurstEnd = 0.f, NextBurst = 0.f, NextSprintAttempt = 0.f, LastProgress = 0.f, LootSince = 0.f,
          IgnoreUntil = 0.f;
    int BoxPiece = 0, BoxAttempts = 0;
    FGuid HealGuid{};
    int HealCount=0;
    float NextHealUse=0.f, SuppressHealUntil=0.f, BoxDamageAt=-100.f;
    float NextDoor=0.f, NextEdit=0.f;
    BotInteraction::EditSession WallEdit;
    Syndicate::Ref<ABuildingSMActor> WindowPending,WindowCover;
    Syndicate::Ref<AFortWeapon> WindowWeapon;
    int WindowAmmo = -1;
    bool WindowFired = false;
    bool CoveredRampRush = false;
    float NextRearRetake = 0.f;
    Syndicate::Ref<ABuildingSMActor> EnemyCover;
    float EnemyCoverHealth=0.f,EnemyCoverProgressAt=0.f,EnemyCoverRetryAt=0.f;
    float WindowCloseAt=0.f,NextWindow=0.f,NextWindowResetRetry=0.f;
    std::vector<Syndicate::Ref<ABuildingSMActor>> OwnedBuilds;
    Syndicate::Ref<ABuildingSMActor> BoxWalls[6];
    float BoxWallHealth[6]{};
    bool LeavingBox=false;
    float BoxExitUntil=0.f, BoxExitProgressAt=0.f, BoxExitBest=0.f, NextEngageCover=0.f;
    FVector BoxExitGoal{}, AscentToward{}, AscentAnchor{};
    float AscentUntil=0.f, RampProgressAt=0.f, RampBestDistance=0.f, RotationBestHeight=0.f;
    int AscentSteps=0, HighGroundSteps=0;
    float HighGroundUntil=0.f, BuildFightHeight=0.f, RampExitFeet=0.f;
    float RouteFightUntil=0.f, NextRouteFight=0.f;
    bool RampSteering=false, RouteFightPermitHeight=false;
    int HighGroundStage=0;
    FVector CornerCellCenter{},CornerCellHalf{};float CornerCellHeight=0.f;bool CornerCellReady=false;
    bool HeightResumeCorner=false;float HeightPeekUntil=0.f,HeightPeekDeadline=0.f;
    std::vector<Syndicate::Ref<AFortPickup>> ChestPickups;
    float ChestPickupUntil=0.f;
    unsigned int ChestOutputClaims=0;
    float FightStageDistance=0.f,FightBlockedUntil=0.f,FightCoverHandledAt=-100.f;
    float FightEntryGroundZ=0.f;bool FightEntrySupported=false;
    BotMission::TerrainMission TerrainMission;FVector TerrainMissionGoal{};
    float GroundRescueUntil=0.f,GroundRescueAt=0.f;FVector GroundRescueGoal{};
    FVector FightBlockedCell{};
    unsigned int FightRetakes=0,FightPlanRejects=0;
    int FightFailureReason=0;
    FVector CombatPeekGoal{};float CombatPeekUntil=0.f,NextCombatPeek=0.f;
    BotPlayerStyle::CombatPlan PlayerCombat;
    BotPlayerStyle::Interaction LootInteraction;
    Syndicate::Ref<AFortPickup> PendingPickup;
    float PendingPickupAt=0.f;
    FVector LandingLootAnchor{};
    float LandingLootUntil=0.f;
    unsigned int LootTransfers=0,CombatPursuits=0,CombatPeeks=0;
    BotBrain::State Brain;
    BotNavigation::Progress Navigation;
    BotBrain::BuildSequence FightBuild;
    BotTactics::RampPlan FightPlan;
    Syndicate::Ref<ABuildingSMActor> FightFloor;
    FVector FightCell{},FightHalf{},FightDirection{},GroundRecoveryGoal{},GroundRotationGoal{};
    bool GroundRecoverySurface=false;
    float FightStory=384.f,FightHeight=0.f,GroundRecoveryAt=0.f,GroundRecoveryUntil=0.f;
    float GroundCombatMoveAt=0.f,GroundRotationUntil=0.f;
    unsigned int GroundNativeSerial=0;
    Syndicate::Ref<AFortWeapon> GroundObservedWeapon;
    BotTactics::ShotEvidence GroundShots;
    bool CornerBridgeReady=true;
    FVector HeightExitGoal{};float HeightExitUntil=0.f,NextHeightExit=0.f;
    unsigned int HeightBuildRejects=0,HeightClimbAborts=0,LootRouteFailures=0;
    FVector LootApproachGoal{};float LootApproachUntil=0.f,NextLootApproach=0.f;
    float EvadeUntil=0.f; FVector EvadeGoal{};
    Syndicate::Ref<ABuildingWall> DoorPass; FVector DoorCenter{},DoorAcross{}; float DoorPassUntil=0.f;
    FVector HighGroundToward{};
    BotTactics::CrouchRhythm Posture;
    bool CrouchRequested=false;
    float BuildLevelHintUntil=0.f, BuildLevelHintZ=0.f, RampUpperZ=0.f;
    FVector BuildLevelHintPoint{},RampUpperCell{};
    float NextTerrainDetour=0.f,TerrainDetourUntil=0.f;
    FVector TerrainDetourGoal{};
    Syndicate::Ref<AFortPlayerPawnAthena> HeightContestTarget;
    float ObservedContestHeight=0.f;
    float NextWeakSpotProbe=0.f;
    FVector CachedWeakSpot{};
    Syndicate::Ref<ABuildingSMActor> WeakSpotParent;
    int AscentLateralSteps=0;
    unsigned int WeakSpotAims=0, MountainSideSteps=0, StairCrests=0;
    bool StormDeathLogged=false;
    UnseenStormPolicy::State StormRecovery;
    float LastDamageTime = -100.f, LastHealth = 100.f, LastShield = 0.f;
    float BuildingUntil = 0.f;
    bool BuildingVisual = false;
    Syndicate::Ref<UFortBuildingItemDefinition> QueuedBuild;
    FVector QueuedBuildLocation{}, QueuedBuildOrigin{}, QueuedBuildFocus{};
    float QueuedBuildYaw = 0.f, BuildPlaceAt = 0.f;
    bool CommittingBuild = false, QueuedBox = false;
    float CombatSide = 1.f, NextCombatSide = 0.f, NextExplore = 0.f;
    float NextRamp = 0.f, RampDeadline = 0.f, BreachDeadline = 0.f, BreachHealth = 0.f;
    bool RampActive = false, RampEntering = false, RampFloorSampled=false;
    FVector RampGoal{}, RampEntry{}, BreachPoint{}, RotationGoal{};
    float RotationProgressAt = 0.f, RotationBestDistance = 0.f;
    bool RotationActive=false;
    float RotationBlockerHealth = 0.f, RotationDetourUntil = 0.f;
    float RotationSide = 1.f;
    Syndicate::Ref<ABuildingSMActor> RotationBlocker;
    Syndicate::Ref<ABuildingSMActor> BreachTarget, RampActor, LastBuilt;
    Syndicate::Ref<ABuildingSMActor> IgnoredBreach;
    float IgnoreBreachUntil = 0.f;
    Syndicate::Ref<ABuildingSMActor> IgnoredHarvest;
    float IgnoreHarvestUntil = 0.f;
    Syndicate::Ref<AFortWeapon> AimedWeapon;
    PlayerBot(AFortPlayerPawnAthena *P, AFortAthenaAIBotController *C, AFortPlayerStateAthena *S)
        : Pawn(P), PC(C), PlayerState(S), PawnRef(P), ControllerRef(C), StateRef(S)
    {
        BotDifficulty::Assign(C);
        WarmupChoice = static_cast<EWarmup>(PlayerBotArray.size() % 5);
        Posture.Tempo=Syndicate::Between(.26f,.42f);
        NextWarmup = Syndicate::Now() + Syndicate::Between(12, 25);
        NextEmote = Syndicate::Now() + Syndicate::Between(1, 4);
        NextLootSearch = Syndicate::Now() + Syndicate::Between(0.f, 0.75f);
        NextSteer = Syndicate::Now() + Syndicate::Between(0.f, 0.2f);
        FlightGlider.Set(P->CosmeticLoadout.Glider);
        FlightCharacter.Set(P->CosmeticLoadout.Character);
        FlightContrail.Set(P->CosmeticLoadout.SkyDiveContrail);
        Appearance.Capture(S);
        LastProgressLocation = P->K2_GetActorLocation();
        LastProgress = Syndicate::Now();
        LastHealth = P->GetHealth(); LastShield = P->GetShield();
        CombatSide = PlayerBotArray.size() % 2 ? -1.f : 1.f;
        C->bSetControlRotationFromPawnOrientation = false;
        C->bAllowStrafe = true;
        C->SetMoveBlockDetection(true);
        P->bUseControllerRotationPitch = false;
        P->bUseControllerRotationRoll = false;
        P->bUseControllerRotationYaw = false;
        if (P->CharacterMovement)
        {
            P->CharacterMovement->bOrientRotationToMovement = true;
            P->CharacterMovement->bUseControllerDesiredRotation = false;
        }
        P->SetMovementSpeedMultiplier(1.f);
        StopNative();
        if(auto Perception=C->PerceptionComponent)
        {
            bool HasHearing=false;
            for(auto Config:Perception->SensesConfig)
                if(Syndicate::As<UAISenseConfig_Hearing>(Config)) { HasHearing=true; break; }
            if(HasHearing)
            {
                Perception->SetSenseEnabled(UAISense_Hearing::StaticClass(),true);
                Perception->RequestStimuliListenerUpdate();
            }
        }
        PlayerBotArray.push_back(this);
    }
    bool ControllerAlive()
    {
        return ControllerRef.Get() && !PC->IsActorBeingDestroyed();
    }
    bool RefreshPawn()
    {
        if (!ControllerAlive())
            return false;
        auto State = Syndicate::As<AFortPlayerStateAthena>(PC->PlayerState);
        if (!State || !Syndicate::Ref<AFortPlayerStateAthena>(State).Get())
            return false;
        if (StateRef.Get() != State)
        {
            PlayerState = State;
            StateRef.Set(State);
        }
        auto Current = Syndicate::As<AFortPlayerPawnAthena>(PC->Pawn);
        if (!Current || !Syndicate::Ref<AFortPlayerPawnAthena>(Current).Get() || Current->IsActorBeingDestroyed())
            return false; // Keep the controller wrapper while native aircraft code has no possessed pawn.
        if (PawnRef.Get() != Current)
        {
            Pawn = Current;
            PawnRef.Set(Current);
            PC->PlayerBotPawn = Current;
            Firing = SprintHeld = HealingStarted = HasGoal = LocalSteering = RampSteering = false;
            RouteMemory.Reset();
            AimReady = false;
            Brain={};PlayerCombat.Reset();FightBuild.Clear();LootInteraction.Clear();PendingPickup.Clear();GroundRescueUntil=GroundRescueAt=0.f;
            GroundRecoveryUntil=0.f;GroundRecoverySurface=false;LandingLootUntil=0.f;CombatPeekUntil=NextCombatPeek=0.f;
            AimedWeapon.Clear();
            Enemy.Clear();
            Release(this);
            LootTarget.Clear();
            Current->CosmeticLoadout.Glider = FlightGlider.Get();
            Current->CosmeticLoadout.Character = FlightCharacter.Get();
            Current->CosmeticLoadout.SkyDiveContrail = FlightContrail.Get();
            Current->OnRep_CosmeticLoadout();
            Appearance.Restore(PlayerState, Current);
            if (Current->bIsSkydiving || Current->bIsParachuteOpen)
                BotPhysics::EnablePawn(Current);
            else
                BotPhysics::EnableGroundPawn(Current);
            EquipPickaxe();
            Log("Native bot pawn possession changed; AI state rebound to the new pawn.");
        }
        return true;
    }
    bool Valid()
    {
        return RefreshPawn() && StateRef.Get();
    }
    void PreparePhase(EAthenaGamePhase Phase)
    {
        if (ObservedPhase == Phase || !ControllerAlive())
            return;
        ObservedPhase = Phase;
        QueuedBuild.Clear(); BuildingVisual = false; BuildingUntil = 0.f;
        bool HasPawn = RefreshPawn();
        PC->StopMovement();
        HasGoal = LocalSteering = RampSteering = false;
        Release(this);
        LootTarget.Clear();
        if (HasPawn)
        {
            CancelEmote();
            SetFire(false);
            SetADS(false);
            Sprint(false);
            Pawn->StopJumping();
        }
        if (Phase == EAthenaGamePhase::Aircraft)
        {
            BotState = EBotState::Bus;
            ChooseDrop();
            if (!PC->Blackboard && PC->BehaviorTree && PC->BehaviorTree->BlackboardAsset)
            {
                UBlackboardComponent *Blackboard = nullptr;
                PC->UseBlackboard(PC->BehaviorTree->BlackboardAsset, &Blackboard);
            }
            if (!PC->Blackboard && PC->BehaviorTree)
                PC->RunBehaviorTree(PC->BehaviorTree);
            StopNative();
            AircraftBoarded = false;
            NativeExitAccepted = bHasJumpedFromBus = false;
            JumpAt = 0.f;
            WillThankDriver = Syndicate::Between(0.f, 1.f) < 0.7f;
            ThankDriverAt = Syndicate::Now() + Syndicate::Between(0.5f, 4.f);
            NextAircraftAttempt = Syndicate::Now();
            SetDropKeys();
        }
        else if (Phase == EAthenaGamePhase::Warmup)
        {
            BotState = EBotState::Warmup;
            if (HasPawn)
                BotPhysics::EnableGroundPawn(Pawn);
        }
    }
    void ObservePhase(EAthenaGamePhase Phase)
    {
        if (ObservedPhase == Phase)
            return;
        PreparePhase(Phase);
        // Some client-server builds omit the native phase delegate on fallback
        // controllers. Dispatch the reflected native handler once in that case.
        PC->OnGamePhaseChanged(Phase);
        StopNative();
    }
    void StopNative()
    {
        if (PC && PC->BrainComponent)
            PC->BrainComponent->StopLogic(L"Syndicate owns decisions");
    }
    void SetFire(bool On)
    {
        if(On && Pawn->CurrentWeapon && IsGun(Pawn->CurrentWeapon->WeaponData) && Enemy.Get() && !BotDifficulty::Fire(PC,Syndicate::Now())) On=false;
        if(On && Pawn->CurrentWeapon && IsGun(Pawn->CurrentWeapon->WeaponData) && Enemy.Get() &&
           OwnShotBlocker(LastSeen+AimError)) On=false;
        if (On == Firing)
            return;
        Firing = On;
        if (On)
        {
            Pawn->PawnStartFire(0);
            // Window exposure ends after observed ammunition consumption, not merely a fire request.
        }
        else
            Pawn->PawnStopFire(0);
    }
    void SetADS(bool On)
    {
        auto W = Pawn->CurrentWeapon;
        if (auto Old = AimedWeapon.Get(); Old && Old != W && Old->GetIsTargeting())
            Old->OnSetTargeting(false);
        if (W && W->bAllowTargeting && W->GetIsTargeting() != On)
            W->OnSetTargeting(On);
        AimedWeapon.Set(W);
        if (Pawn->bIsTargeting != On)
        {
            Pawn->bIsTargeting = On;
            Pawn->OnRep_IsTargeting();
        }
    }
    void Sprint(bool On)
    {
        auto ASC = PlayerState->AbilitySystemComponent;
        if (!ASC)
            return;
        for (auto &S : ASC->ActivatableAbilities.Items)
        {
            if (!S.Ability || (!S.Ability->GetName().contains("Sprint") || S.Ability->GetName().contains("Cancel") ||
                               S.Ability->GetName().contains("Stop")))
                continue;
            if (!On)
            {
                if (SprintHeld)
                {
                    auto Handle=S.Handle;
                    S.InputPressed = false;
                    ASC->ActivatableAbilities.MarkItemDirty(S);
                    ASC->ServerSetInputReleased(Handle);
                    // A deliberate release is not an activation failure.
                    // Native ability cooldown still decides the next attempt.
                    NextSprintAttempt=std::min(NextSprintAttempt,Syndicate::Now()+.1f);
                }
                SprintHeld = false;
                return;
            }
            if (SprintHeld || Syndicate::Now() < NextSprintAttempt)
                return;
            NextSprintAttempt = Syndicate::Now() + 3.f;
            S.InputPressed = true;
            ASC->ActivatableAbilities.MarkItemDirty(S);
            UGameplayAbility *Instance = nullptr;
            FPredictionKey Key{};
            auto Handle = S.Handle;
            SprintHeld = InternalTryActivateAbility(ASC, Handle, Key, &Instance, nullptr, nullptr);
            if (!SprintHeld)
                if (auto Current = AbilitySystemComponent::FindAbilitySpecFromHandle(ASC, Handle))
                {
                    Current->InputPressed = false;
                    ASC->ActivatableAbilities.MarkItemDirty(*Current);
                }
            return;
        }
    }
    void GiveItem(UFortItemDefinition *D, int N = 1, int Ammo = 0) { FortInventory::Add(PC, D, N, Ammo); }
    void Equip(const FFortItemEntry &E)
    {
        if (!E.ItemDefinition || !E.ItemDefinition->IsA(UFortWeaponItemDefinition::StaticClass()))
            return;
        if (Pawn->CurrentWeapon && Pawn->CurrentWeapon->bIsReloadingWeapon && IsGun(Pawn->CurrentWeapon->WeaponData)) return;
        if (Pawn->CurrentWeapon && Pawn->CurrentWeapon->ItemEntryGuid == E.ItemGuid)
            return;
        SetFire(false);
        SetADS(false);
        FortInventory::Equip(PC, Pawn, E.ItemGuid);
    }
    void EquipPickaxe()
    {
        auto I = FortInventory::Get(PC);
        if (I)
            for (auto E : I->Inventory.ReplicatedEntries)
                if (E.ItemDefinition && E.ItemDefinition->IsA(UFortWeaponMeleeItemDefinition::StaticClass()))
                {
                    Equip(E);
                    return;
                }
    }
    bool IsPickaxeEquiped()
    {
        return Pawn->CurrentWeapon && Pawn->CurrentWeapon->WeaponData &&
               Pawn->CurrentWeapon->WeaponData->IsA(UFortWeaponMeleeItemDefinition::StaticClass());
    }
    bool HasGun()
    {
        auto I = FortInventory::Get(PC);
        if (I)
            for (auto E : I->Inventory.ReplicatedEntries)
                if (E.Count > 0 && E.ItemDefinition && IsGun(E.ItemDefinition))
                    return true;
        return false;
    }
    bool CanFight()
    {
        auto I = FortInventory::Get(PC);
        if (!I) return false;
        for (auto E : I->Inventory.ReplicatedEntries)
            if (E.Count > 0 && E.ItemDefinition && IsGun(E.ItemDefinition))
            {
                auto D = static_cast<UFortWeaponRangedItemDefinition *>(E.ItemDefinition);
                auto Ammo = FortInventory::FindItemEntry(PC, D->GetAmmoWorldItemDefinition_BP());
                auto Held = Pawn->CurrentWeapon;
                int Loaded = Held && Held->ItemEntryGuid == E.ItemGuid ? Held->AmmoCount : E.LoadedAmmo;
                if (Loaded > 0 || (Ammo && Ammo->Count > 0)) return true;
            }
        return false;
    }
    bool HasEngagementAmmo()
    {
        auto I=FortInventory::Get(PC); if(!I) return false;
        for(auto E:I->Inventory.ReplicatedEntries)
            if(E.Count>0 && IsGun(E.ItemDefinition))
            {
                auto D=static_cast<UFortWeaponRangedItemDefinition*>(E.ItemDefinition);
                auto Held=Pawn->CurrentWeapon;
                int Loaded=Held && Held->ItemEntryGuid==E.ItemGuid ? Held->AmmoCount : E.LoadedAmmo;
                int Reserve=FortInventory::CountDefinition(PC,D->GetAmmoWorldItemDefinition_BP());
                if(Loaded+Reserve>=std::clamp(Looting::GetClipSize(D),1,30)) return true;
            }
        return false;
    }
    int MaterialCount()
    {
        int Total = 0;
        for (auto Type : {EFortResourceType::Wood, EFortResourceType::Stone, EFortResourceType::Metal})
            Total += FortInventory::CountDefinition(PC, UFortKismetLibrary::K2_GetResourceItemDefinition(Type));
        return Total;
    }
    void SimpleSwitchToWeapon(float Distance = 1800.f)
    {
        if (Syndicate::Now() < BuildingUntil) return;
        if(Syndicate::Now()<HighGroundUntil && (HighGroundStage || RampActive))
        { auto Target=Enemy.Get(); if(!Target || !PC->LineOfSightTo(Target,AimOrigin(),true)) return; }
        if (auto Held = Pawn->CurrentWeapon; Held && Held->bIsReloadingWeapon && IsGun(Held->WeaponData)) return;
        if (auto W = Pawn->CurrentWeapon)
            if (auto E = FortInventory::FindItemEntry(PC, W->ItemEntryGuid); E && E->LoadedAmmo != W->AmmoCount)
            {
                E->LoadedAmmo = std::max(0, W->AmmoCount);
                FortInventory::Update(PC, E);
            }

        auto I = FortInventory::Get(PC);
        if (!I)
            return;
        FFortItemEntry Best{};
        float Score = -10000.f;
        // Range policy distinguishes shotguns from usable long-range guns.
        for (auto E : I->Inventory.ReplicatedEntries)
        {
            if (!E.ItemDefinition || !IsGun(E.ItemDefinition) || E.Count <= 0)
                continue;
            auto D = static_cast<UFortWeaponRangedItemDefinition *>(E.ItemDefinition);
            auto Stock = FortInventory::FindItemEntry(PC, D->GetAmmoWorldItemDefinition_BP());
            // Keep an owned gun equipped after tools, even when seeking ammo.
            // Any usable gun takes priority over every unloaded fallback.
            float V = BotMission::WeaponScore(Role(D),Distance,E.LoadedAmmo,
                Stock ? Stock->Count : 0,static_cast<int>(D->Rarity),
                PlayerCombat.Followup(Syndicate::Now(),Distance,Role(D),E.LoadedAmmo));
            V+=CombatSkill::RangePreference(LoadoutKind(D),Distance,PlayerCombat.Followup(Syndicate::Now(),Distance,Role(D),E.LoadedAmmo));
            if(Enemy.Get()==PressureTarget.Get()&&Syndicate::Now()<PressureUntil&&Distance<1600.f&&LoadoutKind(D)==4&&
               (E.LoadedAmmo>0||(Stock&&Stock->Count>0)))V+=100.f;
            if (V > Score)
            {
                Best = E;
                Score = V;
            }
        }
        if (Best.ItemDefinition)
            Equip(Best);
    }
    void CancelEmote()
    {
        auto ASC = PlayerState ? PlayerState->AbilitySystemComponent : nullptr;
        if (!ASC)
            return;
        std::vector<Syndicate::Ref<UGameplayAbility>> Instances;
        for (auto &S : ASC->ActivatableAbilities.Items)
            if (S.ActiveCount && S.Ability && S.Ability->GetName().contains("Emote"))
            {
                for (auto A : S.NonReplicatedInstances)
                    Instances.emplace_back(A);
                for (auto A : S.ReplicatedInstances)
                    Instances.emplace_back(A);
            }
        for (auto &Ref : Instances)
            if (auto A = Ref.Get(); A && !A->IsDefaultObject())
                A->K2_CancelAbility(); // Native ability cleanup releases its movement/fire restrictions.
    }
    void Emote(UAthenaDanceItemDefinition *Chosen=nullptr)
    {
        if (Pawn->bIsPlayingEmote)
            return;
        if (!Chosen && Pawn->CosmeticLoadout.Dances.Num() == 0)
            return;
        auto D = Chosen?Chosen:Pawn->CosmeticLoadout.Dances[Syndicate::Pick(Pawn->CosmeticLoadout.Dances.Num())];
        auto C = StaticLoadObject<UClass>("/Game/Abilities/Emotes/GAB_Emote_Generic.GAB_Emote_Generic_C");
        if (!D || !C || !C->DefaultObject || !PlayerState->AbilitySystemComponent)
            return;
        FGameplayAbilitySpec S{};
        AbilitySpecConstructor(&S, static_cast<UGameplayAbility *>(C->DefaultObject), 1, -1, D);
        GiveAbilityAndActivateOnce(PlayerState->AbilitySystemComponent, &S.Handle, S);
    }
    void Look(const FVector &Point, bool Air = false)
    {
        auto Origin = Air ? Pawn->K2_GetActorLocation() : AimOrigin();
        if (!Syndicate::Finite(Point))
            return;
        auto Desired = UKismetMathLibrary::FindLookAtRotation(Origin, Point);
        float GroundYawRate=BuildingVisual ? 900.f : 250.f*BotDifficulty::Scale(PC,BotDifficulty::AimTracking,.2f);
        if (!AimReady)
        {
            Aim = PC->GetControlRotation();
            AimReady = true;
            AimTick = tick_counter - 1;
        }
        auto Turn = [this, Air](float C, float W, float R)
        {
            float D = std::remainder(W - C, 360.f);
            if (Air) return C + std::clamp(D, -R * Delta, R * Delta); // Preserve the established native-flight steering rate.
            float Step = std::clamp(D * (BuildingVisual ? 12.f : 8.f), -R, R) * std::min(Delta, 0.1f);
            return C + std::clamp(Step, -std::abs(D), std::abs(D));
        };
        if (AimTick != tick_counter)
        {
            Aim.Pitch = Turn(Aim.Pitch, std::clamp(Desired.Pitch, -85.f, 85.f), Air ? 90.f : 150.f*BotDifficulty::Scale(PC,BotDifficulty::AimTracking,.2f));
            Aim.Yaw = std::remainder(Turn(Aim.Yaw, Desired.Yaw, Air ? 70.f : GroundYawRate), 360.f);
        }
        Aim.Roll = 0;
        AimTick = tick_counter;
        PC->bSetControlRotationFromPawnOrientation = false;
        FVector Eyes{};
        FRotator EyeRotation{};
        Pawn->GetActorEyesViewPoint(&Eyes, &EyeRotation);
        float Pitch = static_cast<float>(Aim.Pitch) * 3.14159265f / 180.f,
              Yaw = static_cast<float>(Aim.Yaw) * 3.14159265f / 180.f;
        // Native AI point-focus flattens controller pitch. A perceived pawn
        // focus preserves native vertical targeting between our updates.
        auto FocusTarget=Enemy.Get();
        if(CombatActorFocus&&!Air&&!BuildingVisual&&FocusTarget&&!FocusTarget->IsDead())PC->K2_SetFocus(FocusTarget);
        else
        {
            CombatActorFocus=false;
            PC->K2_SetFocalPoint(Eyes + FVector(std::cos(Pitch) * std::cos(Yaw), std::cos(Pitch) * std::sin(Yaw), std::sin(Pitch)) * 1000.f);
        }
        PC->SetControlRotation(Aim);
        Pawn->StoredControlRotation = Aim;
        Pawn->bUseControllerRotationYaw = false; // Native movement interpolates body yaw; FaceRotation must not snap it.
        if (Pawn->CharacterMovement)
        {
            Pawn->CharacterMovement->bOrientRotationToMovement = false;
            Pawn->CharacterMovement->bUseControllerDesiredRotation = true;
            if (!Air) Pawn->CharacterMovement->RotationRate = FRotator(0.f, GroundYawRate, 0.f);
        }
    }
    void UpdateGroundView()
    {
        if(BuildingVisual)return; // Retain the build input pose; body-yaw feedback was undoing it each tick.
        if (!AimReady) { Aim = Pawn->K2_GetActorRotation(); AimReady = true; }
        if (AimTick != tick_counter)
        {
            PC->K2_ClearFocus();
            auto Body = Pawn->K2_GetActorRotation();
            auto Turn = [this](float Current, float Desired, float Rate)
            {
                float Difference = std::remainder(Desired-Current,360.f);
                return Current + std::clamp(Difference,-Rate*std::min(Delta,0.1f),Rate*std::min(Delta,0.1f));
            };
            Aim.Yaw = std::remainder(Turn(Aim.Yaw,Body.Yaw,250.f),360.f);
            Aim.Pitch = Turn(Aim.Pitch,Body.Pitch,150.f);
        }
        Aim.Roll = 0.f;
        PC->bSetControlRotationFromPawnOrientation = false;
        PC->SetControlRotation(Aim);
        Pawn->StoredControlRotation = Aim;
        Pawn->bUseControllerRotationYaw = false;
        if (Pawn->CharacterMovement) Pawn->CharacterMovement->RotationRate = FRotator(0.f,BuildingVisual ? 900.f : 250.f,0.f);
        float Pitch = std::fmod(static_cast<float>(Aim.Pitch)+360.f,360.f);
        Pawn->RemoteViewPitch = static_cast<uint8_t>(Pitch*(256.f/360.f)) & 255;
    }
    void StopMove()
    {
        if (HasGoal || LocalSteering)
        {
            PC->StopMovement();
            PC->K2_ClearFocus();
        }
        HasGoal = LocalSteering = RampSteering = false;
        SteeringDirection = {};
        Sprint(false);
    }
    std::vector<Syndicate::Ref<AFortPickup>> LocalPickups(float Radius)
    { return BotSpatial::Pickups.Nearby(Pawn->K2_GetActorLocation(), Radius); }
    std::vector<Syndicate::Ref<ABuildingContainer>> LocalContainers(float Radius)
    { return BotSpatial::Containers.Nearby(Pawn->K2_GetActorLocation(), Radius); }
    std::vector<Syndicate::Ref<AFortPlayerPawnAthena>> LocalPawns(float Radius)
    { return BotSpatial::Pawns.Nearby(Pawn->K2_GetActorLocation(), Radius); }
    bool RequestJump()
    {
        float N = Syndicate::Now();
        if (N < NextJump || Pawn->bPressedJump || !Pawn->CharacterMovement ||
            Pawn->CharacterMovement->MovementMode != EMovementMode::MOVE_Walking || !Pawn->CanJump())
            return false;
        Pawn->Jump();
        JumpReleaseAt = N + Syndicate::Between(0.1f, 0.18f);
        bool HeightJump=FightBuild.Active() || (N<HighGroundUntil && (HighGroundStage || RampActive || HeightResumeCorner));
        NextJump = N + Syndicate::Between(HeightJump ? .18f : 4.f, HeightJump ? .28f : 8.f);
        return true;
    }
    void SteeringTick()
    {
        if (!LocalSteering || !HasGoal)
            return;
        BotWork::Scope Measure(BotWork::Steering);
        auto Here = Pawn->K2_GetActorLocation();
        float N = Syndicate::Now();
        if (N >= NextSteer || (RampSteering && FightBuild.Active()))
        {
            // Include a conservative movement margin for this one-second
            // index snapshot; validate only this nearby crowd in each probe.
            auto Crowd = BotSpatial::Pawns.Nearby(Here, 1400.f);
            NextSteer = N + (RampSteering && FightBuild.Active() ? .025f : .15f);
            auto D = MoveGoal - Here;
            if(RampSteering&&FightBuild.Active()&&!RampEntering)D=BotTactics::RampDriveDelta(Here,RampEntry,RampGoal);
            float L = std::hypot(D.X, D.Y);
            float Feet=Here.Z-(Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f);
            bool Arrived=RampSteering ?
                (Syndicate::Distance2D(Here,MoveGoal)<=MoveRadius && std::abs(Here.Z-MoveGoal.Z)<45.f &&
                 (!RampActive || RampEntering || Feet>=RampExitFeet)) :
                (BotTactics::Reached(Here,MoveGoal,MoveRadius) ||
                 (GroundGoal && Syndicate::Distance2D(Here,MoveGoal)<=MoveRadius));
            if (Arrived)
            {
                StopMove();
                return;
            }
            if(RampSteering && RampEntering && FightBuild.Active() && L<=MoveRadius)
            {SteeringDirection={};return;} // Settle vertically on the paid floor without circling its entry point.
            if (L < 1.f) { SteeringDirection = {}; return; } // A goal above the pawn needs a route/ramp, not undefined horizontal input.
            D = FVector(D.X / L, D.Y / L, 0);
            if(N<GroundRescueUntil)
            {SteeringDirection=BotPhysics::NativeGroundStep(Pawn,D,std::min(L,45.f)) ? D : FVector{};}
            else if(RampSteering || DoorPass.Get())
            {
                // Query collision, then let native character movement physically climb the owned stair.
                // Navigation tiles may still describe the terrain underneath a newly built ramp.
                float Probe=RampSteering ? std::min(L,60.f) : 60.f;
                bool AirBuild=FightBuild.Active() && Pawn->CharacterMovement &&
                    Pawn->CharacterMovement->MovementMode==EMovementMode::MOVE_Falling;
                bool Safe=AirBuild ? BotPhysics::SafeBuildAirStep(Pawn,D,FightFloor.Get(),RampActor.Get(),Probe) :
                    BotPhysics::SafeStep(Pawn,D,&Crowd,false,Probe);
                SteeringDirection=Safe ? D : FVector{};
                if(RampSteering && FightBuild.Active() && !RampEntering &&
                   Pawn->CharacterMovement && Pawn->CharacterMovement->MovementMode==EMovementMode::MOVE_Walking)
                {
                    auto Velocity=Pawn->GetVelocity();
                    float Speed=std::hypot(Velocity.X,Velocity.Y);
                    float Braking=std::max(100.f,Pawn->CharacterMovement->BrakingDecelerationWalking);
                    if(Syndicate::Finite(Velocity))
                        SteeringDirection=SteeringDirection*BotSkillPolicy::CrestInput(L,Speed,Braking);
                }
                if(RampSteering && !FightBuild.Active() && SteeringDirection.IsZero() && N-RampProgressAt>1.f) RequestJump();
            }
            else
            {
            // Keep the actual world heading around a ledge/wall. Testing the
            // direct goal first on every sample made bots turn back into it as
            // soon as a short sweep cleared, then reverse again next frame.
            bool Committed = N < SteerCommitUntil;
            bool Escaped = L + 300.f < SteerEntryDistance && BotPhysics::SafeStep(Pawn, D, &Crowd, GroundGoal);
            if (Committed && !Escaped && !SteeringDirection.IsZero() &&
                BotPhysics::SafeStep(Pawn, SteeringDirection, &Crowd, GroundGoal))
                ; // Continue the same detour; native collision still owns the pawn.
            else
            {
                if (Escaped || !Committed) SteeringSide = 0.f;
                float Sign = SteeringSide ? (SteeringSide > 0.f ? 1.f : -1.f) : CombatSide;
                float Angles[] = {0.f, Sign * 40.f, Sign * 85.f, Sign * 110.f, -Sign * 40.f,
                                    -Sign * 85.f, -Sign * 110.f, Sign * 70.f, -Sign * 70.f, 0.f};
                if (Committed && !Escaped)
                {
                    Angles[0] = Sign * 85.f; Angles[1] = Sign * 40.f;
                    Angles[2] = Sign * 110.f; Angles[3] = -Sign * 85.f; Angles[4] = 0.f;
                }
                SteeringDirection = {};
                for (float Degrees : Angles)
                {
                    float R = Degrees * 3.14159265f / 180.f;
                    FVector Candidate(D.X * std::cos(R) - D.Y * std::sin(R),
                                      D.X * std::sin(R) + D.Y * std::cos(R), 0);
                    if(!Pawn->bIsInAnyStorm && !AllowedDestination(Here+Candidate*150.f,BotState==EBotState::Warmup)) continue;
                    if(Pawn->bIsInAnyStorm && RotationActive &&
                       !BotTactics::RotationContour(Here,Here+Candidate*150.f,RotationGoal)) continue;
                    if (!BotPhysics::SafeStep(Pawn, Candidate, &Crowd, GroundGoal)) continue;
                    SteeringDirection = Candidate;
                    if (Degrees && (!Committed || !SteeringSide || Degrees * SteeringSide < 0.f))
                    {
                        SteeringSide = Degrees > 0.f ? 40.f : -40.f;
                        SteerCommitUntil = N + 1.2f;
                        SteerEntryDistance = L;
                    }
                    break;
                }
            }
            }
        }
        if (!SteeringDirection.IsZero())
        {
            // Only used after path following failed. Never feed a second input
            // while native navigation already owns movement, or scale input by tick rate.
            Pawn->AddMovementInput(SteeringDirection, 1.f, false);
            if (!Pawn->bIsTargeting && BotState != EBotState::Combat && !RampSteering)
                Pawn->bUseControllerRotationYaw = false;
            if (Pawn->CharacterMovement && !Pawn->bIsTargeting && BotState != EBotState::Combat && !RampSteering)
            {
                Pawn->CharacterMovement->bOrientRotationToMovement = true;
                Pawn->CharacterMovement->bUseControllerDesiredRotation = false;
            }
        }
    }
    bool Move(const FVector &Goal, float Radius = 160.f, bool Run = true, bool GroundRoute = false)
    {
        if (!Syndicate::Finite(Goal) || (!Pawn->bIsInAnyStorm && !AllowedDestination(Goal,BotState==EBotState::Warmup)))
            return false;
        auto Here = Pawn->K2_GetActorLocation();
        float N = Syndicate::Now();
        if (RouteMemory.Blocked(Goal,N) || (N < BlockedUntil && Syndicate::Distance2D(Goal, BlockedGoal) < 500.f))
            return false;
        if (HasGoal && RouteMemory.Loop(Here,N,BotState != EBotState::Combat || (!FightBuild.Active()&&N-LastSeenTime>2.f)))
        {
            RouteMemory.Fail(MoveGoal,N); RouteMemory.Fail(Goal,N);
            BlockedGoal=Goal; BlockedUntil=N+30.f;
            StopMove(); NextPath=N+0.5f;
            return false;
        }
        if (BotTactics::Reached(Here, Goal, Radius) ||
            (GroundRoute && Syndicate::Distance2D(Here,Goal) <= Radius))
        {
            StopMove();
            return true;
        }
        if(RampSteering) { StopMove(); NextPath=0.f; }
        Sprint(Run && !Pawn->bIsTargeting);
        Navigation.Observe(Here,N);
        // A genuinely new movement attempt gets startup time. Reissuing a
        // request while that attempt is active cannot renew this allowance.
        if(!HasGoal) Navigation.BeginMission(N,BotState==EBotState::Warmup);
        bool Changed = !HasGoal || GroundRoute != GroundGoal || Syndicate::Distance2D(Goal, MoveGoal) >= 180.f || (!GroundRoute && std::abs(Goal.Z - MoveGoal.Z) > 140.f);
        if (HasGoal && N - MotionSampleAt >= 0.75f)
        {
            auto Travel = Here - MotionLocation;
            float TravelLength = std::hypot(Travel.X, Travel.Y);
            if (TravelLength > 25.f)
            {
                auto Direction = FVector(Travel.X / TravelLength, Travel.Y / TravelLength, 0);
                if (!MotionDirection.IsZero() && Direction.X * MotionDirection.X + Direction.Y * MotionDirection.Y < -0.65f)
                    ++MotionReversals;
                MotionDirection = Direction;
            }
            if (TravelLength > 25.f || std::abs(Travel.Z) > 40.f)
                LastMotionAt = N;
            MotionSampleAt = N; MotionLocation = Here;
        }
        if (HasGoal && !LocalSteering && N-Navigation.At>1.5f && N-Navigation.RequestedAt>1.5f)
        {
            PC->StopMovement(); // Release the stalled owner before feeding native character input.
            LocalSteering = true; NextSteer = N; NextPath = N + 5.f; ++RouteRecoveries;
        }
        if(Changed&&BotState==EBotState::Combat&&N-Navigation.RequestedAt>=.65f)NextPath=std::min(NextPath,N);
        // A changing target cannot restart the path on every AI tick.
        if ((!HasGoal || N >= NextPath) &&
            (Changed || LocalSteering || PC->GetMoveStatus() == EPathFollowingStatus::Idle))
        {
            if (Changed)
            {
                LastProgress = N;
                LastProgressLocation = Here;
                BestGoalDistance = Syndicate::Distance2D(Here, Goal);
                MotionLocation = Here; MotionSampleAt = RouteStartedAt = N;
                LastMotionAt=Navigation.At;
                MotionDirection = {}; MotionReversals = 0;
                SteerCommitUntil = 0.f;
                NextSteer = N;
                SteeringDirection = {};
            }
            NextPath = N + 5.f;Navigation.Requested(N);
            auto RouteGoal = Goal;
            float RouteDistance = Syndicate::Distance2D(Here, Goal);
            if (RouteDistance > 3000.f)
            {
                auto Direction = Goal - Here;
                RouteGoal = Here + FVector(Direction.X / RouteDistance, Direction.Y / RouteDistance, 0) * 2000.f;
            }
            FVector Projected{};
            bool Navigable = NativeNavigation::Project(RouteGoal, Projected);
            // Reject remote projections/partial paths which end away from the goal.
            Navigable = Navigable && Syndicate::Distance2D(Projected, RouteGoal) <= std::max(Radius, 250.f) &&
                (GroundRoute || std::abs(Projected.Z - RouteGoal.Z) <= 160.f);
            auto R = Navigable ? PC->MoveToLocation(Projected, Radius, true, true, false, true, nullptr, false)
                               : EPathFollowingRequestResult::Failed;
            bool WasLocal = LocalSteering;
            LocalSteering = R == EPathFollowingRequestResult::Failed;
            if (LocalSteering && !WasLocal) PC->StopMovement();
            // Path acceptance is not physical progress; Navigation.Observe owns that evidence.
            HasGoal = true;
            MoveGoal = GroundRoute && Navigable ? Projected : Goal;
            GroundGoal = GroundRoute;
            MoveRadius = Radius;
        }
        float Remaining = Syndicate::Distance2D(Here, MoveGoal);
        if (Remaining + 80.f < BestGoalDistance)
        {
            LastProgress = N;
            LastProgressLocation = Here;
            BestGoalDistance = Remaining;
            MotionReversals = 0;
        }
        else if (MotionReversals >= 3 || (LocalSteering && GroundRoute && Pawn->bIsInAnyStorm && SteeringDirection.IsZero() && N-LastMotionAt>2.5f) ||
                 Navigation.Stalled(N,LocalSteering ? 4.f : 6.f))
        {
            RouteMemory.Fail(MoveGoal,N);
            BlockedGoal = MoveGoal;
            BlockedUntil = N + 20.f;
            StopMove();
            NextPath = N + 0.5f;
            LastProgress = N;
            // Pick another route after a failed move. Jumping on a timer made
            // every stalled bot hop repeatedly without an obstacle to clear.
            return false;
        }
        return HasGoal;
    }
    bool MoveRamp(const FVector &Goal,float Radius)
    {
        if(!Syndicate::Finite(Goal) || !RampActive || !RampActor.Get()) return false;
        auto Here=Pawn->K2_GetActorLocation();
        float Feet=Here.Z-(Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f);
        if(Syndicate::Distance2D(Here,Goal)<=Radius && std::abs(Here.Z-Goal.Z)<45.f &&
           (RampEntering || Feet>=RampExitFeet))
        { StopMove(); return true; }
        if(!RampSteering || Syndicate::Distance2D(MoveGoal,Goal)>30.f || std::abs(MoveGoal.Z-Goal.Z)>45.f)
        { PC->StopMovement(); PC->K2_ClearFocus(); NextSteer=Syndicate::Now(); SteeringDirection={}; }
        Sprint(false); HasGoal=LocalSteering=RampSteering=true; GroundGoal=false;
        MoveGoal=Goal; MoveRadius=Radius;
        return true;
    }
    bool IsNamed(AActor *A)
    {
        auto N = A->GetName();
        if (auto Volume = Cast<AFortPoiVolume>(A))
            for (auto &Tag : Volume->LocationTags.GameplayTags)
                N += Tag.TagName.ToString();
        const char *Names[] = {"Salty",  "Paradise", "Retail",   "Pleasant", "Fatal",  "Lucky",  "Sunny",
                               "Lazy",   "Junk",     "Haunted",  "Snobby",   "Greasy", "Shifty", "Polar",
                               "Frosty", "Lonely",   "Pressure", "Dusty",    "Starry", "Gotham", "Syndie",
                               "Loot",   "Happy",    "Hamlet",   "Moisty"};
        for (auto K : Names)
            if (N.contains(K))
                return true;
        return false;
    }
    std::vector<Syndicate::Ref<ABuildingContainer>> DropContainersAt(const FVector &Point,float Radius)
    { return BotSpatial::Containers.Nearby(Point,Radius); }
    std::vector<Syndicate::Ref<AFortPickup>> DropPickupsAt(const FVector &Point,float Radius)
    { return BotSpatial::Pickups.Nearby(Point,Radius); }
    // Late game: a chest roof, or else open ground, inside the late-game circle the bus flies over.
    bool ChooseLateGameDrop()
    {
        if (DropRouteReady && !TargetDropZone.IsZero() && LateGame::InDropCircle(TargetDropZone)) return true;
        TargetDropZone={}; DropVolume.Clear(); DropRegionKey.clear();
        int Budget=8;
        size_t Start=ContainerCache.empty()?0:Syndicate::Pick(static_cast<int>(ContainerCache.size()));
        for (size_t I=0; I<ContainerCache.size() && Budget; ++I)
            if (auto C=ContainerCache[(Start+I)%ContainerCache.size()].Get(); C && !C->bHidden && !C->bDestroyed &&
                !C->bAlreadySearched && !C->IsActorBeingDestroyed() && C->SearchLootTierGroup.ToString().contains("Treasure"))
            {
                auto At=C->K2_GetActorLocation();
                if (!LateGame::InDropCircle(At) || BotLanding::Density(At)>=BotLanding::MaxNearbyArrivals) continue;
                --Budget; FVector Surface{}; bool Structure=false;
                if (!BotLanding::Surface(At,Surface,Structure) || BotLanding::Density(Surface)>=BotLanding::MaxNearbyArrivals) continue;
                TargetDropZone=Surface; if (DropRouteReady) BotLanding::Claim(Surface); return true;
            }
        for (int Try=0; Try<8; ++Try)
        {
            FVector Surface{}; bool Structure=false;
            // Probe two heights: hills and low ground (the probe looks 10,000 units down from above the point).
            if ((BotLanding::Surface(LateGame::RandomDropPoint(2000.f),Surface,Structure) ||
                 BotLanding::Surface(LateGame::RandomDropPoint(-4000.f),Surface,Structure)) &&
                LateGame::InDropCircle(Surface) && BotLanding::Density(Surface)<BotLanding::MaxNearbyArrivals)
            { TargetDropZone=Surface; if (DropRouteReady) BotLanding::Claim(Surface); return true; }
        }
        return false;
    }
    void ChooseDrop()
    {
        if (LateGame::Active() && ChooseLateGameDrop()) return;
        struct Destination { AActor *Actor; FVector Point; FVector Center{},Extent{}; BotLanding::Region *Region=nullptr; };
        std::vector<Destination> Named, Other, Coverage;
        if (DropRouteReady && !DropRegionKey.empty() && !TargetDropZone.IsZero()) return;
        for (auto &R : BotLanding::Regions)
            if (Syndicate::Now()>=R.RetryAt && (!DropRouteReady || BotLanding::ReachableFromRoute(R.Center,DropRouteStart,DropRouteEnd)))
                Coverage.push_back({nullptr,R.Center,R.Center,R.Extent,&R});
        std::stable_sort(Coverage.begin(),Coverage.end(),[](const auto &A,const auto &B){return A.Region->Assigned<B.Region->Assigned;});
        if (Coverage.empty())
        {
        for (auto &Ref : POICache)
            if (auto V = Ref.Get())
            {
                FVector Point{};
                if (!BotPhysics::LandingPoint(V, Point))
                    continue;
                if (DropRouteReady && !BotLanding::ReachableFromRoute(Point,DropRouteStart,DropRouteEnd)) continue;
                Other.push_back({V, Point});
                if (IsNamed(V))
                    Named.push_back({V, Point});
            }
        bool HaveLocatedPOI=!Other.empty();
        for (auto &Ref : FoundationCache)
            if (auto A = Ref.Get())
                if (A && !A->bHidden && !A->IsActorBeingDestroyed())
                {
                    FVector Point{};
                    if (!BotPhysics::LandingPoint(A, Point))
                        continue;
                    if (DropRouteReady && !BotLanding::ReachableFromRoute(Point,DropRouteStart,DropRouteEnd)) continue;
                    if (!HaveLocatedPOI) Other.push_back({A, Point});
                    if (IsNamed(A))
                        Named.push_back({A, Point});
                }
        }
        auto &Pool = !Coverage.empty() ? Coverage : ((!Named.empty() && Syndicate::Between(0, 1) < 0.7f) ? Named : Other);

        auto Start=Coverage.empty()?Syndicate::Pick(static_cast<int>(Pool.size())):0;
        int TraceBudget=24;
        for (size_t Attempt=0; Attempt<std::min<size_t>(Pool.size(),8); ++Attempt)
        {
            if (TraceBudget<=8) break; // Keep a bounded real-building fallback budget.
            auto Selected = Pool[(Start+Attempt)%Pool.size()];
            int RegionBudget=std::min(Selected.Region?8:4,TraceBudget-8);
            // Prefer actual chest-bearing buildings inside the POI. Trace
            // from the sky so indoor loot selects its roof, not the ceiling
            // rejected by a short floor probe. Native flight owns the descent.
            FVector Origin=Selected.Center, Extent=Selected.Extent;
            if (Selected.Actor) Selected.Actor->GetActorBounds(false, &Origin, &Extent);
            std::vector<FVector> Buildings, LandingLoot;
            auto Add = [&](const FVector &At)
            {
                if (!TraceBudget || !RegionBudget ||
                    (DropRouteReady && BotLanding::Density(At)>=BotLanding::MaxNearbyArrivals) ||
                    !BotLanding::Inside(At, Origin, Extent) ||
                    (DropRouteReady && !BotLanding::ReachableFromRoute(At,DropRouteStart,DropRouteEnd))) return;
                --TraceBudget; --RegionBudget;
                FVector Surface{}; bool Structure = false;
                if (!BotLanding::Surface(At, Surface, Structure) ||
                    !BotLanding::Inside(Surface, Origin, Extent) ||
                    (DropRouteReady && BotLanding::Density(Surface)>=BotLanding::MaxNearbyArrivals)) return;
                auto &Candidates = Structure ? Buildings : LandingLoot;
                // Do not weight a roof by the number of pickups in one room.
                for (auto &Existing : Candidates)
                    if (Syndicate::Distance2D(Existing, Surface) < 300.f) return;
                Candidates.push_back(Surface);
            };
            float Radius=std::min(20000.f,std::hypot(Extent.X,Extent.Y));
            auto Containers=DropContainersAt(Origin,Radius);
            size_t ContainerStart = Containers.empty() ? 0 : Syndicate::Pick(static_cast<int>(Containers.size()));
            for (size_t I = 0; I < Containers.size() && TraceBudget && RegionBudget && Buildings.size() < 4; ++I)
                if (auto C = Containers[(ContainerStart + I) % Containers.size()].Get(); C && !C->bHidden && !C->bDestroyed &&
                    !C->IsActorBeingDestroyed() && !C->bAlreadySearched &&
                    C->SearchLootTierGroup.ToString().contains("Treasure"))
                    Add(C->K2_GetActorLocation());
            auto Pickups=DropPickupsAt(Origin,Radius);
            size_t PickupStart = Pickups.empty() ? 0 : Syndicate::Pick(static_cast<int>(Pickups.size()));
            for (size_t I = 0; I < Pickups.size() && TraceBudget && RegionBudget && Buildings.size() < 4; ++I)
                if (auto P = Pickups[(PickupStart + I) % Pickups.size()].Get(); P && !P->bPickedUp &&
                    P->PickupSourceTypeFlags == EFortPickupSourceTypeFlag::FloorLoot &&
                    IsGun(P->PrimaryPickupItemEntry.ItemDefinition))
                    Add(P->K2_GetActorLocation());
            auto &Candidates = Buildings.empty() ? LandingLoot : Buildings;
            if (!Candidates.empty())
            {
                DropVolume.Set(Cast<AFortPoiVolume>(Selected.Actor));
                auto Choice=Syndicate::Pick(static_cast<int>(Candidates.size()));
                for (size_t I=0; I<Candidates.size(); ++I)
                    if (BotLanding::Density(Candidates[I])<BotLanding::Density(Candidates[Choice])) Choice=static_cast<int>(I);
                TargetDropZone=Candidates[Choice];
                if (DropRouteReady)
                {
                    BotLanding::Claim(TargetDropZone);
                    if (Selected.Region)
                    {
                        ++Selected.Region->Assigned; DropRegionKey=Selected.Region->Key;
                        Log("POI landing assigned: "+DropRegionKey+" count="+std::to_string(Selected.Region->Assigned));
                    }
                }
                return;
            }
            if (Selected.Region) Selected.Region->RetryAt=Syndicate::Now()+1.f; // Failed probes never count as coverage.
        }
        // Dedicated 10.40 can retain POI tags while removing the brush roots.
        // Fall back to actual chest-bearing buildings, never an empty marker
        // center or map zero. This is a landing goal; native flight still owns
        // all pawn positions, collision, deployment and descent.
        if (!TargetDropZone.IsZero()) return;
        FVector GroundFallback{};
        size_t ChestStart=ContainerCache.empty()?0:Syndicate::Pick(static_cast<int>(ContainerCache.size()));
        for (size_t I=0; I<ContainerCache.size() && TraceBudget; ++I)
            if (auto C=ContainerCache[(ChestStart+I)%ContainerCache.size()].Get(); C && !C->bHidden &&
                !C->bDestroyed && !C->bAlreadySearched && !C->IsActorBeingDestroyed() &&
                C->SearchLootTierGroup.ToString().contains("Treasure"))
            {
                auto At=C->K2_GetActorLocation();
                if (!Syndicate::Finite(At) || At.IsZero() ||
                    (DropRouteReady && !BotLanding::ReachableFromRoute(At,DropRouteStart,DropRouteEnd)) ||
                    BotLanding::Density(At)>=BotLanding::MaxNearbyArrivals) continue;
                --TraceBudget; FVector Surface{}; bool Structure=false;
                if (!BotLanding::Surface(At,Surface,Structure) ||
                    BotLanding::Density(Surface)>=BotLanding::MaxNearbyArrivals) continue;
                if (Structure)
                { TargetDropZone=Surface; DropVolume.Clear(); if (DropRouteReady) BotLanding::Claim(Surface); return; }
                if (GroundFallback.IsZero()) GroundFallback=Surface;
            }
        if (TargetDropZone.IsZero() && !GroundFallback.IsZero())
        { TargetDropZone=GroundFallback; DropVolume.Clear(); if (DropRouteReady) BotLanding::Claim(GroundFallback); }
    }
    bool JumpFromBus(AActor *Bus, bool Force = false)
    {
        if (bHasJumpedFromBus || NativeExitAccepted)
            return true;
        auto Aircraft = Syndicate::As<AFortAthenaAircraft>(Bus);
        auto World = UWorld::GetWorld();
        auto State = World ? Syndicate::As<AFortGameStateAthena>(World->GameState) : nullptr;
        if (!Aircraft || !State || !ControllerAlive() || State->GamePhase != EAthenaGamePhase::Aircraft)
            return false;
        float N = Syndicate::Now();
        if (!Force && N < NextAircraftAttempt)
            return false;
        NextAircraftAttempt = N + 0.25f;
        if (!AircraftBoarded)
        {
            AircraftBoarded = NativeBotAircraft::Board(PC, Aircraft);
            if (!AircraftBoarded)
                return false;
            bNativeAircraft = true;

        }
        if (WillThankDriver && N >= ThankDriverAt && !PlayerState->bThankedBusDriver)
        {
            PlayerState->bThankedBusDriver = true;
            PlayerState->OnRep_ThankedBusDriver();
            PlayerState->ForceNetUpdate();
        }
        if (!JumpAt)
        {
            if (!Force && (N<NextDropPlanning || !BotLanding::PermitPlanning(N)))
            { NextAircraftAttempt=N; return false; } // Deferred plans retry next frame, not in synchronized quarter-second batches.
            auto BusLocation=Aircraft->K2_GetActorLocation();
            float Heading=Aircraft->FlightInfo.FlightStartRotation.Yaw*3.14159265f/180.f;
            FVector Velocity(std::cos(Heading)*Aircraft->FlightInfo.FlightSpeed,
                             std::sin(Heading)*Aircraft->FlightInfo.FlightSpeed,0.f);
            float Begin=std::max(N,Aircraft->DropStartTime);
            float End=std::max(Begin,Aircraft->DropEndTime-1.f);
            DropRouteStart=BusLocation+Velocity*(Begin-N);
            DropRouteEnd=BusLocation+Velocity*(End-N);
            DropRouteReady=true;
            if (!Force) { TargetDropZone={}; ChooseDrop(); NextDropPlanning=N+0.5f; }
            if (TargetDropZone.IsZero())
            {
                // A forced final exit can use a real cached floor weapon if
                // no roof plan was available. No fake inventory or pawn move.
                if (Force) for (auto &Ref : PickupCache)
                    if (auto P=Ref.Get(); P && !P->bPickedUp && !P->bHidden &&
                        P->PickupSourceTypeFlags==EFortPickupSourceTypeFlag::FloorLoot &&
                        IsGun(P->PrimaryPickupItemEntry.ItemDefinition) &&
                        BotLanding::ReachableFromRoute(P->K2_GetActorLocation(),DropRouteStart,DropRouteEnd))
                    { TargetDropZone=P->K2_GetActorLocation(); break; }
                if (TargetDropZone.IsZero() && !Force) return false;
            }
            JumpAt=BotLanding::DepartureTime(BusLocation,Velocity,TargetDropZone,N,Begin,End);
        }
        SetDropKeys();
        if ((!Force && N < JumpAt) || State->bAircraftIsLocked)
            return false;
        NativeExitAccepted = NativeBotAircraft::TryDeparture(PC, Aircraft);
        if (NativeExitAccepted)
        {
            RefreshPawn();

        }
        return NativeExitAccepted;
    }
    void SetDropKeys()
    {
        float N = Syndicate::Now();
        if (N < NextDropKeys)
            return;
        NextDropKeys = N + 1.f;
        if (PC->Blackboard && !TargetDropZone.IsZero())
            for (auto E : PC->AIEvaluators)
            {
                if (auto J = Syndicate::As<UFortAthenaAIBotEvaluator_JumpOffBus>(E))
                {
                    J->CachedAthenaGameState = Syndicate::As<AFortGameStateAthena>(UWorld::GetWorld()->GameState);
                    PC->Blackboard->SetValueAsVector(J->JumpOffBusDestinationName, TargetDropZone);
                    if (auto Volume = DropVolume.Get())
                    {
                        J->BusDroppingVolume = Volume;
                        PC->Blackboard->SetValueAsObject(J->JumpOffBusDestinationVolumeKeyName, Volume);
                    }
                }
                if (auto F = Syndicate::As<UFortAthenaAIBotEvaluator_FreeFalling>(E))
                {
                    PC->Blackboard->SetValueAsVector(F->JumpOffBusDestinationName, TargetDropZone);
                    PC->Blackboard->SetValueAsVector(F->DiveDestinationKeyName, TargetDropZone);
                    PC->Blackboard->SetValueAsVector(F->GlideDestinationKeyName, TargetDropZone);
                }
            }
    }
    void Land()
    {
        if(LandedAt<=0.f){LandedAt=Syndicate::Now();MaterialIncome.Land(LandedAt);AmmoIncome.Land(LandedAt);}
        DeviceAirUntil=0.f;CombatBaseZ=Pawn->K2_GetActorLocation().Z;
        BotLanding::RecordArrival(DropRegionKey,Pawn->K2_GetActorLocation());
        if (!DropRegionKey.empty()) Log("POI landing completed: "+DropRegionKey+
            (BotLanding::FindRegion(DropRegionKey) && BotLanding::Inside(Pawn->K2_GetActorLocation(),BotLanding::FindRegion(DropRegionKey)->Center,BotLanding::FindRegion(DropRegionKey)->Extent)?" inside region":" missed region"));

        StopNative();
        StopMove();
        PC->ResetIgnoreMoveInput();
        PC->ResetIgnoreLookInput();
        BotPhysics::EnableGroundPawn(Pawn);
        BotState = EBotState::Landed;
        LastHealth = Pawn->GetHealth(); LastShield = Pawn->GetShield();
        RampActive = false; BreachTarget.Clear(); HarvestTarget.Clear();
        AimReady = false;
        NextLootSearch = NextSense = NextHarvest = NextPath = NextStrafe = 0.f;
        NextPhysicsCheck = Syndicate::Now() + 2.f;
        LootTarget.Clear();
        Enemy.Clear(); Threat.Clear(); ThreatUntil=0.f; PursuitSearching=false;
        IgnoredLoot.Clear();
        IgnoreUntil = BlockedUntil = SteerCommitUntil = 0.f;
        LastProgress = Syndicate::Now();
        LastProgressLocation = Pawn->K2_GetActorLocation();
        if(HasGun())SimpleSwitchToWeapon();
    }
    void AirTick()
    {
        if(DeviceStage==7&&(Pawn->bIsSkydiving||Pawn->bIsParachuteOpen))FinishDevice(true);
        auto G = Syndicate::As<AFortGameStateAthena>(UWorld::GetWorld()->GameState);
        if (!G)
            return;
        float Now=Syndicate::Now();
        if (TargetDropZone.IsZero() && Now>=NextDropPlanning && BotLanding::PermitPlanning(Now))
        { NextDropPlanning=Now+0.5f; ChooseDrop(); }
        if (NativeExitAccepted && (Pawn->bIsSkydiving || Pawn->bIsParachuteOpen))
        {
            if (!bHasJumpedFromBus)
            {
                bHasJumpedFromBus = true;
                StopNative();
                bNativeAircraft = false;

                AimReady = false;
            }
            BotState = Pawn->bIsParachuteOpen ? EBotState::Gliding : EBotState::Skydiving;
            // Preserve working input magnitudes; native CharacterMovement owns descent and deployment.
            // Never steer toward zero after a failed destination lookup.
            if (TargetDropZone.IsZero()) return;
            FVector Here = Pawn->K2_GetActorLocation(), D = TargetDropZone - Here;
            if (BotState == EBotState::Gliding && Pawn->CharacterMovement)
            {
                auto Velocity=Pawn->CharacterMovement->Velocity;
                float Lead=std::clamp((Here.Z-TargetDropZone.Z)/std::max(100.f,std::abs(Velocity.Z)),0.f,0.65f);
                D.X-=Velocity.X*Lead; D.Y-=Velocity.Y*Lead;
            }
            D.Z = 0;
            float L = std::hypot(D.X, D.Y);
            if (L > (BotState == EBotState::Gliding ? 100.f : 1.f))
            {
                D.X /= L;
                D.Y /= L;
                Pawn->AddMovementInput(D, 1.f, true);
            }
            if (BotState == EBotState::Skydiving)
            {
                Pawn->AddMovementInput(FVector(0, 0, -1), 1.f, true);
                Look(TargetDropZone, true);
                // Distant map destinations need an earlier ordinary glider
                // request, rather than faster movement or pawn relocation.
                if (L > 18000.f && Here.Z-TargetDropZone.Z > 2500.f && Now >= NextGliderRequest)
                {
                    NextGliderRequest = Now + 1.f;
                    bool Consumed = false;
                    Pawn->OnOpenParachute(&Consumed);
                }
            }
            else if (L > 200.f)
            {
                FVector Point = TargetDropZone;
                Point.Z = Here.Z;
                Look(Point, true);
            }
            return;
        }
        if (bHasJumpedFromBus)
        {
            // Do not label a momentary falling/flight-flag transition as landed.
            if (Pawn->CharacterMovement && Pawn->CharacterMovement->MovementMode == EMovementMode::MOVE_Falling)
                return;
            Land();
            return;
        }
        BotState = EBotState::Bus;
        JumpFromBus(G->GetAircraft(0));
        if (G->GamePhase > EAthenaGamePhase::Aircraft && !bNativeWarning)
        {
            bNativeWarning = true;
            Log("Native aircraft exit has not completed; inspect Phoebe aircraft service/blackboard. No relocation "
                "fallback applied.");
        }
    }

    FGuid SwapFor(UFortItemDefinition *D,int IncomingAmmo=-1)
    {
        FGuid None{};auto I=FortInventory::Get(PC);
        if(!I||!D||FortInventory::GetQuickBars(D)!=EFortQuickBars::Primary)return None;
        int Slot=KitSlot(D);if(Slot<0||Slot>=5)return None;
        int Counts[5]{};FFortItemEntry Upgrade{},Duplicate{};float Worst=10000.f;
        for(auto E:I->Inventory.ReplicatedEntries)
            if(E.Count>0&&E.ItemDefinition&&FortInventory::GetQuickBars(E.ItemDefinition)==EFortQuickBars::Primary)
            {int X=KitSlot(E.ItemDefinition);if(X>=0&&X<5)++Counts[X];}
        for(auto E:I->Inventory.ReplicatedEntries)
        {
            if(E.Count<=0||!E.ItemDefinition||FortInventory::GetQuickBars(E.ItemDefinition)!=EFortQuickBars::Primary)continue;
            int X=KitSlot(E.ItemDefinition);float Q=KitQuality(E.ItemDefinition);
            bool LoadedReplacement=IncomingAmmo>0&&IsGun(D)&&!CanFight()&&IsGun(E.ItemDefinition);
            if(X==Slot&&(KitQuality(D)>Q+.1f||LoadedReplacement)&&(!Upgrade.ItemDefinition||Q<KitQuality(Upgrade.ItemDefinition)))Upgrade=E;
            if(X>=0&&X<5&&Counts[X]>1&&Q<Worst){Duplicate=E;Worst=Q;}
        }
        if(Upgrade.ItemDefinition)return Upgrade.ItemGuid;
        if(FortInventory::IsFullInventory(PC)&&Counts[Slot]==0&&Duplicate.ItemDefinition)return Duplicate.ItemGuid;
        // Critical HP can displace the flex slot, never the sole shotgun/AR.
        if(FortInventory::IsFullInventory(PC)&&Slot==4&&Pawn->GetHealth()<35.f)
            for(auto E:I->Inventory.ReplicatedEntries)
                if(E.Count>0&&KitSlot(E.ItemDefinition)==2&&FortInventory::GetQuickBars(E.ItemDefinition)==EFortQuickBars::Primary)return E.ItemGuid;
        return None;
    }
    bool Wants(UFortItemDefinition *D,int IncomingAmmo=-1)
    {
        if(!D)return false;
        auto E=FortInventory::FindItemEntry(PC,D);
        if(Cast<UFortResourceItemDefinition>(D))return FortInventory::ResourceRoom(PC,D)>0;
        if(D->IsStackable()&&E)return E->Count<static_cast<int>(D->MaxStackSize);
        if(FortInventory::GetQuickBars(D)==EFortQuickBars::Secondary)return !E||E->Count<static_cast<int>(D->MaxStackSize);
        FGuid Empty{};if(SwapFor(D,IncomingAmmo)!=Empty)return true;
        if(FortInventory::IsFullInventory(PC))return false;
        int Slot=KitSlot(D);if(Slot<0||Slot>=5)return false;
        if(auto I=FortInventory::Get(PC))for(auto X:I->Inventory.ReplicatedEntries)
            if(X.Count>0&&X.ItemDefinition&&FortInventory::GetQuickBars(X.ItemDefinition)==EFortQuickBars::Primary&&KitSlot(X.ItemDefinition)==Slot)return false;
        return true;
    }
    bool LootVisible(AActor *Target)
    {
        if(!Target || Target->IsActorBeingDestroyed()) return false;
        if(PC->LineOfSightTo(Target,AimOrigin(),true)) return true;
        auto Eyes=AimOrigin();FVector Origin{},Extent{};Target->GetActorBounds(true,&Origin,&Extent);
        if(!Syndicate::Finite(Origin) || !Syndicate::Finite(Extent)) return false;
        auto Point=FVector(std::clamp(Eyes.X,Origin.X-Extent.X,Origin.X+Extent.X),
            std::clamp(Eyes.Y,Origin.Y-Extent.Y,Origin.Y+Extent.Y),
            std::clamp(Eyes.Z,Origin.Z-Extent.Z,Origin.Z+Extent.Z));
        if(auto Pickup=Syndicate::As<AFortPickup>(Target)) Point=Pickup->K2_GetActorLocation()+FVector(0,0,25.f);
        FHitResult Hit{};TArray<AActor*> Ignored;Ignored.Add(Pawn);
        if(Pawn->CurrentWeapon) Ignored.Add(Pawn->CurrentWeapon);
        bool Blocked=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),Eyes,Point,
            BotPhysics::PawnProfile(),false,Ignored,EDrawDebugTrace::None,&Hit,true,{},{},0);
        Ignored.Free();
        return !Hit.bStartPenetrating && (!Blocked || Hit.Actor.Get()==Target);
    }
    AFortPickup *ChestPickup()
    {
        float N=Syndicate::Now();
        if(N>=ChestPickupUntil) {ChestPickups.clear();return nullptr;}
        auto Ammo=NeededAmmo();bool Armed=HasGun();
        AFortPickup *Best=nullptr;float Score=100000.f;
        std::erase_if(ChestPickups,[&](auto &Ref)
        {
            auto P=Ref.Get();
            return !P || P->bPickedUp || P->bHidden || P->PrimaryPickupItemEntry.Count<=0 ||
                !Wants(P->PrimaryPickupItemEntry.ItemDefinition,P->PrimaryPickupItemEntry.LoadedAmmo);
        });
        for(auto &Ref:ChestPickups) if(auto P=Ref.Get())
        {
            auto D=P->PrimaryPickupItemEntry.ItemDefinition;
            if(Reserved(P,this) || (IgnoredLoot.Get()==P && N<IgnoreUntil) ||
               P->GetDistanceTo(Pawn)>1200.f || !AllowedDestination(P->K2_GetActorLocation())) continue;
            float Value=P->GetDistanceTo(Pawn);
            if(!Armed && IsGun(D)) Value-=4000.f;
            else if(D==Ammo) Value-=2000.f;
            if(Value<Score) {Score=Value;Best=P;}
        }
        return Best;
    }
    void Pickup(AFortPickup *P)
    {
        if (!P || P->bPickedUp || P->bHidden || !Wants(P->PrimaryPickupItemEntry.ItemDefinition,P->PrimaryPickupItemEntry.LoadedAmmo) ||
            P->GetDistanceTo(Pawn) > 350.f)
            return;
        if(P->GetDistanceTo(Pawn)<180.f) Posture.Request(BotTactics::Posture::Pickup,Syndicate::Now(),.18f);
        bool ChestOutput=false;
        for(auto &Ref:ChestPickups) if(Ref.Get()==P) {ChestOutput=true;break;}
        if(PickupFlight::Begin(Pawn, P, FVector(), true))
        {PendingPickup.Set(P);PendingPickupAt=Syndicate::Now();if(ChestOutput) ++ChestOutputClaims;}
    }
    int FinishPickup(AFortPickup *P)
    {
        if (!P || !PC->Inventory) return 0;
        bool WasArmed = HasGun();
        auto E = P->PrimaryPickupItemEntry;
        int Accepted = FortInventory::Transfer(PC, Pawn, E, SwapFor(E.ItemDefinition,E.LoadedAmmo));
        if (LootTarget.Get() == P) { Release(this); LootTarget.Clear(); }
        if (Accepted > 0 && IsGun(E.ItemDefinition) && !WasArmed)
            if (auto Owned = FortInventory::FindItemEntry(PC, E.ItemDefinition)) Equip(*Owned);
        if(Accepted>0) {++LootTransfers;++GroundNativeSerial;}
        if(PendingPickup.Get()==P) PendingPickup.Clear();
        return Accepted;
    }
    bool ExpireLoot()
    {
        auto Target = LootTarget.Get();
        if (!Target) return false;
        float N = Syndicate::Now();
        float Distance = Syndicate::Distance2D(Pawn->K2_GetActorLocation(), Target->K2_GetActorLocation()) +
                         std::abs(Pawn->K2_GetActorLocation().Z - Target->K2_GetActorLocation().Z);
        auto Blocker = BreachTarget.Get();
        int Materials = MaterialCount();
        bool Progress = Distance + 60.f < LootBestDistance || Materials > LootMaterials;
        if (Blocker)
        {
            float Health = Blocker->GetHealth();
            if (LootProgressBlocker.Get() == Blocker && Health + 0.5f < LootBlockerHealth) Progress = true;
            LootProgressBlocker.Set(Blocker); LootBlockerHealth = Health;
        }
        LootMaterials = Materials;
        if (Progress)
        {
            LootProgressAt = N; LootBestDistance = Distance;
            for (auto &R : Reservations)
                if (R.Owner == this && R.Actor.Get() == Target) R.Until = N + 18.f;
        }
        // A physical climb, harvest or damaging pickaxe swing extends the
        // objective. Reacquiring the same undamaged wall is not progress.
        if (N - LootProgressAt <= (CanFight() ? 8.f : 5.f) && N - LootSince <= 60.f) return false;
        RouteMemory.Fail(Target->K2_GetActorLocation(),N);
        if (HasGoal) RouteMemory.Fail(MoveGoal,N);
        IgnoredLoot.Set(Target); IgnoreUntil = Syndicate::Now() + 30.f;
        if (auto Blocker = BreachTarget.Get())
        { IgnoredBreach.Set(Blocker); IgnoreBreachUntil = Syndicate::Now() + 20.f; }
        Release(this); LootTarget.Clear(); BreachTarget.Clear(); HarvestTarget.Clear();
        ++LootRouteFailures;LootApproachUntil=0.f;
        RampActive = false; SetFire(false); StopMove();
        if (HasGun()) SimpleSwitchToWeapon();
        NextLootSearch = 0.f;
        return true;
    }
    void BeginLootTarget(AActor *Target)
    {
        Release(this); LootTarget.Set(Target);
        LootInteraction.Clear();
        LootInteraction.Ready(reinterpret_cast<std::uintptr_t>(Target),Syndicate::Now(),
            Target->GetDistanceTo(Pawn)<=350.f && LootVisible(Target),.3f);
        LootApproachUntil=NextLootApproach=0.f;
        float N = Syndicate::Now(); LootSince = LootProgressAt = N;
        LootBestDistance = Syndicate::Distance2D(Pawn->K2_GetActorLocation(), Target->K2_GetActorLocation()) +
                           std::abs(Pawn->K2_GetActorLocation().Z - Target->K2_GetActorLocation().Z);
        LootMaterials = MaterialCount(); LootProgressBlocker.Clear(); LootBlockerHealth = 0.f;
        Reservations.push_back({Syndicate::Ref<AActor>(Target), this, N + 18.f});
    }
    UFortItemDefinition *NeededAmmo()
    {
        auto I = FortInventory::Get(PC);
        if (!I) return nullptr;
        if (auto Held = Pawn->CurrentWeapon; Held && IsGun(Held->WeaponData))
        {
            auto D = static_cast<UFortWeaponRangedItemDefinition *>(Held->WeaponData);
            auto Ammo = D->GetAmmoWorldItemDefinition_BP();
            if (Ammo && FortInventory::CountDefinition(PC, Ammo) < BotSkillPolicy::AmmoReserve(Looting::GetClipSize(D))) return Ammo;
        }
        for (auto E : I->Inventory.ReplicatedEntries)
            if (E.Count > 0 && IsGun(E.ItemDefinition))
            {
                auto D = static_cast<UFortWeaponRangedItemDefinition *>(E.ItemDefinition);
                auto Ammo = D->GetAmmoWorldItemDefinition_BP();
                if (Ammo && FortInventory::CountDefinition(PC, Ammo) < BotSkillPolicy::AmmoReserve(Looting::GetClipSize(D))) return Ammo;
            }
        return nullptr;
    }
    bool AllowedDestination(const FVector &Goal, bool Warmup = false)
    {
        if (Warmup) return Syndicate::Finite(Goal);
        auto World = UWorld::GetWorld();
        auto State = World ? Cast<AFortGameStateAthena>(World->GameState) : nullptr;
        auto Zone = State ? Syndicate::Ref<AFortSafeZoneIndicator>(State->SafeZoneIndicator).Get() : nullptr;
        if(!Syndicate::Finite(Goal)) return false;
        if(!Zone || !State || State->GamePhase<EAthenaGamePhase::SafeZones) return true;
        auto Here=Pawn->K2_GetActorLocation();float N=Syndicate::Now();
        if(!BotTactics::AvoidStormReentry(Here,Goal,Zone->GetSafeZoneCenter(),Zone->GetSafeZoneRadius())) return false;
        if(!Pawn->bIsInAnyStorm && (BotState==EBotState::Combat || FightBuild.Active()) &&
           Enemy.Get() && N-LastSeenTime<12.f && Syndicate::Distance2D(Here,Goal)<=1200.f &&
           BotTactics::CombatRotationBudget(Goal,Zone->NextCenter,Zone->NextRadius,N,
               Zone->SafeZoneFinishShrinkTime,false)) return true;
        if(RotationActive && BotState==EBotState::MovingToSafeZone &&
           BotTactics::RotationContour(Here,Goal,Zone->NextCenter)) return true;
        if(Zone->NextRadius<=0.f) return true;
        if(BotTactics::SafeDestination(Here,Goal,Zone->NextCenter,Zone->NextRadius,
            Pawn->bIsInAnyStorm || N+60.f>=Zone->SafeZoneStartShrinkTime)) return true;
        // A short local loot route is permitted only with time to walk there and back into the next ring.
        float Local=Syndicate::Distance2D(Here,Goal);
        return !Pawn->bIsInAnyStorm && Local<=2500.f &&
            BotTactics::CanCommitFight(Goal,Zone->NextCenter,Zone->NextRadius,N+Local/180.f,
                Zone->SafeZoneStartShrinkTime,false);
    }
    bool SeekCriticalHeal()
    {
        if(Pawn->GetHealth()>=100.f || HealItem().ItemDefinition || Syndicate::Now()<NextHealSearch) return false;
        auto Here=Pawn->K2_GetActorLocation(); float N=Syndicate::Now();
        AFortPickup *Best=nullptr;float Distance=Pawn->bIsInAnyStorm ? 350.f : (Pawn->GetHealth()<35.f ? 900.f : 500.f);
        for(auto Ref:LocalPickups(Distance))
            if(auto P=Ref.Get(); P && !P->bHidden && !P->bPickedUp && P->PrimaryPickupItemEntry.Count>0 &&
               UsableHealthHeal(P->PrimaryPickupItemEntry.ItemDefinition,Pawn->GetHealth()) && Wants(P->PrimaryPickupItemEntry.ItemDefinition,P->PrimaryPickupItemEntry.LoadedAmmo) &&
               !Reserved(P,this) && AllowedDestination(P->K2_GetActorLocation()) && LootVisible(P))
            {
                float D=P->GetDistanceTo(Pawn);
                if(D<Distance && (!AmmoThreatened() || BotTactics::AmmoEscapeTarget(Here,P->K2_GetActorLocation(),LastSeen)))
                { Best=P;Distance=D; }
            }
        NextHealSearch=N+0.3f; // A failed heal search must not suppress nearby weapon/ammo collection.
        if(!Best) return false;
        SetFire(false); SetADS(false); HarvestTarget.Clear(); BreachTarget.Clear();
        if(LootTarget.Get()!=Best) { StopMove(); BeginLootTarget(Best); }
        if(Distance<=350.f) Pickup(Best); else Move(Best->K2_GetActorLocation(),120.f,true,true);
        return true;
    }
    AActor *FindLootTarget(bool Warmup)
    {
        BotWork::Scope Measure(BotWork::LootSearch);
        if(!Warmup) if(auto P=ChestPickup()) return P;
        auto Here = Pawn->K2_GetActorLocation();
        float N = Syndicate::Now();
        bool NeedGun = !HasGun();
        auto Ammo = NeededAmmo();
        bool FightReady=CanFight();
        bool AvoidThreat=!FightReady && AmmoThreatened();
        float Health=Pawn->GetHealth();
        bool NeedAmmo = Ammo!=nullptr;
        bool NeedLoadedGun=!FightReady;
        AActor *AmmoTarget=nullptr;float AmmoScore=18000.f;
        AActor *HealTarget=nullptr;float HealScore=2200.f;
        AActor *FloorWeapon = nullptr, *Chest = nullptr, *Other = nullptr, *DistantWeapon = nullptr;
        float DistantScore = 16000.f;
        float WeaponScore = Warmup ? 6000.f : 16000.f, ChestScore = 16000.f, OtherScore = 9000.f;
        for (auto &R : LocalPickups(Warmup ? 6500.f : 16000.f))
            if (auto P = R.Get(); P && !P->bHidden && !P->bPickedUp && !Reserved(P, this) &&
                !(IgnoredLoot.Get() == P && IgnoreUntil > N) && P->PrimaryPickupItemEntry.Count > 0)
            {
                auto D = P->PrimaryPickupItemEntry.ItemDefinition;
                bool Heal=!Warmup && UsableHealthHeal(D,Health);
                if (!Wants(D,P->PrimaryPickupItemEntry.LoadedAmmo) || (!Heal && !EligiblePickup(D, NeedGun, Warmup))) continue;
                auto At = P->K2_GetActorLocation();
                if(!Warmup && AvoidThreat &&
                   !BotTactics::AmmoEscapeTarget(Here,At,LastSeen)) continue;
                if (RouteMemory.Blocked(At,N) || !AllowedDestination(At,Warmup)) continue;
                float Dist = Syndicate::Distance2D(Here, At), Height = std::abs(At.Z - Here.Z);
                float Score = Dist+Height*1.8f;
                if(N<LandingLootUntil && Syndicate::Distance2D(At,LandingLootAnchor)>1800.f) Score+=1400.f;
                if(Heal && (Health<35.f || !NeedGun) && Score<HealScore) { HealTarget=P; HealScore=Score; }
                if ((NeedGun || (NeedLoadedGun && P->PrimaryPickupItemEntry.LoadedAmmo>0)) && IsGun(D) && Score < DistantScore)
                { DistantWeapon = P; DistantScore = Score; }
                // A weapon visibly on this floor is the fastest way to arm.
                if (IsGun(D) && (FightReady || NeedGun || P->PrimaryPickupItemEntry.LoadedAmmo>0) && Height < 180.f && Dist < (Warmup ? 6000.f : 1800.f) &&
                    Score < WeaponScore && LootVisible(P))
                { FloorWeapon = P; WeaponScore = Score; }
                if (NeedAmmo && D == Ammo && Score < AmmoScore)
                { AmmoTarget=P; AmmoScore=Score; }
                float OtherRank = Score - (D == Ammo ? 2500.f : (IsGun(D) ? 1000.f+150.f*static_cast<int>(D->Rarity) : 0.f)) - (Warmup?0.f:LootImprovement(D));
                if (!NeedGun && Score < 9000.f && OtherRank < OtherScore)
                { Other = P; OtherScore = OtherRank; }
            }
        if (!Warmup)
            for (auto &R : LocalContainers(14000.f))
                if (auto C = R.Get(); C && !C->bHidden && !C->bDestroyed && !C->bAlreadySearched &&
                    !Reserved(C, this) && !(IgnoredLoot.Get() == C && IgnoreUntil > N))
                {
                    auto Group = C->SearchLootTierGroup.ToString();
                    bool Treasure = EligibleContainer(Group, true);
                    bool AmmoBox = !NeedGun && Ammo && Group.contains("Ammo");
                    if (!Treasure && !AmmoBox) continue;
                    auto At = C->K2_GetActorLocation();
                    if(AvoidThreat &&
                       !BotTactics::AmmoEscapeTarget(Here,At,LastSeen)) continue;
                    if (RouteMemory.Blocked(At,N) || !AllowedDestination(At,Warmup)) continue;
                    float Score = Syndicate::Distance2D(Here, At) * 1.1f + std::abs(At.Z - Here.Z) * 1.8f;
                    if(N<LandingLootUntil && Syndicate::Distance2D(At,LandingLootAnchor)>1800.f) Score+=1400.f;
                    if (NeedGun && Score < ChestScore) { Chest = C; ChestScore = Score; }
                    else if (!NeedGun)
                    {
                        if (NeedAmmo && AmmoBox && Score < AmmoScore)
                        { AmmoTarget=C; AmmoScore=Score; }
                        Score-=CombatSkill::UpgradeChestPriority(N-LandedAt,NeedsBetterLoot(),AmmoBox,NeedAmmo);
                        if (Score < OtherScore) { Other = C; OtherScore = Score; }
                    }
                }
        if(HealTarget) return HealTarget;
        if ((NeedGun || NeedLoadedGun) && FloorWeapon) return FloorWeapon;
        if (NeedGun && Chest && (!DistantWeapon || ChestScore < 3500.f || ChestScore < DistantScore * 1.25f)) return Chest;
        if (NeedGun && DistantWeapon && DistantScore<3500.f) return DistantWeapon;
        // Supply crates are real, reserved objectives. Never chase falling,
        // already opened or vehicle/special crates at an arbitrary height.
        for(auto Ref:BotSpatial::Supplies.Nearby(Here,9000.f))
            if(auto Drop=Ref.Get();Drop && InteractionLoot::SupplyAvailable(Drop) &&
               !Reserved(Drop,this) && !(IgnoredLoot.Get()==Drop && IgnoreUntil>N))
            {
                auto At=InteractionLoot::SupplyPoint(Drop);
                auto Velocity=Drop->GetVelocity();
                if(!Syndicate::Finite(Velocity) || std::abs(Velocity.Z)>80.f || RouteMemory.Blocked(At,N) || !AllowedDestination(At) ||
                   (AvoidThreat&&!BotTactics::AmmoEscapeTarget(Here,At,LastSeen))) continue;
                float Score=Syndicate::Distance2D(Here,At)+std::abs(At.Z-Here.Z)*2.f-(NeedsBetterLoot()?4500.f:1800.f);
                if(Score<OtherScore) {Other=Drop;OtherScore=Score;}
            }
        if(NeedGun) return Other ? Other : DistantWeapon;
        // One usable magazine is enough to finish a nearby loadout upgrade.
        // Routine four-magazine top-ups must not monopolize every loot trip.
        if(FightReady&&HasEngagementAmmo())
            if(auto Upgrade=Cast<AFortPickup>(Other);Upgrade&&IsGun(Upgrade->PrimaryPickupItemEntry.ItemDefinition)&&
               LootImprovement(Upgrade->PrimaryPickupItemEntry.ItemDefinition)>0.f)return Upgrade;
        if(AmmoTarget && (!FightReady || !HasEngagementAmmo() || !Other || AmmoScore<=OtherScore+300.f))return AmmoTarget;
        return NeedLoadedGun && DistantWeapon ? DistantWeapon : (Other ? Other : AmmoTarget);
    }
    void CollectNearbyLoot(bool Warmup=false,bool AllowContainer=true)
    {
        float N=Syndicate::Now();
        if (N >= NextNearbyPickup)
        {
            NextNearbyPickup = N + 0.2f;
            for (auto &R : LocalPickups(400.f))
                if (auto P = R.Get(); P && !P->bHidden && !P->bPickedUp &&
                    P->GetDistanceTo(Pawn) <= 350.f && !Reserved(P,this) &&
                    (EligiblePickup(P->PrimaryPickupItemEntry.ItemDefinition, !HasGun(), Warmup) ||
                     (!Warmup && UsableHealthHeal(P->PrimaryPickupItemEntry.ItemDefinition,Pawn->GetHealth()))) &&
                    Wants(P->PrimaryPickupItemEntry.ItemDefinition,P->PrimaryPickupItemEntry.LoadedAmmo) && LootVisible(P))
                { Pickup(P); break; }
            // A chest beside an unarmed bot beats a committed upstairs route.
            // Keep reservations/failed searches, and require real nearby access.
            if (AllowContainer && !Warmup && !ChestPickup() && (!HasGun() || NeededAmmo() || NeedsBetterLoot()))
            {
                ABuildingContainer *Nearby = nullptr;
                float Best = 350.f;
                for (auto &R : LocalContainers(400.f))
                    if (auto C = R.Get(); C && !C->bHidden && !C->bDestroyed && !C->bAlreadySearched &&
                        (HasGun()&&!NeedsBetterLoot() ? C->SearchLootTierGroup.ToString().contains("Ammo") :
                         EligibleContainer(C->SearchLootTierGroup.ToString(), true)) && !Reserved(C, this) &&
                        !(IgnoredLoot.Get() == C && IgnoreUntil > N) && AllowedDestination(C->K2_GetActorLocation()))
                    {
                        float Distance = C->GetDistanceTo(Pawn);
                        if (Distance <= Best && LootVisible(C))
                        { Best = Distance; Nearby = C; }
                    }
                if (Nearby && LootTarget.Get() != Nearby)
                {
                    SetFire(false); StopMove(); BreachTarget.Clear(); HarvestTarget.Clear(); RampActive = false;
                    BeginLootTarget(Nearby);
                }
            }
        }
    }
    bool TryLootApproach(AActor *Target)
    {
        float N=Syndicate::Now();if(!Target || N<NextLootApproach) return false;
        NextLootApproach=N+3.f;
        auto Here=Pawn->K2_GetActorLocation(),At=Target->K2_GetActorLocation();
        if(Syndicate::Distance2D(Here,At)>1600.f) return false;
        auto Forward=Target->GetActorForwardVector();Forward.Z=0.f;
        float Length=std::max(1.f,std::hypot(Forward.X,Forward.Y));Forward=Forward*(1.f/Length);
        FVector Side(-Forward.Y,Forward.X,0),Best{};float Score=100000.f;bool Found=false;
        for(auto Offset:{Forward*200.f,Side*200.f,Side*-200.f,Forward*-200.f})
        {
            FVector Goal{};
            if(!NativeNavigation::Project(At+Offset,Goal) || !Syndicate::Finite(Goal) ||
               Syndicate::Distance2D(Goal,At+Offset)>120.f || std::abs(Goal.Z-At.Z)>180.f) continue;
            Goal.Z+=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->CapsuleHalfHeight : 75.f;
            if(!AllowedDestination(Goal) || RouteMemory.Blocked(Goal,N)) continue;
            TArray<AActor*> Ignored;Ignored.Add(Pawn);FHitResult Hit{};
            auto Point=At+FVector(0,0,25.f);
            bool Blocked=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
                Goal+FVector(0,0,60.f),Point,BotPhysics::PawnProfile(),false,Ignored,
                EDrawDebugTrace::None,&Hit,true,{},{},0);
            Ignored.Free();if(Hit.bStartPenetrating || (Blocked && Hit.Actor.Get()!=Target)) continue;
            float Distance=Syndicate::Distance2D(Here,Goal);
            if(Distance<Score) {Score=Distance;Best=Goal;Found=true;}
        }
        if(!Found) return false;
        StopMove();NextPath=0.f;
        if(!Move(Best,55.f)) return false;
        LootApproachGoal=Best;LootApproachUntil=N+6.f;
        return true; // Retain one accessible search/pickup side; never change it on every decision tick.
    }
    bool WaitingForPickup()
    {
        auto P=PendingPickup.Get();
        bool Wait=BotPlayerStyle::PickupWait(Syndicate::Now(),PendingPickupAt,P && P->bPickedUp);
        if(!Wait) PendingPickup.Clear();
        return Wait;
    }
    void ExploreLootGoal(bool Warmup)
    {
        if(!Warmup&&HasGun()&&ExploreWiderArea())return;
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        if (!HasGoal || BotTactics::Reached(Here, MoveGoal, 250.f))
        {
            if (HasGoal) StopMove(); // Finish once before committing to the next exploration objective.
            FVector Goal{};
            bool Found = false;
            if (!Warmup && N - LastSeenTime < 25.f && Syndicate::Distance2D(Here, LastSeen) > 400.f)
            {
                Goal = LastSeen; Found = Syndicate::Finite(Goal) && !RouteMemory.Blocked(Goal,N);
            }
            auto World=UWorld::GetWorld();
            auto State=World ? Cast<AFortGameStateAthena>(World->GameState) : nullptr;
            auto Circle=State ? State->SafeZoneIndicator : nullptr;
            bool SmallCircle=!Warmup && Circle && State->GamePhase>=EAthenaGamePhase::SafeZones &&
                std::isfinite(Circle->NextRadius) && Circle->NextRadius>0.f && Circle->NextRadius<6000.f &&
                Syndicate::Finite(Circle->NextCenter);
            if (SmallCircle && !Found)
            {
                // Search the publicly visible playable circle, never the
                // hidden opponent's position. Retain one goal until reached.
                for (int I=0; I<3 && !Found; ++I)
                {
                    float Angle=Syndicate::Between(-3.14159265f,3.14159265f);
                    float Radius=Syndicate::Between(0.f,std::max(0.f,Circle->NextRadius-std::min(650.f,Circle->NextRadius*0.3f)));
                    auto Sample=Circle->NextCenter+FVector(std::cos(Angle)*Radius,std::sin(Angle)*Radius,0);
                    Found=NativeNavigation::Project(Sample,Goal) && Syndicate::Finite(Goal) &&
                        Syndicate::Distance2D(Here,Goal)>180.f && AllowedDestination(Goal);
                }
            }
            if (!Found && !SmallCircle && !Warmup && N >= NextExplore && !FoundationCache.empty())
            {
                NextExplore = N + Syndicate::Between(10.f, 18.f);
                auto A = FoundationCache[Syndicate::Pick(static_cast<int>(FoundationCache.size()))].Get();
                if (A)
                {
                    FVector Place{};
                    if (BotPhysics::LandingPoint(A, Place) && Syndicate::Distance2D(Here, Place) < 25000.f &&
                        Syndicate::Distance2D(Here, Place) > 2500.f) { Goal = Place; Found = true; }
                }
            }
            if (!Found)
                Found = NativeNavigation::RandomReachable(Here, Warmup ? 1600.f : 4200.f, Goal);
            if (!Found)
            {
                float Angle = Syndicate::Between(-3.14159265f, 3.14159265f);
                float Range = Syndicate::Between(350.f, Warmup ? 1200.f : 2500.f);
                FVector Ground{};
                Found = BotPhysics::Floor(Here + FVector(std::cos(Angle) * Range, std::sin(Angle) * Range, 0),
                                          Ground, Pawn);
                if (Found)
                    Goal = Ground + FVector(0, 0, Pawn->CapsuleComponent ? Pawn->CapsuleComponent->CapsuleHalfHeight : 88.f);
            }
            if (Found && (!AllowedDestination(Goal,Warmup) || RouteMemory.Blocked(Goal,N))) Found=false;
            if (!Found && !Warmup)
                for (int Attempt=0; Attempt<2 && !Found; ++Attempt)
                    Found=NativeNavigation::RandomReachable(Here,1800.f,Goal) && AllowedDestination(Goal) && !RouteMemory.Blocked(Goal,N);
            if (Found) Move(Goal,160.f,true,!Warmup);
        }
        else
        {
            if (!Warmup && N - LastProgress > 3.f && ReachEnvironment(MoveGoal)) return;
            if (!Move(MoveGoal,160.f,true,!Warmup)) NextLootSearch = 0.f;
        }
    }
    void Loot(bool Warmup = false)
    {
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        ClearFinishedEnvironment();
        if(WaitingForPickup()) {SetFire(false);SetADS(false);StopMove();return;}
        ExpireLoot();
        if(!Warmup) if(auto P=ChestPickup(); P && LootTarget.Get()!=P)
        {StopMove();BeginLootTarget(P);}
        CollectNearbyLoot(Warmup);
        if(WaitingForPickup()) {SetFire(false);StopMove();return;}
        auto Target=LootTarget.Get();
        if(auto P=Syndicate::As<AFortPickup>(Target);P && (P->bPickedUp || P->bHidden ||
           !Wants(P->PrimaryPickupItemEntry.ItemDefinition,P->PrimaryPickupItemEntry.LoadedAmmo))) Target=nullptr;
        if(auto C=Syndicate::As<ABuildingContainer>(Target);C && (C->bAlreadySearched || C->bDestroyed)) Target=nullptr;
        if(auto Drop=Syndicate::As<AFortAthenaSupplyDrop>(Target);Drop && !InteractionLoot::SupplyAvailable(Drop)) Target=nullptr;
        if(!Target) {Release(this);LootTarget.Clear();LootInteraction.Clear();}
        // Divert only for a substantially quicker visible weapon or critical
        // ammo. Keep the actual room/chest route through ordinary rescoring.
        if(Target && (!HasGun() || !CanFight()) && N>=NextLootSearch)
        {
            NextLootSearch=N+BotDifficulty::Delay(PC,BotDifficulty::Looting,.4f);auto Immediate=FindLootTarget(Warmup);
            auto P=Syndicate::As<AFortPickup>(Immediate);
            auto Box=Syndicate::As<ABuildingContainer>(Immediate);
            bool FastWeapon=P && !HasGun() && IsGun(P->PrimaryPickupItemEntry.ItemDefinition) &&
                std::abs(P->K2_GetActorLocation().Z-Here.Z)<180.f && LootVisible(P) &&
                (P->GetDistanceTo(Pawn)<=350.f || P->GetDistanceTo(Pawn)+400.f<Target->GetDistanceTo(Pawn));
            bool Ammo=HasGun() && !CanFight() &&
                ((P && P->PrimaryPickupItemEntry.ItemDefinition==NeededAmmo()) ||
                 (Box && Box->SearchLootTierGroup.ToString().contains("Ammo")));
            if(Immediate && Immediate!=Target && (FastWeapon || Ammo))
            {
                SetFire(false);StopMove();BreachTarget.Clear();HarvestTarget.Clear();RampActive=false;
                Target=Immediate;BeginLootTarget(Target);
            }
        }
        if(!Target && N>=NextLootSearch)
        {
            NextLootSearch=N+BotDifficulty::Delay(PC,BotDifficulty::Looting,.3f);Target=FindLootTarget(Warmup);
            if(Target) {SetFire(false);StopMove();BeginLootTarget(Target);}
        }
        if(!Target) {if(HasGun()) SimpleSwitchToWeapon();ExploreLootGoal(Warmup);return;}
        auto SupplyTarget=Syndicate::As<AFortAthenaSupplyDrop>(Target);
        auto At=SupplyTarget?InteractionLoot::SupplyPoint(SupplyTarget):Target->K2_GetActorLocation();auto C=Syndicate::As<ABuildingContainer>(Target);
        float Capsule=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->CapsuleHalfHeight : 75.f;
        bool SameFloor=!C || std::abs(Here.Z-Capsule-At.Z)<180.f;
        bool Accessible=SameFloor && Target->GetDistanceTo(Pawn)<=350.f && LootVisible(Target);
        bool SearchReady=LootInteraction.Ready(reinterpret_cast<std::uintptr_t>(Target),N,Accessible,.3f);
        if(Accessible)
        {
            // Search first; an old pickaxe breach can never damage a reachable
            // unopened container. Native flight/transfer then owns the pickup.
            SetFire(false);SetADS(false);BreachTarget.Clear();HarvestTarget.Clear();RampActive=false;
            StopMove();if(HasGun()) SimpleSwitchToWeapon();
            Look(At+FVector(0,0,25.f));
            if(auto P=Syndicate::As<AFortPickup>(Target)) Pickup(P);
            else if(C && SearchReady)
            {
                std::vector<AFortPickup*> Output;
                if(BuildingContainer::SpawnLootTracked(C,&Output))
                {
                    ++GroundNativeSerial;ChestPickups.clear();ChestPickupUntil=N+12.f;
                    for(auto P:Output) ChestPickups.emplace_back(P);
                }
                else {IgnoredLoot.Set(C);IgnoreUntil=N+30.f;}
                Release(this);LootTarget.Clear();LootInteraction.Clear();NextLootSearch=0.f;
                if(auto P=ChestPickup()) BeginLootTarget(P);
            }
            else if(auto Drop=Syndicate::As<AFortAthenaSupplyDrop>(Target);Drop && SearchReady)
            {
                std::vector<AFortPickup*> Output;
                if(InteractionLoot::SearchSupply(Drop,Pawn,&Output))
                {
                    ++GroundNativeSerial;ChestPickups.clear();ChestPickupUntil=N+15.f;
                    for(auto P:Output) ChestPickups.emplace_back(P);
                }
                else {IgnoredLoot.Set(Drop);IgnoreUntil=N+3.f;}
                Release(this);LootTarget.Clear();LootInteraction.Clear();NextLootSearch=0.f;
                if(auto P=ChestPickup()) BeginLootTarget(P);
            }
            return;
        }
        auto Approach=At;
        if(C)
        {
            auto Forward=C->GetActorForwardVector();float L=std::max(1.f,std::hypot(Forward.X,Forward.Y));
            Approach=At+FVector(Forward.X/L,Forward.Y/L,0)*120.f;Approach.Z+=Capsule;
        }
        if(N<LootApproachUntil) Approach=LootApproachGoal;
        if(!Warmup && (BreachTarget.Get() || RampActive) && EnvironmentAction()) return;
        bool Stalled=N-LastProgress>2.f;
        if(!Warmup && Stalled && N>=LootApproachUntil && TryLootApproach(Target)) return;
        if(!Warmup && Stalled && ReachEnvironment(Approach)) return;
        if(!Move(Approach,60.f))
        {
            if(!Warmup && TryLootApproach(Target)) return;
            IgnoredLoot.Set(Target);IgnoreUntil=N+20.f;Release(this);LootTarget.Clear();LootInteraction.Clear();
            NextLootSearch=N+BotDifficulty::Delay(PC,BotDifficulty::Looting,.2f);
        }
    }
    void UpdatePosture()
    {
        bool Grounded=Pawn->CharacterMovement &&
            (Pawn->CharacterMovement->MovementMode==EMovementMode::MOVE_Walking ||
             Pawn->CharacterMovement->MovementMode==EMovementMode::MOVE_NavWalking);
        bool Permitted=Grounded && !PropCrouchArea(Pawn->K2_GetActorLocation()) && !Pawn->bIsSkydiving && !Pawn->bIsParachuteOpen && !Pawn->bIsDBNO &&
            !RampActive && !HighGroundStage && !BuildingVisual && !WallEdit.Active() && !LeavingBox &&
            Syndicate::Now()-LastDamageTime>.35f;
        bool Want=Posture.Sample(Syndicate::Now(),Permitted);
        if(Want==CrouchRequested) return;
        CrouchRequested=Want;
        if(Want) { Sprint(false);Pawn->Crouch(false); } else Pawn->UnCrouch(false);
        // Native character movement owns capsule height, speed, encroachment and replication.
    }
    FVector HarvestAim(ABuildingSMActor *Target,const FVector &Fallback)
    {
        float N=Syndicate::Now();
        if(WeakSpotParent.Get()!=Target) { WeakSpotParent.Set(Target);NextWeakSpotProbe=0.f; }
        if(N<NextWeakSpotProbe) return CachedWeakSpot;
        NextWeakSpotProbe=N+.12f;CachedWeakSpot=Fallback;
        if(!Target || Target->bDestroyed || Target->IsActorBeingDestroyed() || Target->GetHealth()<=0.f) return Fallback;
        FVector Origin{},Extent{};Target->GetActorBounds(true,&Origin,&Extent);
        auto Eyes=AimOrigin();int Count=PC->ActiveWeakSpots.Num();
        if(Count<1 || Count>64) return Fallback;
        for(int I=Count-1;I>=0;--I)
        {
            auto &Spot=PC->ActiveWeakSpots[I];
            if(Spot.ParentBuilding.Get()!=Target ||
               !BotTactics::ReachableWeakSpot(Eyes,Spot.Position,Spot.Normal,Origin,Extent)) continue;
            FHitResult Hit{};TArray<AActor*> Ignored;Ignored.Add(Pawn);
            bool Found=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),Eyes,
                Spot.Position-Spot.Normal*8.f,BotPhysics::PawnProfile(),false,Ignored,
                EDrawDebugTrace::None,&Hit,true,{},{},0);
            Ignored.Free();
            if(!Found || Hit.bStartPenetrating || Hit.Actor.Get()!=Target ||
               (Hit.ImpactPoint-Spot.Position).Magnitude()>45.f) continue;
            CachedWeakSpot=Spot.Position;++WeakSpotAims;break;
        }
        return CachedWeakSpot; // Aim/fire inputs only; native harvesting awards weak-point hits/materials.
    }
    bool Harvest()
    {
        float N = Syndicate::Now();
        if (MaterialCount() >= (GatheringBurst ? BotDifficulty::MaterialTarget(PC,GatherGoal) : BotDifficulty::MaterialTarget(PC,BotSkillPolicy::MaterialFloor)))
        {
            SetFire(false);
            HarvestTarget.Clear();
            if (HasGun()) SimpleSwitchToWeapon();
            return false;
        }
        if (N < NextHarvest)
            return false;
        auto Target = HarvestTarget.Get();
        if (!Target)
        {
            float Best = 1800.f;
            for (auto &R : BotSpatial::Resources.Nearby(Pawn->K2_GetActorLocation(),1800.f))
            {
                auto B = R.Get();
                auto Container = Syndicate::As<ABuildingContainer>(B);
                if (!B || B->bHidden || B->IsActorBeingDestroyed() || B->bDestroyed || B->bPlayerPlaced || !B->bAllowResourceDrop ||
                    (Container && !Container->bAlreadySearched) ||
                    (IgnoredHarvest.Get() == B && N < IgnoreHarvestUntil))
                    continue;
                if (!AllowedDestination(B->K2_GetActorLocation())) continue;
                float Dist = B->GetDistanceTo(Pawn);
                if (Dist < Best && PC->LineOfSightTo(B, AimOrigin(), true))
                {
                    Best = Dist;
                    Target = B;
                }
            }
            if (!Target)
            {
                NextHarvest = N + 1.5f;
                return false;
            }
            HarvestTarget.Set(Target);
            HarvestDeadline = N + 12.f;
            LootTarget.Clear(); Release(this);
        }
        auto Container = Syndicate::As<ABuildingContainer>(Target);
        if (Target->bDestroyed || Target->GetHealth() <= 0.f || Target->IsActorBeingDestroyed() ||
            (Container && !Container->bAlreadySearched) || !AllowedDestination(Target->K2_GetActorLocation()) || N > HarvestDeadline)
        {
            SetFire(false);
            IgnoredHarvest.Set(Target); IgnoreHarvestUntil = N + 20.f;
            HarvestTarget.Clear();
            NextHarvest = N + .3f;
            if (HasGun()) SimpleSwitchToWeapon();
            return false;
        }
        SetADS(false);
        EquipPickaxe();
        FVector Origin{}, Extent{};
        Target->GetActorBounds(true, &Origin, &Extent);
        auto Here = Pawn->K2_GetActorLocation();
        auto Point = Syndicate::Finite(Origin) && Syndicate::Finite(Extent) && !Extent.IsZero() ?
            BotTactics::HarvestPoint(Here, Origin, Extent) : Target->K2_GetActorLocation() + FVector(0,0,60);
        if (Syndicate::Distance2D(Here, Point) > 180.f)
        {
            SetFire(false);
            auto D = Here - Point; float Length = std::max(1.f, std::hypot(D.X,D.Y));
            auto Approach = Point + FVector(D.X/Length*140.f,D.Y/Length*140.f,0);
            Approach.Z = Here.Z;
            if (!Move(Approach, 30.f))
            {
                HarvestTarget.Clear();
                NextHarvest = N + .5f;
                if (HasGun()) SimpleSwitchToWeapon();
                return false;
            }
        }
        else
        {
            StopMove();
            Posture.Request(BotTactics::Posture::Harvest,N);
            Point=HarvestAim(Target,Point);
            Look(Point);
            auto Desired = UKismetMathLibrary::FindLookAtRotation(AimOrigin(), Point);
            SetFire(BotTactics::AimAligned(Desired, PC->GetControlRotation(), Pawn->K2_GetActorRotation()));
        }
        return true;
    }
    void Sense()
    {
        float N = Syndicate::Now();
        if (N < NextSense)
            return;
        NextSense = N + BotDifficulty::Delay(PC,BotDifficulty::Awareness,Syndicate::Between(0.22f, 0.38f));
        BotWork::Scope Measure(BotWork::Sense);
        AFortPlayerPawnAthena *Best = nullptr;
        float BestD = 12000.f*BotDifficulty::Scale(PC,BotDifficulty::Awareness,.25f);
        auto Here = Pawn->K2_GetActorLocation();
        auto Priority=Threat.Get();
        auto PriorityState=Priority ? Syndicate::As<AFortPlayerStateAthena>(Priority->PlayerState) : nullptr;
        if (!Priority || N >= ThreatUntil || Priority->IsDead() || Priority->IsActorBeingDestroyed() ||
            !PriorityState || PriorityState->IsPlayerDead() || PriorityState->bInAircraft || PriorityState->TeamIndex==PlayerState->TeamIndex)
        { Threat.Clear(); Priority=nullptr; }
        if (Priority && PC->LineOfSightTo(Priority,AimOrigin(),true) && Syndicate::Distance2D(Here,Priority->K2_GetActorLocation())<15000.f)
        { Best=Priority; BestD=Syndicate::Distance2D(Here,Priority->K2_GetActorLocation()); }
        if (!Priority)
            if (auto Current=Enemy.Get(); Current && !Current->IsDead() && !Current->IsActorBeingDestroyed())
            {
                auto S=Syndicate::As<AFortPlayerStateAthena>(Current->PlayerState);
                float Distance=Syndicate::Distance2D(Here,Current->K2_GetActorLocation());
                if (S && !S->IsPlayerDead() && !S->bInAircraft && S->TeamIndex!=PlayerState->TeamIndex &&
                    Distance<12000.f && PC->LineOfSightTo(Current,AimOrigin(),true))
                { Best=Current; BestD=std::max(0.f,Distance*0.65f-250.f); }
            }
        auto Current=Enemy.Get();
        bool CommittedHidden=!Priority && !Best && Current && !Current->IsDead() && CanFight() && N<PursuitUntil && N-LastSeenTime<12.f;
        bool Locked=Current && BotPlayerStyle::KeepOpponent(!Current->IsDead() && !Current->IsActorBeingDestroyed(),
            Best==Current,N-LastSeenTime,Syndicate::Distance2D(Here,LastSeen));
        if (!Priority && !CommittedHidden && !Locked) for (auto &R : LocalPawns(14000.f))
            if (auto P = R.Get(); P && P != Pawn && !P->IsDead() && !P->IsActorBeingDestroyed())
            {
                auto S = Syndicate::As<AFortPlayerStateAthena>(P->PlayerState);
                if (!S || S->IsPlayerDead() || S->bInAircraft || S->TeamIndex == PlayerState->TeamIndex)
                    continue;
                float D = Syndicate::Distance2D(Here, P->K2_GetActorLocation());
                if (D < BestD && PC->LineOfSightTo(P, AimOrigin(), true))
                {
                    BestD = D;
                    Best = P;
                }
            }
        if (Best)
        {
            if (Enemy.Get() != Best)
            {
                Enemy.Set(Best);
                ReactionReady = N + BotDifficulty::Delay(PC,BotDifficulty::ReactionSpeed,Syndicate::Between(0.25f, 0.65f));
            }
            PursuitSearching=false;
            LastSeen = Best->K2_GetActorLocation();
            LastSeenTime = N; PursuitUntil=N+12.f*BotDifficulty::Scale(PC,BotDifficulty::Pursuit,.25f);
            bPotentiallyUnderAttack = N - LastDamageTime < 3.f;
        }
        else
        {
            bPotentiallyUnderAttack = N - LastDamageTime < 3.f;
            if (Priority) Enemy.Set(Priority); // Retain identity, never refresh a hidden position.
            else if (N-LastSeenTime > 12.f) Enemy.Clear();
        }
    }
    void RecordDamage(AFortPlayerPawnAthena *Attacker,const FVector &Momentum)
    {
        if (!Attacker || Attacker==Pawn || Attacker->IsDead() || Attacker->IsActorBeingDestroyed()) return;
        auto State=Syndicate::As<AFortPlayerStateAthena>(Attacker->PlayerState);
        if (!State || State->IsPlayerDead() || State->bInAircraft || State->TeamIndex==PlayerState->TeamIndex) return;
        float Now=Syndicate::Now();
        bool SameThreat=Threat.Get()==Attacker && Now<ThreatUntil;
        bool SameEnemy=Enemy.Get()==Attacker;
        Threat.Set(Attacker); ThreatUntil=Now+20.f; NextSense=0.f;
        LastDamageTime=Now; bPotentiallyUnderAttack=true;
        if (!SameEnemy) ReactionReady=Now+BotDifficulty::Delay(PC,BotDifficulty::ReactionSpeed,Syndicate::Between(0.15f,0.35f));
        Enemy.Set(Attacker);
        if (!SameThreat) { PursuitSearching=false; NextPursuitSearch=0.f; }
        auto Here=Pawn->K2_GetActorLocation();
        if (PC->LineOfSightTo(Attacker,AimOrigin(),true))
        { LastSeen=Attacker->K2_GetActorLocation(); LastSeenTime=Now; }
        else if (!SameThreat)
        {
            // An unseen hit gives a search bearing, not the attacker's live coordinates.
            float Length=std::hypot(Momentum.X,Momentum.Y);
            LastSeen=Here;
            if (Syndicate::Finite(Momentum) && Length>0.001f)
                LastSeen=Here+FVector(-Momentum.X/Length,-Momentum.Y/Length,0)*800.f;
            LastSeenTime=Now;
        }
    }
    bool Build(UFortBuildingItemDefinition *Def, const FVector &Requested, float Yaw)
    {
        auto Loc=Requested;
        if(!CommittingBuild && (Def==Floor || Def==Roof))
        {
            FVector Center{},Half{};float Height=0.f;
            if(!BuildingCell(Center,Half,Height)) return false;
            Loc=BotTactics::FloorRoot(Requested,Half,Yaw);
        } // Queued coordinates are already actor roots; committing must not apply the offset twice.
        if (!CommittingBuild && QueuedBuild.Get()) return false;
        if (!Def || !Syndicate::Finite(Loc))
            return false;
        UClass *Class = nullptr;
        for (auto Type : {EFortResourceType::Wood, EFortResourceType::Stone, EFortResourceType::Metal})
        {
            auto Resource = UFortKismetLibrary::K2_GetResourceItemDefinition(Type);
            if (FortInventory::CountDefinition(PC, Resource) < 10) continue;
            if (Type == EFortResourceType::Wood)
                Class = Def == Wall ? APBWA_W1_Solid_C::StaticClass() : (Def == Stair ? APBWA_W1_StairW_C::StaticClass() : (Def == Floor ? APBWA_W1_Floor_C::StaticClass() : APBWA_W1_RoofS_C::StaticClass()));
            else if (Type == EFortResourceType::Stone)
                Class = Def == Wall ? APBWA_S1_Solid_C::StaticClass() : (Def == Stair ? APBWA_S1_StairW_C::StaticClass() : (Def == Floor ? APBWA_S1_Floor_C::StaticClass() : APBWA_S1_RoofS_C::StaticClass()));
            else
                Class = Def == Wall ? APBWA_M1_Solid_C::StaticClass() : (Def == Stair ? APBWA_M1_StairW_C::StaticClass() : (Def == Floor ? APBWA_M1_Floor_C::StaticClass() : APBWA_M1_RoofS_C::StaticClass()));
            if (Class) break;
        }
        if (!Class || !Cast<ABuildingSMActor>(Class->DefaultObject))
            return false;
        auto CDO = static_cast<ABuildingSMActor *>(Class->DefaultObject);
        if (!CDO)
            return false;
        auto Resource = UFortKismetLibrary::K2_GetResourceItemDefinition(CDO->ResourceType);
        auto E = FortInventory::FindItemEntry(PC, Resource);
        if (!E || E->Count < 10)
            return false;
        FGuid G = E->ItemGuid;
        FRotator Rotation(0, Yaw, 0);
        TArray<ABuildingSMActor *> Replacements;
        char Restriction = 0;
        auto Blocked = CantBuild(UWorld::GetWorld(), Class, Loc, Rotation, false, &Replacements, &Restriction);
        std::vector<Syndicate::Ref<ABuildingSMActor>> Replace;
        for (auto Existing : Replacements)
        {
            if (!Existing || !Syndicate::Ref<ABuildingSMActor>(Existing).Get()) continue;
            auto Container = Cast<ABuildingContainer>(Existing);
            // Respect native approved replacement rules, while protecting
            // other players' builds and unopened loot interactions.
            if (Existing->bPlayerPlaced || (Container && !Container->bAlreadySearched)) Blocked = true;
            else Replace.push_back(Syndicate::Ref<ABuildingSMActor>(Existing));
        }
        Replacements.Free();
        if (Blocked) return false;
        UBuildingStructuralSupportSystem *Grid=nullptr;
        UFortKismetLibrary::GetBuildingStructuralSupportSystem(UWorld::GetWorld(),&Grid);
        if(!Grid) return false;
        TArray<ABuildingActor*> GridActors;EFortBuildPreviewMarkerOptionalAdjustment Adjustment{};
        auto GridResult=Grid->CanAddBuildingActorClassToGrid(UWorld::GetWorld(),Class,Loc,Rotation,false,&GridActors,&Adjustment,false);
        GridActors.Free();
        if(GridResult!=EFortStructuralGridQueryResults::CanAdd) return false;
        auto Tool = FortInventory::FindItemEntry(PC, Def);
        if (!Tool || Tool->Count <= 0) return false;
        auto ToolGuid = Tool->ItemGuid;
        SetFire(false); SetADS(false);
        Equip(*Tool);
        if (!Pawn->CurrentWeapon || Pawn->CurrentWeapon->ItemEntryGuid != ToolGuid) return false;
        auto Focus=Loc;
        if (Def == Wall)
        {
            FVector Center{},Half{};float Height=0.f;
            if (BuildingCell(Center,Half,Height)) Focus=BotTactics::WallFocus(Loc,Yaw,Pawn->K2_GetActorLocation(),std::max(Half.X,Half.Y));
        }
        Focus.Z=Pawn->K2_GetActorLocation().Z+30.f;
        if(FightBuild.Active())
        {
            auto D=FightPlan.Top-FightPlan.Entry;float L=std::max(1.f,std::hypot(D.X,D.Y));
            Focus=Pawn->K2_GetActorLocation()+FVector(D.X/L,D.Y/L,0)*700.f+FVector(0,0,30.f);
        }
        Look(Focus);
        if (!CommittingBuild)
        {
            QueuedBuild.Set(Def); QueuedBuildLocation = Loc; QueuedBuildYaw = Yaw;
            QueuedBuildOrigin = Pawn->K2_GetActorLocation(); QueuedBuildFocus=Focus;
            if(!FightBuild.Active()) StopMove();
            QueuedBox = BoxActive;
            bool Pressure=Def==Wall && !BoxActive && (Syndicate::Now()-CoverHitAt<.6f ||
                Syndicate::Now()-LastDamageTime<.9f || Syndicate::Now()-LastShotCueAt<.9f);
            bool HeightFight=FightBuild.Active() || HighGroundStage || (RampActive && Syndicate::Now()<HighGroundUntil);
            BuildPlaceAt = Syndicate::Now() + BotDifficulty::BuildPause(PC,Syndicate::Between(HeightFight ? .005f : (Pressure ? .025f : .08f),HeightFight ? .012f : (Pressure ? .045f : .14f)));
            BuildingUntil = BuildPlaceAt + (HighGroundStage || Syndicate::Now()<HighGroundUntil ? .02f : .1f);
            BuildingVisual = true;
            return true; // Accepted request; the actual native placement is delayed.
        }
        // Native CantBuild already validated the exact grid transform. A
        // second generic spawn-overlap rejection rejects a stair under its
        // builder. Spawn at that validated position; native collision remains.
        auto Actor = SpawnActor<ABuildingSMActor>(Loc, Rotation, PC, Class,
                                                  ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Actor)
            return false;
        Actor->bPlayerPlaced = true;
        Actor->TeamIndex = PlayerState->TeamIndex;
        Actor->Team = static_cast<EFortTeam>(PlayerState->TeamIndex);
        if (!FortInventory::RemoveGuid(PC, G, 10))
        {
            Actor->K2_DestroyActor();
            return false;
        }
        Actor->InitializeKismetSpawnedBuildingActor(Actor, nullptr, true);
        for (auto Ref : Replace)
            if (auto Existing = Ref.Get()) Existing->K2_DestroyActor();
        Actor->ForceNetUpdate();
        ObserveBuild(Actor);
        LastBuilt.Set(Actor);
        if(Def==Wall && !BoxActive && !HighGroundStage && !FightBuild.Active())
        {
            HeldCover.Set(Actor);HeldCoverLocation=Loc;HeldCoverYaw=Yaw;
            HeldCoverHealth=Actor->GetHealth();HoldCoverUntil=Syndicate::Now()+2.f;
        }
        std::erase_if(OwnedBuilds, [](const auto &Ref){ return !Ref.Get(); });
        if (OwnedBuilds.size()>=64) OwnedBuilds.erase(OwnedBuilds.begin());
        OwnedBuilds.emplace_back(Actor);
        return true;
    }
    bool ProcessBuild()
    {
        auto Def = QueuedBuild.Get();
        if (!Def) { QueuedBuild.Clear(); return false; }
        float Now = Syndicate::Now();
        Look(QueuedBuildFocus);
        if (Now < BuildPlaceAt) return true;
        auto Here = Pawn->K2_GetActorLocation();
        auto Desired = UKismetMathLibrary::FindLookAtRotation(Here,QueuedBuildFocus);
        bool Facing = Def==Floor || Def==Roof || std::abs(std::remainder(Desired.Yaw-Pawn->K2_GetActorRotation().Yaw,360.f)) <= (FightBuild.Active() ? 65.f : 35.f);
        if ((!Facing && Now >= BuildPlaceAt+(FightBuild.Active() ? .45f : .9f)) || Syndicate::Distance2D(Here,QueuedBuildOrigin) > 180.f)
        {
            QueuedBuild.Clear();RampActive=false;BuildingUntil=Now;
            if(Now<HighGroundUntil)
            { ++HeightBuildRejects;HighGroundStage=0;HighGroundUntil=0.f;CornerBridgeReady=false;NextRamp=Now+.3f; }
            return false;
        }
        if (!Facing) { BuildingUntil=Now+0.1f; return true; }
        auto Loc = QueuedBuildLocation; float Yaw = QueuedBuildYaw;
        bool WasBox = QueuedBox;
        QueuedBuild.Clear();
        CommittingBuild = true;
        bool Placed = Build(Def, Loc, Yaw); // Revalidate placement and real materials after the reaction interval.
        CommittingBuild = false;
        if(!Placed && (Def==Stair || (Def==Floor && HighGroundStage==1 && CornerBridgeReady)))
        { ++HeightBuildRejects;HighGroundStage=0;HighGroundUntil=0.f;CornerBridgeReady=false;NextRamp=Now+.5f; }
        if(Placed && Def==Floor && HighGroundStage==1 && CornerBridgeReady && Now<HighGroundUntil)
            RequestJump(); // Turn only after the connecting floor has actually passed native placement and exists.
        if (Def == Stair)
        {
            if (Placed && RampActive) RampActor.Set(LastBuilt.Get());
            else if (!Placed) RampActive = false;
        }
        // Turning waits already elapsed before this native commit. Carrying
        // their last 100 ms into the next paid combat piece adds a false pause.
        if(Placed && (FightBuild.Active() || (Def==Wall && !WasBox &&
           (Now-LastDamageTime<.9f || Now-LastShotCueAt<.9f)))) BuildingUntil=Now;
        if (Placed && WasBox && BoxPiece>0 && BoxPiece<=6)
        { BoxWalls[BoxPiece-1]=LastBuilt; BoxWallHealth[BoxPiece-1]=LastBuilt.Get()->GetHealth(); }
        if (!Placed && WasBox && BoxPiece > 0) --BoxPiece;
        return true;
    }
    bool BuildingCell(FVector &Center, FVector &Half, float &Height)
    {
        UBuildingStructuralSupportSystem *Grid = nullptr;
        UFortKismetLibrary::GetBuildingStructuralSupportSystem(UWorld::GetWorld(), &Grid);
        if (!Grid)
            return false;
        auto Feet = Pawn->K2_GetActorLocation();
        if (Pawn->CapsuleComponent)
            Feet.Z -= Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight();
        if(Syndicate::Now()<BuildLevelHintUntil && std::abs(Feet.Z-BuildLevelHintZ)<80.f &&
           Syndicate::Distance2D(Feet,BuildLevelHintPoint)<180.f)
            Feet=BuildLevelHintPoint; // Query the next connected upper cell beyond the crest; never move the pawn to it.
        auto Cell = Grid->K2_GetGridBoxFromWorldLoc(Feet);
        if (!Cell.IsValid || !Syndicate::Finite(Cell.min_0) || !Syndicate::Finite(Cell.max_0))
            return false;
        Half = (Cell.max_0 - Cell.min_0) * 0.5f;
        Height = Cell.max_0.Z - Cell.min_0.Z;
        if (Half.X <= 0 || Half.Y <= 0 || Height <= 0)
            return false;
        Center = FVector((Cell.min_0.X + Cell.max_0.X) * 0.5f, (Cell.min_0.Y + Cell.max_0.Y) * 0.5f, Cell.min_0.Z);
        if(!Syndicate::Finite(Half) || !std::isfinite(Height) || !Syndicate::Finite(Center)) return false;
        // Never change a returned grid box into a guessed cell. An unreachable
        // level is a routing failure, not permission to make floating pieces.
        if(Center.Z>Feet.Z+80.f || Center.Z<Feet.Z-Height) return false;
        return true;
    }
    void DefensiveBuild(bool Box = false)
    {
        if(!Box && !BotDifficulty::Allow(PC,BotDifficulty::DefensiveBuilding,Syndicate::Now()))return;
        float N = Syndicate::Now();
        if (N < NextBuild)
            return;
        NextBuild = N + (Box?.10f:.15f);
        auto Here = Pawn->K2_GetActorLocation();
        if (Box && !BoxActive)
        {
            if (!BuildingCell(BoxCenter, BoxHalf, BoxHeight))
                return;
            BoxPiece = BoxAttempts = 0;
            for (int I=0;I<6;++I) { BoxWalls[I].Clear(); BoxWallHealth[I]=0.f; }
            LeavingBox=false;
            BoxActive = true;
        }
        if (Box)
        {
            const FVector Offsets[] = {FVector(BoxHalf.X, 0, 0), FVector(0, BoxHalf.Y, 0), FVector(-BoxHalf.X, 0, 0),
                                       FVector(0, -BoxHalf.Y, 0), FVector(0, 0, BoxHeight), FVector(0,0,BoxHeight)};
            if (BoxPiece<6)
            {
                if(ReuseBoxPiece(BoxPiece,BoxCenter+Offsets[BoxPiece],BoxPiece>=4?0.f:static_cast<float>(BoxPiece*90+90)))
                {++BoxPiece;return;}
                if (Build(BoxPiece==5 ? Roof : (BoxPiece==4 ? Floor : Wall), BoxCenter + Offsets[BoxPiece],
                          BoxPiece >= 4 ? 0.f : static_cast<float>(BoxPiece * 90 + 90)))
                    ++BoxPiece;
                if (++BoxAttempts >= 18)
                    BoxActive = false; // A blocked build cannot hold a wounded bot forever.
            }
            return;
        }
        auto P = Enemy.Get();
        FVector DirectionPoint=P && PC->LineOfSightTo(P,Pawn->K2_GetActorLocation(),true) ? P->K2_GetActorLocation() : LastSeen;
        if (!P && (N-LastDamageTime>3.f || N-LastSeenTime>3.f)) return;
        auto D = DirectionPoint - Here;
        if (!Syndicate::Finite(D) || std::hypot(D.X,D.Y)<50.f) return;
        float Yaw = std::round(std::atan2(D.Y, D.X) * 180.f / 3.14159265f / 90.f) * 90.f;
        FVector Center{}, Half{};
        float Height = 0;
        if (!BuildingCell(Center, Half, Height))
            return;
        // Taking a hit requests cover; a separate combat choice requests a
        // real ramp and owns the climb/jump sequence.
        auto Offset = FVector(std::cos(Yaw*3.14159265f/180.f)*Half.X,
                              std::sin(Yaw*3.14159265f/180.f)*Half.Y,0);
        auto Location = Center+Offset;
        auto Front=BotTactics::WallFocus(Location,Yaw+90.f,Here,std::max(Half.X,Half.Y));
        if ((Front.X-Here.X)*D.X+(Front.Y-Here.Y)*D.Y <= 0.f)
        {
            // The nearest grid boundary can be behind a builder near the front edge.
            // Move one complete native wall cell toward the observed shot instead.
            Location=Location+Offset*2.f;
            Front=BotTactics::WallFocus(Location,Yaw+90.f,Here,std::max(Half.X,Half.Y));
            if ((Front.X-Here.X)*D.X+(Front.Y-Here.Y)*D.Y <= 0.f) return;
        }
        Build(Wall, Location, std::remainder(Yaw+90.f,360.f)); // Native wall spans local X; its normal is local Y.
    }
    bool MaintainCover(bool AllowHeight=true)
    {
        float N=Syndicate::Now();
        if(Pawn->bIsInAnyStorm || !HeldCover.Pointer || Syndicate::Distance2D(Pawn->K2_GetActorLocation(),HeldCoverLocation)>700.f || MaterialCount()<10)
            return false;
        bool HeightIntent=AllowHeight && !BoxActive && Enemy.Get() && CanFight() && N-LastSeenTime<8.f &&
            ((N<HighGroundUntil && (HighGroundStage || RampActive)) ||
             (N>=NextRamp && N>=HeightPeekUntil && MaterialCount()>=40 &&
              (LastSeen.Z>Pawn->K2_GetActorLocation().Z+120.f ||
               (Syndicate::Distance2D(Pawn->K2_GetActorLocation(),LastSeen)<2500.f && LastSeen.Z>Pawn->K2_GetActorLocation().Z-768.f) ||
              ((N-LastDamageTime<3.f || N-LastShotCueAt<1.f) &&
               LastSeen.Z>Pawn->K2_GetActorLocation().Z-384.f &&
               Syndicate::Distance2D(Pawn->K2_GetActorLocation(),LastSeen)<2500.f))));
        auto WallActor=HeldCover.Get();
        bool Gone=!WallActor || WallActor->bDestroyed || WallActor->IsActorBeingDestroyed() || WallActor->GetHealth()<=0.f;
        if(!Gone)
        {
            float H=WallActor->GetHealth();
            if(H+.5f<HeldCoverHealth) { CoverHitAt=N;HoldCoverUntil=N+2.f; }
            HeldCoverHealth=H;
            if(N-CoverHitAt<=.35f && TryReloadPeek(WallActor)) return true;
            if(HeightIntent) return false; // The covered ascent owns the next piece instead of holding a lower wall forever.
            if(N-CoverHitAt>.35f) return false; // Resume shooting/movement when pressure stops.
            SetFire(false);SetADS(false);StopMove();
            if(auto Tool=FortInventory::FindItemEntry(PC,Wall)) Equip(*Tool);
            Look(HeldCoverLocation+FVector(0,0,60.f));return true;
        }
        if(HeldCoverHealth>0.f) { HeldCoverHealth=0.f; CoverHitAt=N;HoldCoverUntil=N+2.f; } // First observed destruction also catches a late one-shot break.
        if(HeightIntent) return false;
        if(N>HoldCoverUntil) return false;
        CoverHitAt=N;HoldCoverUntil=N+2.f;
        if(QueuedBuild.Get()) return true;
        NextBuild=N;
        if(Build(Wall,HeldCoverLocation,HeldCoverYaw)) { NextBuild=N+.18f;return true; }
        HeldCover.Clear();return false; // Native rejection is not permission to spawn an unsupported/restricted wall.
    }
    FVector AimOrigin()
    {
        FVector Eyes{}; FRotator Rotation{};
        Pawn->GetActorEyesViewPoint(&Eyes, &Rotation);
        return Eyes;
    }
    void ClearFinishedEnvironment()
    {
        auto Finished = [](const auto &Ref)
        {
            if (!Ref.Pointer) return false;
            auto Target = Ref.Get();
            return !Target || Target->bDestroyed || Target->IsActorBeingDestroyed() || Target->GetHealth() <= 0.f;
        };
        if (Finished(BreachTarget) || Finished(HarvestTarget))
        {
            SetFire(false); StopMove();
            if (Finished(BreachTarget)) BreachTarget.Clear();
            if (Finished(HarvestTarget)) HarvestTarget.Clear();
            NextPath = NextLootSearch = 0.f;
            if (!BreachTarget.Get() && !HarvestTarget.Get() && HasGun()) SimpleSwitchToWeapon();
        }
    }
    bool RampClearance(const BotTactics::RampPlan &Plan,const FVector &Half,float Height)
    {
        float Facing=(Plan.ActorYaw-90.f)*3.14159265f/180.f;
        FVector Direction(std::cos(Facing),std::sin(Facing),0),Side(-Direction.Y,Direction.X,0);
        float Length=(std::abs(Direction.X)>.5f ? Half.X : Half.Y)*2.f;
        float Capsule=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f;
        TArray<AActor*> Ignored;Ignored.Add(Pawn);
        bool Clear=true;
        auto EntryDelta=Plan.Entry-Plan.Root;
        float EntryFraction=1.f+(EntryDelta.X*Direction.X+EntryDelta.Y*Direction.Y)/Length;
        float Across=EntryDelta.X*Side.X+EntryDelta.Y*Side.Y;
        float Begin=std::clamp(EntryFraction,.2f,.8f);
        for(float Fraction:{Begin,std::max(Begin,.55f),std::max(Begin,.85f)})
        {
            auto Surface=Plan.Root-Direction*(Length*(1.f-Fraction))+
                Side*(Across*(1.f-Fraction)/std::max(.01f,1.f-EntryFraction))+FVector(0,0,Height*Fraction+25.f);
            FHitResult Hit{};
            bool Blocked=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),Surface,
                Surface+FVector(0,0,Capsule*2.f),BotPhysics::PawnProfile(),false,
                Ignored,EDrawDebugTrace::None,&Hit,true,{},{},0);
            if(Blocked || Hit.bStartPenetrating) { Clear=false;break; }
        }
        Ignored.Free();return Clear; // A native-approved footprint is insufficient if the walking surface is buried in a cliff/ceiling.
    }
    bool HeightHeadroom(const FVector &Feet)
    {
        float Capsule=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f;
        TArray<AActor*> Ignored;Ignored.Add(Pawn);FHitResult Hit{};
        bool Blocked=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
            Feet+FVector(0,0,Capsule*2.f+5.f),Feet+FVector(0,0,800.f),BotPhysics::PawnProfile(),false,
            Ignored,EDrawDebugTrace::None,&Hit,true,{},{},0);
        Ignored.Free();return !Blocked && !Hit.bStartPenetrating;
    }
    bool TryHeightExit()
    {
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        auto Feet=Here-FVector(0,0,Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f);
        if(N<NextHeightExit || !Enemy.Get() || !CanFight() || HeightHeadroom(Feet)) return false;
        NextHeightExit=N+3.f;
        auto D=LastSeen-Here;float Length=std::max(1.f,std::hypot(D.X,D.Y));D=D*(1.f/Length);D.Z=0.f;
        FVector Chosen{};float Best=100000.f;bool Found=false;
        for(float Range:{600.f,1200.f})
            for(float Degrees:{0.f,90.f,-90.f,180.f})
            {
                float R=Degrees*3.14159265f/180.f;FVector Direction(D.X*std::cos(R)-D.Y*std::sin(R),D.X*std::sin(R)+D.Y*std::cos(R),0);
                FVector Goal{},Sample=Here+Direction*Range;
                if(!NativeNavigation::Project(Sample,Goal) || !Syndicate::Finite(Goal) ||
                   Syndicate::Distance2D(Sample,Goal)>200.f || std::abs(Goal.Z-Feet.Z)>400.f ||
                   !AllowedDestination(Goal) || RouteMemory.Blocked(Goal,N) || !HeightHeadroom(Goal)) continue;
                float Score=Syndicate::Distance2D(Here,Goal)+Syndicate::Distance2D(Goal,LastSeen)*.3f;
                if(Score<Best) {Best=Score;Chosen=Goal;Found=true;}
            }
        if(!Found) return false;
        HighGroundStage=0;HighGroundUntil=0.f;RampActive=false;CornerCellReady=false;
        HarvestTarget.Clear();BreachTarget.Clear();GatheringBurst=false;
        SetFire(false);SetADS(false);StopMove();NextPath=0.f;
        if(!Move(Chosen,100.f,true,true)) return false;
        HeightExitGoal=Chosen;HeightExitUntil=N+8.f;return true;
    }
    bool HeightExitAction()
    {
        float N=Syndicate::Now();if(N>=HeightExitUntil) return false;
        if(!Enemy.Get() || !CanFight() || N-LastSeenTime>12.f)
        {HeightExitUntil=0.f;return false;}
        auto Here=Pawn->K2_GetActorLocation();
        if(BotTactics::Reached(Here,HeightExitGoal,120.f))
        {HeightExitUntil=0.f;NextRamp=0.f;StopMove();return false;}
        SetFire(false);SetADS(false);SimpleSwitchToWeapon();
        if(N-LastProgress>1.5f && ReachEnvironment(HeightExitGoal)) return true;
        if(Move(HeightExitGoal,100.f,true,true)) return true;
        RouteMemory.Fail(HeightExitGoal,N);HeightExitUntil=0.f;return false;
    }
    bool TryTerrainDetour(const FVector &Goal)
    {
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        if(BotState!=EBotState::MovingToSafeZone || N<NextTerrainDetour) return false;
        NextTerrainDetour=N+1.5f;
        float Current=Syndicate::Distance2D(Here,Goal);
        float Best=Current+650.f;FVector Chosen{};bool Found=false;
        for(int Attempt=0;Attempt<8;++Attempt)
        {
            FVector Candidate{};
            if(!NativeNavigation::RandomReachable(Here,Attempt<4 ? 1600.f : 2800.f,Candidate) || !Syndicate::Finite(Candidate) ||
               Syndicate::Distance2D(Here,Candidate)<350.f || Syndicate::Distance2D(Here,Candidate)>2900.f || RouteMemory.Blocked(Candidate,N)) continue;
            float Distance=Syndicate::Distance2D(Candidate,Goal);
            bool ExposedContour=Pawn->bIsInAnyStorm && RotationActive &&
                Syndicate::Distance2D(Candidate,RotationGoal)<=Syndicate::Distance2D(Here,RotationGoal)+120.f;
            if(!AllowedDestination(Candidate) && !ExposedContour) continue;
            float Score=Distance+std::max(0.f,Candidate.Z-Here.Z)*.5f;
            if(Score<Best) { Best=Score;Chosen=Candidate;Found=true; }
        }
        if(!Found) return false;
        StopMove();NextPath=0.f;
        if(!Move(Chosen,150.f,true,true)) return false;
        TerrainDetourGoal=Chosen;TerrainDetourUntil=N+8.f;
        return true; // Native reachable terrain gets an attempt before spending materials on a mountain.
    }
    bool StartRamp(const FVector &Toward,bool NearCell=false)
    {
        float N = Syndicate::Now();
        if (RampActive || N < NextRamp || MaterialCount() < 10 || !Syndicate::Finite(Toward)) return false;
        NextRamp=N+0.75f; // Bound failed placement probes without a ten-second combat lockout.
        FVector Center{}, Half{}; float Height = 0;
        if(HighGroundStage==4 && CornerCellReady &&
           BotTactics::CornerCellContains(CornerCellCenter,CornerCellHalf,CornerCellHeight,Pawn->K2_GetActorLocation(),
               Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f))
        { Center=CornerCellCenter;Half=CornerCellHalf;Height=CornerCellHeight; }
        else if (!BuildingCell(Center, Half, Height)) return false;
        float HalfHeight = Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f;
        auto Plan = BotTactics::ForwardRamp(Center,Half,Height,HalfHeight,Pawn->K2_GetActorLocation(),Toward,NearCell);
        if(Plan.Top.Z<=Pawn->K2_GetActorLocation().Z+45.f) return false; // A height/obstacle ascent must not create a lower stair behind a pawn already above this cell.
        if(RouteMemory.Blocked(Plan.Entry,N) || RouteMemory.Blocked(Plan.Top,N))
        {
            if(!NearCell) return false;
            Plan=BotTactics::ForwardRamp(Center,Half,Height,HalfHeight,Pawn->K2_GetActorLocation(),Toward);
            NearCell=false;
            if(RouteMemory.Blocked(Plan.Entry,N) || RouteMemory.Blocked(Plan.Top,N)) return false;
        }
        bool SidePlaced=false;
        auto PlaceRamp=[&](const BotTactics::RampPlan &Candidate)
        { return (Pawn->bIsInAnyStorm || AllowedDestination(Candidate.Top)) &&
                 RampClearance(Candidate,Half,Height) && Build(Stair,Candidate.Root,Candidate.ActorYaw); };
        if (!PlaceRamp(Plan))
        {
            if(!NearCell) return false;
            // Native placement may reject the near-cell footprint. Try the
            // ordinary connected forward cell once; never bypass support/collision checks.
            Plan=BotTactics::ForwardRamp(Center,Half,Height,HalfHeight,Pawn->K2_GetActorLocation(),Toward);
            bool Placed=!RouteMemory.Blocked(Plan.Entry,N) && !RouteMemory.Blocked(Plan.Top,N) &&
                PlaceRamp(Plan);
            // A cliff can reject both forward footprints. Keep the full ascent destination,
            // but try one connected quarter-turn on each side before abandoning it.
            if(!Placed && N<AscentUntil && AscentLateralSteps<2)
            {
                if(BotState==EBotState::MovingToSafeZone && RotationActive && !NativePlatform(Center))
                {
                    // A quarter-turn beside a cliff needs a real connecting
                    // platform. Finish that paid floor before requesting the
                    // side stair, rather than stepping into its unsupported gap.
                    if(MaterialCount()<20 || !Build(Floor,Center,0.f)) return false;
                    NextRamp=N+.05f;return true;
                }
                auto Here=Pawn->K2_GetActorLocation(),D=Toward-Here;float L=std::max(1.f,std::hypot(D.X,D.Y));
                for(float Side:{CombatSide,-CombatSide})
                {
                    auto SideGoal=Here+FVector(-D.Y/L*Side,D.X/L*Side,0)*512.f;
                    auto SidePlan=BotTactics::ForwardRamp(Center,Half,Height,HalfHeight,Here,SideGoal,true);
                    bool Safe=AllowedDestination(SidePlan.Top);
                    // Going around a cliff may briefly be lateral to the circle. Bound
                    // this to two paid side stairs and at most 600 cm extra zone distance.
                    bool ZoneDetour=BotState==EBotState::MovingToSafeZone && RotationActive &&
                        Syndicate::Distance2D(SidePlan.Top,RotationGoal)<=Syndicate::Distance2D(Here,RotationGoal)+600.f;
                    if(RouteMemory.Blocked(SidePlan.Entry,N) || RouteMemory.Blocked(SidePlan.Top,N) || (!Safe && !ZoneDetour)) continue;
                    if(PlaceRamp(SidePlan))
                    { Plan=SidePlan;Placed=SidePlaced=true;++MountainSideSteps;++AscentLateralSteps;break; }
                }
            }
            if(!Placed) return false;
        }
        if(!SidePlaced) AscentLateralSteps=0;
        RampUpperZ=Plan.Root.Z+Height;
        auto RiseDirection=Plan.Top-Plan.Entry;float RiseLength=std::max(1.f,std::hypot(RiseDirection.X,RiseDirection.Y));
        RampUpperCell=Plan.Root+FVector(RiseDirection.X/RiseLength,RiseDirection.Y/RiseLength,0)*8.f;
        RampUpperCell.Z=RampUpperZ+2.f;
        RampEntry = Plan.Entry; RampGoal = Plan.Top; RampExitFeet=Plan.Root.Z+Height+1.f; RampEntering = true;
        RampActor = LastBuilt;
        RampActive = true;
        RampFloorSampled=false;
        NextRamp=N+(N<HighGroundUntil ? .2f : (Pawn->bIsInAnyStorm ? 2.f : 4.f));
        RampDeadline = N + 16.f;
        RampProgressAt=N; RampBestDistance=(Pawn->K2_GetActorLocation()-RampEntry).Magnitude();
        StopMove();
        return true;
    }
    bool EnvironmentAction()
    {
        if (QueuedBuild.Get()) return true; // Await each real piece before climbing or extending it.
        ClearFinishedEnvironment();
        float N = Syndicate::Now();
        auto Here = Pawn->K2_GetActorLocation();
        if(HighGroundStage)
        {
            if(N>=HighGroundUntil || !Enemy.Get() || N-LastSeenTime>8.f || MaterialCount()<10)
            { HighGroundStage=0; HighGroundUntil=0.f; return false; }
            StopMove(); SetFire(false); SetADS(false);
            FVector Center{},Half{}; float Height=0.f;
            float CornerCapsule=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f;
            if(CornerCellReady && !BotTactics::CornerCellContains(CornerCellCenter,CornerCellHalf,CornerCellHeight,Here,CornerCapsule))
            { CornerCellReady=false;CornerBridgeReady=HighGroundSteps==0;HighGroundStage=1; } // A jump/fall into another level invalidates the captured group; query the real new cell before any further piece.
            if(!CornerCellReady)
            {
                if(!BuildingCell(CornerCellCenter,CornerCellHalf,CornerCellHeight)) { HighGroundStage=0; return false; }
                CornerCellReady=true;
            }
            Center=CornerCellCenter;Half=CornerCellHalf;Height=CornerCellHeight;
            if(!CornerBridgeReady)
            {
                FHitResult Support{};TArray<AActor*> Ignored;Ignored.Add(Pawn);
                bool Supported=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
                    Center+FVector(0,0,70.f),Center-FVector(0,0,40.f),BotPhysics::PawnProfile(),false,
                    Ignored,EDrawDebugTrace::None,&Support,true,{},{},0);
                Ignored.Free();
                if(Supported && !Support.bStartPenetrating && Support.ImpactNormal.Z>=.7f &&
                   !Syndicate::As<APawn>(Support.Actor.Get()) && std::abs(Support.ImpactPoint.Z-Center.Z)<40.f)
                    CornerBridgeReady=true;
                else
                {
                    if(Build(Floor,Center,0.f)) { CornerBridgeReady=true;return true; }
                    ++HeightBuildRejects;HighGroundStage=0;HighGroundUntil=0.f;CornerCellReady=false;NextRamp=N+.5f;
                    return false; // No safe connecting floor: do not jump or turn into empty space.
                }
            }
            auto Plan=BotTactics::ForwardRamp(Center,Half,Height,CornerCapsule,Here,HighGroundToward,true);
            if(Plan.Top.Z<=Here.Z+45.f)
            { HighGroundStage=0;CornerCellReady=false;HighGroundUntil=0.f;NextRamp=N+.15f;return false; }
            auto Rise=Plan.Top-Plan.Entry;float Length=std::max(1.f,std::hypot(Rise.X,Rise.Y));
            FVector Direction(Rise.X/Length,Rise.Y/Length,0),Side(-Direction.Y,Direction.X,0);
            float Along=std::abs(Direction.X)>.5f ? Half.X : Half.Y;
            float Across=std::abs(Side.X)>.5f ? Half.X : Half.Y;
            auto StairCenter=Plan.Root-Direction*Along;
            if((LastSeen.X-StairCenter.X)*Side.X+(LastSeen.Y-StairCenter.Y)*Side.Y<0.f) Side=Side*-1.f;
            float WallYaw=std::remainder(std::atan2(Side.Y,Side.X)*180.f/3.14159265f+90.f,360.f);
            if(HighGroundStage==3 && MaterialCount()<20)
            {HighGroundStage=4;return true;} // Reserve the real last ten materials for the stair after the connecting floor and enemy-side cover.
            bool Started=false;
            if(HighGroundStage==1) Started=Build(Floor,StairCenter,0.f);
            else if(HighGroundStage==2) Started=Build(Wall,StairCenter+Side*Across,WallYaw);
            else if(HighGroundStage==3) Started=Build(Wall,Plan.Root,Plan.ActorYaw); // Reinforcement below the high end leaves the low stair entrance open.
            else { NextRamp=N; Started=StartRamp(HighGroundToward,true); }
            if(!Started)
            {
                if(HighGroundStage<4) { ++HighGroundStage; return true; } // One rejected optional piece does not cancel the next native-checked stair.
                ++HeightBuildRejects;HighGroundStage=0; CornerCellReady=false;HighGroundUntil=0.f; NextRamp=N+.5f; return false;
            }
            if(HighGroundStage==4) CornerCellReady=false;
            HighGroundStage=HighGroundStage==4 ? 0 : HighGroundStage+1;
            return true; // Floor, cover and quarter-turn stair each use normal placement and reaction time.
        }
        if (auto Target = BreachTarget.Get())
        {
            if (Target==LastBuilt.Get() || Target==RampActor.Get())
            {
                SetFire(false); BreachTarget.Clear(); StopMove(); NextPath=0.f;
                if (HasGun()) SimpleSwitchToWeapon();
                return false; // Route recovery must not dismantle its own new cover/stair.
            }
            if (auto Container = Syndicate::As<ABuildingContainer>(Target); Container && !Container->bAlreadySearched)
            {
                SetFire(false); BreachTarget.Clear(); StopMove();
                NextPath = 0.f;
                if (HasGun()) SimpleSwitchToWeapon();
                return false;
            }
            float Health = Target->GetHealth();
            if (Health + 0.5f < BreachHealth) BreachDeadline = N + 10.f;
            BreachHealth = Health;
            if (Target->bDestroyed || Target->GetHealth() <= 0.f || Target->IsActorBeingDestroyed() || N >= BreachDeadline)
            {
                if (!Target->bDestroyed) { IgnoredBreach.Set(Target); IgnoreBreachUntil = N + 20.f; }
                SetFire(false); BreachTarget.Clear(); StopMove();
                NextLootSearch = NextPath = 0.f;
                if (HasGun()) SimpleSwitchToWeapon();
                return false;
            }
            SetADS(false); EquipPickaxe();
            if (Syndicate::Distance2D(Here, BreachPoint) > 170.f)
            {
                SetFire(false);
                auto D = Here - BreachPoint; float L = std::max(1.f, std::hypot(D.X,D.Y));
                auto Approach = BreachPoint + FVector(D.X/L*140.f,D.Y/L*140.f,0);
                Approach.Z = Here.Z;
                Move(Approach, 25.f, false);
            }
            else
            {
                StopMove();
                Posture.Request(BotTactics::Posture::Harvest,N);
                auto Point=HarvestAim(Target,BreachPoint);Look(Point);
                auto Desired = UKismetMathLibrary::FindLookAtRotation(AimOrigin(), Point);
                SetFire(BotTactics::AimAligned(Desired, PC->GetControlRotation(), Pawn->K2_GetActorRotation()));
            }
            return true;
        }
        if (RampActive)
        {
            auto Ramp = RampActor.Get();
            float CurrentFeet=Here.Z-(Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f);
            if(CurrentFeet>RampUpperZ+125.f)
            {
                RampActive=false;RampEntering=false;CornerCellReady=false;HighGroundStage=0;HeightResumeCorner=false;
                BuildingVisual=false;BuildingUntil=N;StopMove();NextPath=0.f;NextRamp=N+.15f;
                if(HasGun()) SimpleSwitchToWeapon();return false; // Do not walk down to a stale lower stair while the enemy is above us.
            }
            if(Ramp && !RampFloorSampled)
            {
                RampFloorSampled=true;
                float Capsule=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f;
                FHitResult FloorHit{}; TArray<AActor *> Ignored; Ignored.Add(Pawn);
                bool FloorFound=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
                    RampEntry+FVector(0,0,400.f),RampEntry-FVector(0,0,500.f),BotPhysics::PawnProfile(),false,
                    Ignored,EDrawDebugTrace::None,&FloorHit,true,{},{},0);
                Ignored.Free();
                if(FloorFound && !FloorHit.bStartPenetrating && FloorHit.ImpactNormal.Z>=0.65f &&
                   Syndicate::Finite(FloorHit.ImpactPoint) && FloorHit.ImpactPoint.Z+Capsule<RampGoal.Z-40.f)
                    RampEntry.Z=FloorHit.ImpactPoint.Z+Capsule+2.f;
                // Enter from the actual walkable stair/terrain surface, not a buried grid estimate.
                RampBestDistance=(Here-RampEntry).Magnitude();
            }
            if(RampEntering && Here.Z>RampEntry.Z+35.f)
            {
                float Capsule=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f;
                float Feet=Here.Z-Capsule;
                FHitResult Support{};TArray<AActor*> Ignored;Ignored.Add(Pawn);
                bool Found=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
                    Here,Here-FVector(0,0,Capsule+80.f),BotPhysics::PawnProfile(),false,
                    Ignored,EDrawDebugTrace::None,&Support,true,{},{},0);
                Ignored.Free();
                if(Found && !Support.bStartPenetrating && Support.Actor.Get()==Ramp &&
                   Support.ImpactNormal.Z>=.65f &&
                   BotTactics::SupportedRampProgress(Here,RampEntry,RampGoal,Feet,Support.ImpactPoint))
                {
                    // Native floor contact proves the pawn is already partway
                    // up this stair. Do not route it downhill to enter again.
                    RampEntering=false;StopMove();NextPath=0.f;
                    RampBestDistance=(Here-RampGoal).Magnitude();RampProgressAt=N;
                    if(N<HighGroundUntil && !Pawn->bIsInAnyStorm && Enemy.Get() && CanFight() &&
                       N-LastSeenTime<1.f && N>=NextRearRetake && MaterialCount()>=40 &&
                       BotTactics::RearThreat(Here,LastSeen,RampGoal-RampEntry))
                    {
                        HighGroundToward=BotTactics::BuildContinuation(Here,LastSeen,RampGoal-RampEntry,CombatSide,true);
                        CoveredRampRush=true;RampActive=false;CornerCellReady=false;CornerBridgeReady=false;HighGroundStage=1;
                        NextRearRetake=N+1.5f;StopMove();SetFire(false);SetADS(false);
                        return true; // Actual native ramp support permits a protected turn; airborne pawns never qualify.
                    }
                }
            }
            auto ActiveGoal=RampEntering ? RampEntry : RampGoal;
            if(!Enemy.Get()) Look(ActiveGoal);
            float RampDistance=(Here-ActiveGoal).Magnitude();
            if (RampDistance+25.f<RampBestDistance)
            { RampBestDistance=RampDistance; RampProgressAt=N; RampDeadline=N+8.f; }
            if (!Ramp || Ramp->bDestroyed || N >= RampDeadline || N-RampProgressAt>(N<HighGroundUntil && Enemy.Get() ? 2.5f : (Pawn->bIsInAnyStorm ? 3.f : 6.f)))
            {
                if(N<HighGroundUntil) ++HeightClimbAborts;
                RouteMemory.Fail(RampEntry,N); RouteMemory.Fail(RampGoal,N);
                AscentUntil=HighGroundUntil=0.f; HighGroundStage=0;
                RampActive = false;CornerCellReady=false;HeightResumeCorner=false;BuildingVisual=false;BuildingUntil=N;
                StopMove();NextPath=0.f;NextRamp=N+.15f;if(HasGun()) SimpleSwitchToWeapon();
                return false;
            }
            if (RampEntering)
            {
                bool EntryReached=BotTactics::Reached(Here,RampEntry,110.f) ||
                    (Syndicate::Distance2D(Here,RampEntry)<140.f && Here.Z>=RampEntry.Z-50.f && Here.Z<RampGoal.Z);
                if (!EntryReached)
                { MoveRamp(RampEntry,35.f); return true; }
                RampEntering=false; StopMove(); NextPath=0.f;
                RampBestDistance=(Here-RampGoal).Magnitude(); RampProgressAt=N;
            }
            float Feet=Here.Z-(Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f);
            bool Crest=false;
            if(Syndicate::Distance2D(Here,RampGoal)<85.f && Feet>=RampUpperZ-60.f)
            {
                FHitResult Support{};TArray<AActor*> Ignored;Ignored.Add(Pawn);
                bool Found=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
                    Here,Here-FVector(0,0,(Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f)+50.f),
                    BotPhysics::PawnProfile(),false,Ignored,EDrawDebugTrace::None,&Support,true,{},{},0);
                Ignored.Free();
                Crest=Found && !Support.bStartPenetrating && Support.Actor.Get()==Ramp && Support.ImpactNormal.Z>=.65f &&
                    BotTactics::SupportedRampCrest(Here,RampGoal,Feet,RampUpperZ,Support.ImpactPoint);
            }
            if (Crest || (Syndicate::Distance2D(Here,RampGoal)<38.f && std::abs(Here.Z-RampGoal.Z)<55.f && Feet>=RampExitFeet))
            {
                ++StairCrests;
                if(Crest) { BuildLevelHintZ=RampUpperZ;BuildLevelHintPoint=RampUpperCell;BuildLevelHintUntil=N+3.f; }
                RampActive = false; StopMove(); if(N>=AscentUntil && N>=HighGroundUntil) RequestJump(); NextPath = 0.f;
                NextRamp=N;
                auto Toward=AscentToward-Here, Original=AscentToward-AscentAnchor;
                bool StillAhead=Syndicate::Distance2D(Here,AscentToward)>180.f && Toward.X*Original.X+Toward.Y*Original.Y>0.f;
                if(!StillAhead) AscentUntil=0.f; // Reached/passed a segment: resume its native route instead of reversing the stair chain.
                if (N<AscentUntil && ++AscentSteps<24 && MaterialCount()>=10)
                {
                    if(BotState==EBotState::MovingToSafeZone && !TerrainNeedsRamp(AscentToward))
                    {AscentUntil=0.f;TerrainMission.Clear();NextPath=0.f;return false;}
                    // Each continuation begins from the real top of the previous stair.
                    RotationProgressAt=N; AscentUntil=N+20.f;
                    if (StartRamp(AscentToward,true)) return true;
                }
                if (N<HighGroundUntil && ++HighGroundSteps<8 && MaterialCount()>=10)
                {
                    if (Enemy.Get() && N-LastSeenTime<8.f && BuildFightHeight>Here.Z+60.f && MaterialCount()>=40)
                    {
                        HighGroundToward=BotTactics::BuildContinuation(Here,LastSeen,RampGoal-RampEntry,CombatSide,CoveredRampRush);
                        CornerCellReady=false;CornerBridgeReady=false;HighGroundStage=1;
                        return true; // Commit a native floor/wall/stair corner before the next physical ascent.
                    }
                }
                if(N<AscentUntil && MaterialCount()>=10)
                { RouteMemory.Fail(RampEntry,N); NextRamp=N+0.75f; NextPath=0.f; return false; }
                if(N<HighGroundUntil)
                {
                    HighGroundUntil=0.f;HighGroundStage=0;HeightResumeCorner=false;
                    HeightPeekUntil=N+1.2f;BuildingVisual=false;BuildingUntil=N;
                    if(HasGun()) SimpleSwitchToWeapon(Syndicate::Distance2D(Here,LastSeen));
                } // Shoot after the height goal/budget, rather than pausing on each intermediate stair.
                NextRamp=N+.35f;
                return false;
            }
            MoveRamp(RampGoal,8.f);
            return true;
        }
        return false;
    }
    bool ReachEnvironment(const FVector &Goal)
    {
        if (EnvironmentAction()) return true;
        auto Here = Pawn->K2_GetActorLocation();
        float N=Syndicate::Now();
        if(N<TerrainDetourUntil && !BotTactics::Reached(Here,TerrainDetourGoal,150.f))
        {
            if(Move(TerrainDetourGoal,150.f,true,true)) return true;
            RouteMemory.Fail(TerrainDetourGoal,N);
        }
        TerrainDetourUntil=0.f;
        FVector Eyes{}; FRotator Rotation{};
        Pawn->GetActorEyesViewPoint(&Eyes, &Rotation);
        auto D = Goal - Eyes; float L = std::max(1.f, D.Magnitude());
        auto End = Eyes + D * (std::min(500.f, L) / L);
        FHitResult Hit{}; TArray<AActor *> Ignored; Ignored.Add(Pawn);
        bool Blocked = UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(), Eyes, End,
            BotPhysics::PawnProfile(), false, Ignored, EDrawDebugTrace::None, &Hit, true, {}, {}, 0);
        Ignored.Free();
        auto Building = Blocked ? Syndicate::As<ABuildingSMActor>(Hit.Actor.Get()) : nullptr;
        if (Building && BotInteraction::OpenDoor(PC,Pawn,Building,NextDoor))
        { BeginDoorPass(Building); NextPath=0.f; return true; }
        if (Building && Building->bPlayerPlaced && Building->GetOwner()==PC)
        {
            // A native arch has no door interaction to toggle or wait for.
            if(!Building->GetName().contains("Archway") && QueueEdit(Building,true)) return true;
            return false; // A rejected edit is not permission to destroy our cover.
        }
        if (Building && (Building==LastBuilt.Get() || Building==RampActor.Get())) return false;
        // The loot container is an interaction target, never a route obstacle.
        // Do not destroy a neighboring unopened chest/ammo box either.
        if (auto Container = Syndicate::As<ABuildingContainer>(Building); Container && !Container->bAlreadySearched)
            return false;
        if (!Building || Building->bDestroyed || !Building->bCanBeDamaged || Building->GetHealth() <= 0.f ||
            (IgnoredBreach.Get() == Building && Syndicate::Now() < IgnoreBreachUntil) ||
            Hit.ImpactPoint.Z - Eyes.Z > 170.f)
        {
            // Zone waypoints intentionally keep the current Z. Sample actual
            // terrain ahead so a mountain can trigger a stair even with a flat
            // destination, instead of repeatedly walking into the same slope.
            auto ClimbGoal=BotState==EBotState::MovingToSafeZone && RotationActive &&
                Syndicate::Finite(RotationGoal) && Syndicate::Distance2D(Here,RotationGoal)>500.f ? RotationGoal : Goal;
            float Distance = Syndicate::Distance2D(Here, ClimbGoal);
            if (Syndicate::Now() >= NextRamp && Distance > 200.f)
            {
                auto Direction = ClimbGoal - Here; Direction.Z = 0.f;
                auto Ahead = Here + Direction * (500.f / Distance);
                float Feet = Here.Z - (Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 75.f);
                FHitResult Terrain{}; TArray<AActor *> TerrainIgnored; TerrainIgnored.Add(Pawn);
                bool Found = UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
                    FVector(Ahead.X,Ahead.Y,Feet+750.f), FVector(Ahead.X,Ahead.Y,Feet-200.f),
                    BotPhysics::PawnProfile(), false, TerrainIgnored, EDrawDebugTrace::None, &Terrain, true, {}, {}, 0);
                TerrainIgnored.Free();
                if (Found && !Terrain.bStartPenetrating && !Syndicate::As<APawn>(Terrain.Actor.Get()) &&
                    Syndicate::Finite(Terrain.ImpactPoint) && Terrain.ImpactPoint.Z > Feet+80.f &&
                    Terrain.ImpactPoint.Z < Feet+700.f &&
                    (Terrain.ImpactNormal.Z < 0.7f || Terrain.ImpactPoint.Z > Feet+180.f))
                {
                    if(TryTerrainDetour(ClimbGoal)) return true;
                    if(MaterialCount()<10) return Harvest(); // Gather only the real first-stair cost when no walking contour is found.
                    if(HarvestTarget.Get()) { HarvestTarget.Clear();SetFire(false);StopMove(); }
                    AscentToward=ClimbGoal; AscentAnchor=Here; AscentUntil=Syndicate::Now()+50.f; AscentSteps=0;
                    if (StartRamp(ClimbGoal,true)) return true;
                }
            }
            // Native path following gets the first attempt at existing stairs.
            // After it stalls, breach a reachable obstruction before building;
            // a high ceiling needs physical elevation before a pickaxe can hit.
            if (Goal.Z - Here.Z > 180.f && Syndicate::Distance2D(Here, Goal) < 700.f)
            {
                if (StartRamp(Goal)) return EnvironmentAction();
                if (MaterialCount() < 10 && Harvest()) return true;
            }
            return false;
        }
        BreachTarget.Set(Building); BreachPoint = Hit.ImpactPoint;
        BreachDeadline = Syndicate::Now() + 10.f; BreachHealth = Building->GetHealth();
        return EnvironmentAction();
    }
    FFortItemEntry HealItem()
    {
        FFortItemEntry Empty{};
        auto I = FortInventory::Get(PC);
        if (!I)
            return Empty;
        float H = Pawn->GetHealth(), S = Pawn->GetShield();
        // Actual health items take precedence over shields whenever they can improve HP.
        if(H<100.f)
            for(auto E:I->Inventory.ReplicatedEntries)
                if(E.ItemDefinition && E.Count>0)
                {
                    auto N=E.ItemDefinition->GetName();
                    std::transform(N.begin(),N.end(),N.begin(),[](unsigned char C){return static_cast<char>(std::tolower(C));});
                    if((H<75.f && N.contains("bandage")) || N.contains("medkit") || N.contains("slurp") || N.contains("chug")) return E;
                }
        // Use minis first below their native 50 cap, preserving large shields for the remaining 50.
        if (S<50.f)
            for(auto E:I->Inventory.ReplicatedEntries)
                if(E.ItemDefinition && E.Count>0)
                {
                    auto N=E.ItemDefinition->GetName();
                    std::transform(N.begin(),N.end(),N.begin(),[](unsigned char C){return static_cast<char>(std::tolower(C));});
                    if(N.contains("shield") && (N.contains("small") || N.contains("mini"))) return E;
                }
        for (auto E : I->Inventory.ReplicatedEntries)
            if (E.ItemDefinition && E.Count > 0)
            {
                auto N = E.ItemDefinition->GetName();
                std::transform(N.begin(), N.end(), N.begin(),
                               [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
                bool Small = N.contains("small") || N.contains("mini");
                if ((H < 75 && N.contains("bandage")) || (H < 100 && N.contains("medkit")) ||
                    (S < (Small ? 50.f : 100.f) && N.contains("shield")) ||
                    ((H < 100 || S < 100) && (N.contains("slurp") || N.contains("chug"))))
                    return E;
            }
        return Empty;
    }
    bool QueueEdit(ABuildingSMActor *Building,bool Passage)
    {
        if (WallEdit.Active()) return true;
        if (Syndicate::Now()<NextEdit) return false;
        SetFire(false); SetADS(false); StopMove();
        if (!WallEdit.Begin(PC,Pawn,PlayerState,Building,Passage))
        { NextEdit=Syndicate::Now()+1.f; return false; }
        NextEdit=Syndicate::Now()+BotDifficulty::Delay(PC,BotDifficulty::EditSpeed,2.f);
        return true;
    }
    void CancelHeal()
    {
        SetFire(false);
        BotInteraction::CancelConsumable(PlayerState,Pawn,HealGuid);
        HealingStarted=false; SuppressHealUntil=Syndicate::Now()+3.f;
        NextHealUse=Syndicate::Now()+0.3f;
        if (HasGun()) SimpleSwitchToWeapon(); else EquipPickaxe();
    }
    bool BoxDamaged()
    {
        bool Damaged=false;
        for (int I=0;I<6;++I)
        {
            auto B=BoxWalls[I].Get();
            if (BoxWalls[I].Pointer && (!B || B->bDestroyed || B->GetHealth()+0.5f<BoxWallHealth[I])) Damaged=true;
            if (B) BoxWallHealth[I]=B->GetHealth();
        }
        return Damaged;
    }
    bool LeaveHealingBox()
    {
        auto Here=Pawn->K2_GetActorLocation();
        if (!LeavingBox)
        {
            auto W=UWorld::GetWorld(); auto GS=W ? Syndicate::As<AFortGameStateAthena>(W->GameState) : nullptr;
            auto Zone=GS ? GS->SafeZoneIndicator : nullptr;
            auto Target=Enemy.Get();
            auto Toward=Target ? LastSeen : (Zone ? Zone->NextCenter : Here+FVector(1000,0,0));
            ABuildingSMActor *Best=nullptr; float Score=-1e30f;
            for(int I=0;I<4;++I)
                if (auto W=BoxWalls[I].Get(); W && !W->bDestroyed && W->GetOwner()==PC)
                {
                    auto D=W->K2_GetActorLocation()-Here, Want=Toward-Here;
                    float Value=D.X*Want.X+D.Y*Want.Y;
                    if(Value>Score) { Score=Value; Best=W; }
                }
            if (!Best) { BoxActive=false; return false; }
            auto D=Best->K2_GetActorLocation()-Here; float L=std::max(1.f,std::hypot(D.X,D.Y));
            BoxExitGoal=Here+FVector(D.X/L,D.Y/L,0)*650.f;
            if (!QueueEdit(Best,true)) return false;
            LeavingBox=true; BoxExitUntil=Syndicate::Now()+7.f;
            BoxExitProgressAt=Syndicate::Now();BoxExitBest=Syndicate::Distance2D(Here,BoxExitGoal);
            return true;
        }
        if (WallEdit.Active()) return true;
        float N=Syndicate::Now(), Distance=Syndicate::Distance2D(Here,BoxExitGoal);
        if(Distance+20.f<BoxExitBest) { BoxExitBest=Distance;BoxExitProgressAt=N; }
        if (Syndicate::Distance2D(Here,BoxCenter)>std::max(BoxHalf.X,BoxHalf.Y)+100.f)
        { LeavingBox=BoxActive=false; return false; }
        if(N>=BoxExitUntil || N-BoxExitProgressAt>2.f || !Move(BoxExitGoal,80.f,false,true))
        {
            // Retire the healing task; normal native route recovery can retry
            // the opening. A blocked exit cannot retain an editing tool forever.
            LeavingBox=BoxActive=false;NextPath=NextLootSearch=0.f;
            StopMove();return false;
        }
        return true;
    }
    void Heal()
    {
        float N=Syndicate::Now();
        SetADS(false); if(!LeavingBox) StopMove(); Sprint(false);
        if(HealingStarted && !LeavingBox && (!BoxActive || BoxPiece>=6))
            Posture.Request(BotTactics::Posture::Healing,N);
        if (BoxDamaged() || N-BoxDamageAt<0.5f || N-LastDamageTime<0.35f)
        {
            CancelHeal(); BoxActive=false; BotState=EBotState::Combat;
            NextBuild=0.f; DefensiveBuild(); return;
        }
        if (BoxActive && BoxPiece<6) { DefensiveBuild(true); return; }
        if (!HealingStarted)
        {
            if (N<NextHealUse) return;
            auto E=HealItem();
            if (!E.ItemDefinition)
            {
                if (LeaveHealingBox()) return;
                BoxActive=false; BotState=EBotState::Looting;
                if (HasGun()) SimpleSwitchToWeapon(); return;
            }
            Equip(E);
            if (!Pawn->CurrentWeapon || Pawn->CurrentWeapon->ItemEntryGuid!=E.ItemGuid)
            { SuppressHealUntil=N+3.f; BotState=EBotState::Looting; return; }
            HealGuid=E.ItemGuid; HealCount=E.Count;
            HealHealth=Pawn->GetHealth(); HealShield=Pawn->GetShield();
            HealDeadline=N+18.f; HealingStarted=true; SetFire(true);
        }
        auto E=FortInventory::FindItemEntry(PC,HealGuid);
        bool Complete=!E || E->Count<HealCount ||
            (!BotInteraction::ConsumableActive(PlayerState,Pawn,HealGuid) &&
             (Pawn->GetHealth()>HealHealth+0.5f || Pawn->GetShield()>HealShield+0.5f));
        if (Complete)
        {
            SetFire(false); HealingStarted=false; NextHealUse=N+0.25f;
            // Keep the same box and re-evaluate the real owned stack/cap on the next use.
        }
        else if (N>HealDeadline)
        { CancelHeal(); BotState=EBotState::Looting; LeavingBox=false; }
    }

    bool TryCombatHeight(bool Visible,bool Travelling)
    {
        float N=Syndicate::Now();auto Target=Enemy.Get();auto Here=Pawn->K2_GetActorLocation();
        if(!Target || Target->IsDead() || Travelling || Pawn->bIsInAnyStorm || !CanFight() ||
           WindowCover.Get() || WindowPending.Get() || (Pawn->CurrentWeapon && Pawn->CurrentWeapon->bIsReloadingWeapon) ||
           N<NextRamp || N<HeightPeekUntil || RampActive || HighGroundStage ||
           N-LastSeenTime>8.f || !Syndicate::Finite(LastSeen) || MaterialCount()<40) return false;
        float D=Syndicate::Distance2D(Here,LastSeen);
        auto OwnCover=HeldCover.Get();
        bool CoverOcclusion=OwnCover && OwnCover->GetOwner()==PC && !OwnCover->bDestroyed &&
            OwnCover->GetHealth()>0.f && Syndicate::Distance2D(Here,HeldCoverLocation)<700.f && N-LastSeenTime<3.f;
        // A recently seen opponent above our cover still creates a height goal.
        // Do not require sight through the wall we just built; do not read a
        // hidden opponent's new position to choose the stair.
        bool Reclaim=LastSeen.Z>Here.Z+120.f;
        bool Incoming=N-LastDamageTime<3.f || N-LastShotCueAt<1.5f;
        bool Contest=(Visible || Incoming) && D<(Incoming ? 6000.f : 4500.f) && D>120.f;
        if(D>6000.f || (!Reclaim && !Contest && !CoverOcclusion) || (!Visible && !Incoming && !CoverOcclusion && !Reclaim) ||
           !AllowedDestination(LastSeen)) return false;
        FVector Center{},Half{};float Height=0.f;
        if(!BuildingCell(Center,Half,Height)) { HighGroundUntil=0.f;return TryHeightExit(); }
        BuildFightHeight=LastSeen.Z+Height*2.f;
        if(BuildFightHeight<=Here.Z+60.f) return false;
        SetFire(false);SetADS(false);
        HarvestTarget.Clear();BreachTarget.Clear();GatheringBurst=false;StopMove();
        bool CrestRestart=N<BuildLevelHintUntil && std::abs(Here.Z-(Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f)-BuildLevelHintZ)<80.f &&
            Syndicate::Distance2D(Here,BuildLevelHintPoint)<180.f;
        HighGroundSteps=0;HighGroundUntil=N+35.f;AscentUntil=0.f;
        CoveredRampRush=Visible && !Reclaim && D>1400.f;
        HighGroundToward=BotTactics::BuildContinuation(Here,LastSeen,LastSeen-Here,CombatSide,CoveredRampRush);
        CornerCellReady=false;CornerBridgeReady=!CrestRestart;HighGroundStage=1;
        if(EnvironmentAction()) return true;
        return TryHeightExit();
    }
    bool OfferCombatCover(bool Visible,bool Travelling)
    {
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        if(!Visible || Travelling || Pawn->bIsInAnyStorm || !CanFight() || N<NextEngageCover ||
           RampActive || HighGroundStage || N<HeightPeekUntil || WindowCover.Get() || WindowPending.Get() || MaterialCount()<10) return false;
        auto Cover=HeldCover.Get();
        if(Cover && !Cover->bDestroyed && Cover->GetHealth()>0.f &&
           Syndicate::Distance2D(Here,HeldCoverLocation)<700.f) return false;
        NextEngageCover=N+3.f;
        DefensiveBuild();return QueuedBuild.Get()!=nullptr;
    }
    void UpdateHeightContest(bool Visible,bool Travelling)
    {
        float N=Syndicate::Now();auto Target=Enemy.Get();auto Here=Pawn->K2_GetActorLocation();
        if(!Target || Target->IsDead() || !Visible || Travelling || Pawn->bIsInAnyStorm || !CanFight() ||
           MaterialCount()<40 || N-LastSeenTime>.8f || !Syndicate::Finite(LastSeen)) return;
        bool Same=HeightContestTarget.Get()==Target;
        bool Raised=Same && LastSeen.Z>ObservedContestHeight+80.f;
        HeightContestTarget.Set(Target);
        if(!Same || Raised || LastSeen.Z<ObservedContestHeight-80.f) ObservedContestHeight=LastSeen.Z;
        // Keep a height baseline so several small stair steps accumulate into a reclaim, instead of resetting it every decision tick.
        if(Raised || LastSeen.Z>Here.Z+120.f)
        {
            if(N<HighGroundUntil)
            {
                FVector Center{},Half{};float Height=0.f;
                if(BuildingCell(Center,Half,Height)) BuildFightHeight=std::max(BuildFightHeight,LastSeen.Z+Height*2.f);
                HighGroundUntil=std::max(HighGroundUntil,N+12.f);
            }
            else if(Raised) NextRamp=std::min(NextRamp,N+.15f); // React to an observed new height claim instead of waiting the ordinary four-second stair cooldown.
        }
    }
    void ResumeHeightPeek()
    {
        if(!HeightResumeCorner || Syndicate::Now()<HeightPeekUntil) return;
        auto Target=Enemy.Get();
        if(Target && !Target->IsDead() && Pawn->CurrentWeapon && Pawn->CurrentWeapon->bIsReloadingWeapon &&
           Syndicate::Now()<HeightPeekDeadline)
        { HeightPeekUntil=Syndicate::Now()+.1f;return; } // Let the actual reload complete; do not repeatedly cancel it with schematics.
        HeightResumeCorner=false;
        if(Target && !Target->IsDead() && Syndicate::Now()<HighGroundUntil &&
           Syndicate::Now()-LastSeenTime<8.f && MaterialCount()>=40)
        { CornerCellReady=false;HighGroundStage=1;SetFire(false);SetADS(false);RequestJump(); }
        else { HighGroundStage=0;HighGroundUntil=0.f; }
    }
    bool CombatConstruction(bool Visible,bool Travelling)
    {
        // Choose the height strategy before proactive cover can occlude the same-level opponent.
        // Once native construction starts, the existing physical stair/corner task owns continuation.
        if(!Travelling && HeightExitAction()) return true;
        if(TryCombatHeight(Visible,Travelling)) return true;
        if(!Travelling && !Pawn->bIsInAnyStorm && Enemy.Get() && CanFight() &&
           MaterialCount()>=40 && Syndicate::Now()-LastSeenTime<8.f && TryHeightExit()) return true;
        return OfferCombatCover(Visible,Travelling);
    }
    void Combat(bool Warmup = false,bool Travelling=false)
    {
        float N = Syndicate::Now();
        auto P = Enemy.Get();
        if (!P || P->IsDead())
        {
            SetFire(false);
            SetADS(false);
            Enemy.Clear();
            HighGroundStage=0; HighGroundUntil=0.f;
            BotState = EBotState::Looting;
            return;
        }
        if(!Warmup && !CanFight()) { EvadeForAmmo(); return; }
        auto Here = Pawn->K2_GetActorLocation();
        bool Visible=PC->LineOfSightTo(P,AimOrigin(),true);
        if(!Warmup) UpdateHeightContest(Visible,Travelling);
        if(!Warmup && (HighGroundStage || RampActive) && EnvironmentAction()) { SetFire(false);SetADS(false);return; }
        if(!Warmup && CombatConstruction(Visible,Travelling)) return;
        if (!Visible)
        {
            if(CombatActorFocus){PC->K2_ClearFocus();CombatActorFocus=false;}
            if(!Warmup)
                if(auto Blocker=OwnShotBlocker(LastSeen+FVector(0,0,50.f)); Blocker && TryWindow(Blocker,LastSeen+FVector(0,0,50.f))) return;
            if(!Warmup && !Travelling && AttackEnemyCover()) return;
            if (!Pursue(Warmup)) { SetFire(false); SetADS(false); Enemy.Clear(); }
            return;
        }
        float D = Syndicate::Distance2D(Here, P->K2_GetActorLocation());
        SimpleSwitchToWeapon(D);
        auto W = Pawn->CurrentWeapon;
        Syndicate::Ref<AFortWeapon> WeaponGuard(W);
        if (!W || !W->WeaponData || !IsGun(W->WeaponData))
        {
            SetFire(false);
            SetADS(false);
            Loot(Warmup);
            return;
        }
        Sprint(false);
        int R = Role(W->WeaponData);
        bool GroundCombat=LastSeen.Z>=Here.Z-300.f; // Keep a gained firing platform; a stale terrain projection below new builds must not send us off its edge.
        if(!GroundCombat && HasGoal && MoveGoal.Z<Here.Z-160.f) StopMove();
        SetADS(R != 0 && D > 700.f);
        if (N > NextAimError)
        {
            AimError = FVector(Syndicate::Between(-CombatSkill::AimError,CombatSkill::AimError)*BotDifficulty::Scale(PC,BotDifficulty::AimAccuracy,12.f),Syndicate::Between(-CombatSkill::AimError,CombatSkill::AimError)*BotDifficulty::Scale(PC,BotDifficulty::AimAccuracy,12.f),Syndicate::Between(32.f,58.f));
            NextAimError = N + Syndicate::Between(0.45f, 0.9f);
        }
        if (!Warmup && (N-LastDamageTime<3.f || N-LastShotCueAt<1.f) && N > NextBuild)
        {
            DefensiveBuild();
            NextBuild = N + Syndicate::Between(.22f, .35f);
            if(QueuedBuild.Get()) return;
        }
        if(!Warmup)
            if(auto Blocker=OwnShotBlocker(LastSeen+AimError))
            {
                SetFire(false);SetADS(false);
                if(TryWindow(Blocker,LastSeen+FVector(0,0,50.f))) return;
                if(Blocker==RampActor.Get() && Here.Z<RampGoal.Z-40.f && N<RampDeadline+4.f)
                {
                    RampActive=true;RampEntering=Here.Z<RampEntry.Z+60.f;
                    RampDeadline=N+8.f;RampProgressAt=N;
                    RampBestDistance=(Here-(RampEntering ? RampEntry : RampGoal)).Magnitude();
                    EnvironmentAction();return;
                }
                Pursue(false);return; // Native routes go around cover; firing never demolishes our own opening.
            }
        if (RampActive)
            EnvironmentAction();
        else if (!HasGoal || BotTactics::Reached(Here, MoveGoal, 160.f))
        {
            if (N > NextCombatSide)
            {
                CombatSide *= -1.f;
                NextCombatSide = N + Syndicate::Between(4.f, 7.f);
            }
            bool Elevated=std::abs(LastSeen.Z-Here.Z)>150.f;
            float Range = R == 0 ? 650.f : (Elevated ? 900.f : (R == 2 ? 5000.f : 2200.f));
            auto Goal=BotTactics::CombatGoal(Here,P->K2_GetActorLocation(),Range,CombatSide);
            if (AllowedDestination(Goal,Warmup)) Move(Goal,140.f,false,GroundCombat);
            else StopMove();
        }
        else if (AllowedDestination(MoveGoal,Warmup))
            Move(MoveGoal,140.f,false,GroundCombat);
        else StopMove();
        // Movement may clear a reached-goal focus. Restore aim after all moves.
        CombatActorFocus=true;Look(P->K2_GetActorLocation() + AimError);
        W = WeaponGuard.Get();
        if (!W || Pawn->CurrentWeapon != W) { SetFire(false); return; }
        if (W->AmmoCount <= 0 || W->bIsReloadingWeapon)
        {
            SetFire(false);
            auto Reload=BotReload::Check(PC,W,NextReload);
            if (Reload == BotReload::Result::NeedsAmmo)
            { SetADS(false); Forage(); }
            return;
        }
        if (N < ReactionReady)
        {
            SetFire(false);
            return;
        }
        auto Desired = UKismetMathLibrary::FindLookAtRotation(AimOrigin(), P->K2_GetActorLocation() + AimError);
        if (!BotTactics::AimAligned(Desired, PC->GetControlRotation(), Pawn->K2_GetActorRotation()))
        {
            SetFire(false);
            return;
        }
        if(TickGunTrigger(W,N))return;
        // Held native fire input retains the weapon's real cadence and ammunition costs.
        if (Firing && N > BurstEnd)
        {
            SetFire(false);
            NextBurst = N + Syndicate::Between(0.15f, 0.4f);
        }
        else if (!Firing && N > NextBurst)
        {
            SetFire(true);
            BurstEnd = N + Syndicate::Between(R == 0 ? 0.12f : 0.5f, R == 0 ? 0.2f : 1.5f);
        }
    }
    AActor *ShotObstacle(const FVector &Point,AActor *Ignore=nullptr)
    {
        auto Origin=AimOrigin();auto Weapon=Pawn->CurrentWeapon;
        if(Weapon && IsGun(Weapon->WeaponData))
        { auto Muzzle=Weapon->GetMuzzleLocation(0);if(Syndicate::Finite(Muzzle) && Syndicate::Distance2D(Muzzle,Origin)<250.f) Origin=Muzzle; }
        FHitResult Hit{};TArray<AActor*> Ignored;Ignored.Add(Pawn);
        if(Weapon) Ignored.Add(Weapon);if(Ignore) Ignored.Add(Ignore);
        bool Blocked=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),Origin,Point,
            BotPhysics::PawnProfile(),true,Ignored,EDrawDebugTrace::None,&Hit,true,{},{},0);
        Ignored.Free();return Blocked ? Hit.Actor.Get() : nullptr;
    }
    ABuildingSMActor *OwnShotBlocker(const FVector &Point)
    {
        auto B=Syndicate::As<ABuildingSMActor>(ShotObstacle(Point));
        return B && B->bPlayerPlaced && !B->bDestroyed && !B->IsActorBeingDestroyed() &&
            (B->GetOwner()==PC || B->TeamIndex==PlayerState->TeamIndex) ? B : nullptr;
    }
    bool TryWindow(ABuildingSMActor *B,const FVector &Point,bool ReloadOpportunity=false)
    {
        float N=Syndicate::Now();
        if(!BotDifficulty::Allow(PC,BotDifficulty::EditFrequency,N))return false;
        if(!B || N<NextWindow || WindowCover.Get() || WindowPending.Get() || RampActive || HighGroundStage ||
           !CanFight() || N-LastSeenTime>2.f || !B->IsA(ABuildingWall::StaticClass()) ||
           B->GetOwner()!=PC || !B->GetName().contains("Solid") || B->GetDistanceTo(Pawn)>=550.f ||
           (N-CoverHitAt<(ReloadOpportunity ? .12f : .35f)) || !BotTactics::WindowShot(AimOrigin(),Point,B->K2_GetActorLocation(),B->K2_GetActorRotation().Yaw)) return false;
        auto Beyond=ShotObstacle(Point,B);
        if(Beyond && Beyond!=Enemy.Get()) return false;
        if(ReloadOpportunity)
        {
            auto Target=Enemy.Get();auto Weapon=Target ? Target->CurrentWeapon : nullptr;
            if(Beyond!=Target || !Weapon || !Weapon->bIsReloadingWeapon) return false;
        } // Editing this wall cannot expose a shot through a second obstruction.
        if(!QueueEdit(B,false)) return false;
        WindowPending.Set(B);NextWindow=N+BotDifficulty::Delay(PC,BotDifficulty::EditSpeed,1.1f);return true;
    }
    bool TryReloadPeek(ABuildingSMActor *Cover)
    {
        auto Target=Enemy.Get();
        if(!Target || Target->IsDead() || Syndicate::Now()-LastSeenTime>2.f ||
           !PC->LineOfSightTo(Target,AimOrigin(),true)) return false;
        auto Weapon=Target->CurrentWeapon;
        if(!Weapon || !Weapon->bIsReloadingWeapon) return false;
        return TryWindow(Cover,LastSeen+FVector(0,0,50.f),true);
    }
    void CompleteCombatEdit(ABuildingSMActor *Old,ABuildingSMActor *Edited)
    {
        float N=Syndicate::Now();
        if(HeldCover.Pointer==Old) { HeldCover.Set(Edited);HeldCoverHealth=Edited->GetHealth(); }
        if(WindowPending.Pointer==Old && !WallEdit.Reset)
        {
            WindowCover.Set(Edited);WindowCloseAt=N+1.25f;NextWindowResetRetry=0.f;
            WindowPending.Clear();HeldCover.Clear();WindowWeapon.Clear();WindowAmmo=-1;WindowFired=false;
            BuildingVisual=false;BuildingUntil=N;SetFire(false);StopMove();
        }
        if(WallEdit.Reset)
        {
            HeldCover.Set(Edited);HeldCoverLocation=Edited->K2_GetActorLocation();HeldCoverYaw=Edited->K2_GetActorRotation().Yaw;
            HeldCoverHealth=Edited->GetHealth();HoldCoverUntil=N+2.f;
        }
    }
    void CloseWindow()
    {
        if(!WindowCover.Pointer || WallEdit.Active()) return;
        auto B=WindowCover.Get();float N=Syndicate::Now();
        if(!B || B->bDestroyed || B->IsActorBeingDestroyed() || B->GetOwner()!=PC)
        { WindowCover.Clear();return; }
        if(N<WindowCloseAt && Enemy.Get() && N-CoverHitAt>=.2f && !Pawn->bIsInAnyStorm) return;
        SetFire(false);SetADS(false);StopMove();
        if(N<NextWindowResetRetry) return;
        if(!WallEdit.Begin(PC,Pawn,PlayerState,B,false,true))
        {
            NextWindowResetRetry=N+.2f;
            if(N<WindowCloseAt+1.5f) return;
        }
        else NextEdit=N+.2f;
        WindowCover.Clear();WindowPending.Clear();WindowWeapon.Clear();WindowAmmo=-1;
        NextWindow=N+(WindowFired ? 1.1f : 3.f); // A failed firing lane cannot produce an endless open/reset animation.
        WindowFired=false;
    }
    bool Pursue(bool Warmup=false)
    {
        auto Target=Enemy.Get();
        float Now=Syndicate::Now();
        float Until=Target && Threat.Get()==Target ? ThreatUntil : std::max(PursuitUntil,LastSeenTime+12.f);
        if (!Target || Target->IsDead() || Now>=Until || !AllowedDestination(LastSeen,Warmup)) return false;
        if (!Warmup && (BreachTarget.Get() || RampActive) && EnvironmentAction()) return true;
        SetFire(false); SetADS(false);
        auto Here=Pawn->K2_GetActorLocation();
        if (!PursuitSearching && !BotTactics::Reached(Here,LastSeen,150.f))
        {
            if (!Warmup && Now-LastProgress>3.f && ReachEnvironment(LastSeen)) return true;
            if(Move(LastSeen,120.f,true,false))
            { Look(LastSeen+FVector(0,0,50.f));return true; }
            if(!Warmup && ReachEnvironment(LastSeen)) return true;
            PursuitSearching=true;NextPursuitSearch=Now; // A rejected route must not hold a stationary chase for twelve seconds.
        }
        if (!PursuitSearching) { StopMove(); PursuitSearching=true; NextPursuitSearch=0.f; }
        if (HasGoal && !BotTactics::Reached(Here,PursuitGoal,150.f) && Now-LastProgress<3.f)
        { Move(PursuitGoal,120.f,true,false); Look(PursuitGoal+FVector(0,0,50.f)); return true; }
        if (!Warmup && HasGoal && Now-LastProgress>3.f && ReachEnvironment(PursuitGoal)) return true;
        if (Now>=NextPursuitSearch)
        {
            NextPursuitSearch=Now+2.5f;
            FVector Goal{};
            if (NativeNavigation::RandomReachable(LastSeen,900.f,Goal) && Syndicate::Finite(Goal) &&
                Syndicate::Distance2D(Here,Goal)>180.f && Syndicate::Distance2D(Goal,LastSeen)<1100.f &&
                AllowedDestination(Goal,Warmup))
            { StopMove(); PursuitGoal=Goal; Move(Goal,120.f,true,false); }
        }
        Look(LastSeen+FVector(0,0,50.f));
        return true;
    }
    void WarmupTick()
    {
        float N = Syndicate::Now();
        if (N > NextWarmup)
        {
            CancelEmote();
            WarmupChoice = static_cast<EWarmup>(Syndicate::Pick(5));
            NextWarmup = N + Syndicate::Between(12, 25);
            SetFire(false);
            SetADS(false);
            StopMove();
        }
        if (WarmupChoice == EWarmup::Watch || WarmupChoice == EWarmup::Fight)
            Sense();
        switch (WarmupChoice)
        {
        case EWarmup::Idle:
            StopMove();
            SetFire(false);
            SetADS(false);
            break;
        case EWarmup::Emote:
            StopMove();
            SetFire(false);
            SetADS(false);
            if (N > NextEmote)
            {
                Emote();
                NextEmote = N + Syndicate::Between(8, 15);
            }
            break;
        case EWarmup::Watch:
            StopMove();
            SetFire(false);
            SetADS(false);
            if (auto P = Enemy.Get())
                Look(P->K2_GetActorLocation());
            break;
        case EWarmup::Roam:
            SetFire(false);
            SetADS(false);
            Loot(true);
            break;
        case EWarmup::Fight:
            if (HasGun() && Enemy.Get())
                Combat(true);
            else
            {
                SetFire(false);
                Loot(true);
            }
            break;
        }
    }
    void RecordKillLoot(const FVector &Point)
    {
        if(!Syndicate::Finite(Point)) return;
        SpoilsCenter=Point; SpoilsUntil=Syndicate::Now()+30.f; NextSpoilsSearch=0.f;
    }
    bool LootSpoils()
    {
        float N=Syndicate::Now(); auto Here=Pawn->K2_GetActorLocation();
        if(N>=SpoilsUntil || !AllowedDestination(SpoilsCenter) || Syndicate::Distance2D(Here,SpoilsCenter)>6500.f)
        { SpoilsUntil=0.f; return false; }
        if(auto Target=LootTarget.Get())
        {
            if(Syndicate::Distance2D(Target->K2_GetActorLocation(),SpoilsCenter)<1200.f) { Loot(); return true; }
            return false; // Keep an existing useful objective instead of bouncing between loot piles.
        }
        if(N<NextSpoilsSearch) return false;
        NextSpoilsSearch=N+0.4f;
        AFortPickup *Best=nullptr; float Score=100000.f; auto Ammo=NeededAmmo();
        for(auto Ref:LocalPickups(6500.f))
            if(auto P=Ref.Get(); P && !P->bHidden && !P->bPickedUp && P->PrimaryPickupItemEntry.Count>0 &&
                !Reserved(P,this) && !(IgnoredLoot.Get()==P && N<IgnoreUntil))
            {
                auto D=P->PrimaryPickupItemEntry.ItemDefinition; auto At=P->K2_GetActorLocation();
                if(Syndicate::Distance2D(At,SpoilsCenter)>1200.f || !Wants(D,P->PrimaryPickupItemEntry.LoadedAmmo) || !AllowedDestination(At) || RouteMemory.Blocked(At,N) ||
                   !PC->LineOfSightTo(P,AimOrigin(),true)) continue;
                if(!HasGun() && !IsGun(D) && !UsableHealthHeal(D,Pawn->GetHealth())) continue;
                float Value=Syndicate::Distance2D(Here,At)+std::abs(Here.Z-At.Z)*3.f;
                if(IsGun(D)) Value-=2000.f+static_cast<int>(D->Rarity)*100.f;
                if(D==Ammo) Value-=2500.f;
                if(UsableHealthHeal(D,Pawn->GetHealth())) Value-=Pawn->GetHealth()<35.f ? 5000.f : 1800.f;
                if(Value<Score) { Best=P; Score=Value; }
            }
        if(Best) { SetFire(false); StopMove(); BeginLootTarget(Best); Loot(); return true; }
        // Give real death pickups time to settle/index before retiring the pile.
        if(N+26.f<SpoilsUntil) return false;
        SpoilsUntil=0.f; return false;
    }
    void ReactToCues()
    {
        float N=Syndicate::Now();if(N<NextCueCheck) return;NextCueCheck=N+.06f;
        auto Here=Pawn->K2_GetActorLocation();
        auto Consider=[&](AFortPlayerPawnAthena *Source,const FVector &Point,float At,bool Incoming)
        {
            auto State=Source ? Syndicate::As<AFortPlayerStateAthena>(Source->PlayerState) : nullptr;
            if(!State || Source==Pawn || Source->IsDead() || State->TeamIndex==PlayerState->TeamIndex) return false;
            bool Visible=PC->LineOfSightTo(Source,AimOrigin(),true);
            if(Incoming) { LastShotCueAt=N; bPotentiallyUnderAttack=true; }
            // This bearing comes from a recorded event. Hidden moving actors do not supply updated coordinates.
            if((Incoming || !Enemy.Get()) && !FightBuild.Active() && !QueuedBuild.Get())
                Look(Point+FVector(0,0,60.f)); // Record the threat without stealing the committed builder's view budget.
            bool ArmedSource=Source->CurrentWeapon && IsGun(Source->CurrentWeapon->WeaponData);
            if(Visible && (Incoming || !Enemy.Get()) && (Incoming || CanFight() || ArmedSource))
            {
                if(Enemy.Get()!=Source) ReactionReady=N+BotDifficulty::Delay(PC,BotDifficulty::ReactionSpeed,.08f);
                Enemy.Set(Source);Threat.Set(Source);ThreatUntil=N+8.f;
                LastSeen=Point;LastSeenTime=At;PursuitUntil=N+12.f*BotDifficulty::Scale(PC,BotDifficulty::Pursuit,.25f);NextSense=0.f;
            }
            else if(Incoming)
            {
                Enemy.Set(Source);Threat.Set(Source);ThreatUntil=N+8.f;
                LastSeen=Point;LastSeenTime=At;PursuitUntil=N+12.f*BotDifficulty::Scale(PC,BotDifficulty::Pursuit,.25f);NextSense=0.f;
                // A shooter identity plus recorded sound bearing supports
                // cover/pursuit, never firing through unseen geometry.
                Investigation.Offer(Point,N);
            }
            else if(!Enemy.Get()) Investigation.Offer(Point,N);
            return true;
        };
        for(auto I=RecentShots.rbegin();I!=RecentShots.rend();++I)
            if(N-I->Time<.6f && BotTactics::IncomingShot(Here,I->Start,I->Direction))
            {
                auto Source=I->Source.Get();if(!Source || Source==Pawn) continue;
                FHitResult Hit{};TArray<AActor*> Ignored;Ignored.Add(Pawn);Ignored.Add(Source);
                if(Source->CurrentWeapon) Ignored.Add(Source->CurrentWeapon);
                bool Blocked=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),I->Start,Here,
                    BotPhysics::PawnProfile(),false,Ignored,EDrawDebugTrace::None,&Hit,true,{},{},0);Ignored.Free();
                bool NearbyCover=Blocked && Syndicate::Finite(Hit.ImpactPoint) &&
                    Syndicate::Distance2D(Hit.ImpactPoint,Here)<350.f;
                if((!Blocked || NearbyCover) && !Hit.bStartPenetrating && Consider(Source,I->Start,I->Time,true)) break;
            }
        if(!Enemy.Get()) for(auto I=RecentSounds.rbegin();I!=RecentSounds.rend();++I)
            if(N-I->Time<.8f && Syndicate::Distance2D(Here,I->Point)<=std::min(I->Range,2200.f))
            { if(Consider(I->Source.Get(),I->Point,I->Time,false)) break; }
    }
    void Hear(AActor *Source,const FAIStimulus &Stim)
    {
        if(!Stim.bSuccessfullySensed || !std::isfinite(Stim.Age) || Stim.Age>2.f ||
            !std::isfinite(Stim.Strength) || Stim.Strength<=0.f || !Syndicate::Finite(Stim.StimulusLocation)) return;
        auto SourcePawn=Syndicate::As<AFortPlayerPawnAthena>(Source);
        auto State=SourcePawn ? Syndicate::As<AFortPlayerStateAthena>(SourcePawn->PlayerState) : nullptr;
        if(!State || SourcePawn==Pawn || State->TeamIndex==PlayerState->TeamIndex || SourcePawn->IsDead()) return;
        if(UAIPerceptionSystem::GetSenseClassForStimulus(UWorld::GetWorld(),Stim).Get()!=UAISense_Hearing::StaticClass()) return;
        ++NativeHearingCount;
        float N=Syndicate::Now();
        if(!CanFight() || HealingStarted || N<NextInvestigation) return;
        // Stimulus location is a past native sound event, never a hidden live pawn coordinate.
        if(Syndicate::Distance2D(Pawn->K2_GetActorLocation(),Stim.StimulusLocation)<=3500.f && AllowedDestination(Stim.StimulusLocation))
            if(Investigation.Offer(Stim.StimulusLocation,N)) ++InvestigationsStarted;
    }
    bool Investigate()
    {
        float N=Syndicate::Now(); auto Here=Pawn->K2_GetActorLocation();
        if(!CanFight() || HealingStarted || FightBuild.Active()) return false;
        if((RampActive || BreachTarget.Get()) && EnvironmentAction()) return true;
        if(!Investigation.Active(N) && N>=NextInvestigation && N>=NextAwarenessScan)
        {
            NextAwarenessScan=N+.3f;
            // Only actual native noise events enter this queue. The source's live position is never queried here.
            for(const auto &Event:RecentSounds)
                if(auto Source=Event.Source.Get(); Source && N-Event.Time<=2.f && Source!=Pawn && !Source->IsDead())
                {
                    auto State=Syndicate::As<AFortPlayerStateAthena>(Source->PlayerState);
                    if(!State || State->TeamIndex==PlayerState->TeamIndex) continue;
                    float Range=(PC->LineOfSightTo(Source,AimOrigin(),true) ? Event.Range : Event.Range*.5f)*BotDifficulty::Scale(PC,BotDifficulty::Awareness,.25f);
                    if(Syndicate::Distance2D(Here,Event.Point)<=Range && AllowedDestination(Event.Point) &&
                       !RouteMemory.Blocked(Event.Point,N)) { if(Investigation.Offer(Event.Point,N)) ++InvestigationsStarted; break; }
                }
            for(auto &Event:RecentBuilds)
                if(auto B=Event.Actor.Get(); B && N-Event.Time<5.f && B->bPlayerPlaced && !B->bDestroyed &&
                    B->TeamIndex!=PlayerState->TeamIndex && B->IsUnderConstruction() &&
                    Syndicate::Distance2D(Here,B->K2_GetActorLocation())<3500.f && PC->LineOfSightTo(B,AimOrigin(),true) &&
                    AllowedDestination(B->K2_GetActorLocation()) && !RouteMemory.Blocked(B->K2_GetActorLocation(),N))
                { if(Investigation.Offer(B->K2_GetActorLocation(),N)) ++InvestigationsStarted; break; }
        }
        if(!Investigation.Active(N)) return false;
        auto Goal=Investigation.Point;
        if(!AllowedDestination(Goal) || RouteMemory.Blocked(Goal,N))
        { Investigation.Clear(); NextInvestigation=N+8.f; return false; }
        if(BotTactics::Reached(Here,Goal,200.f))
        { StopMove(); Look(Goal+FVector(0,0,60.f)); Investigation.Clear(); NextInvestigation=N+8.f; return false; }
        if(LootTarget.Get()) { Release(this);LootTarget.Clear();StopMove(); } // An armed, supplied bot can investigate a fresh local cue instead of searching forever for two reserve clips.
        SetFire(false); SetADS(false); SimpleSwitchToWeapon();
        if((BreachTarget.Get() || RampActive) && EnvironmentAction()) return true;
        if(N-LastProgress>3.f && ReachEnvironment(Goal)) return true;
        if(!Move(Goal,150.f,true,false)) { Investigation.Clear(); NextInvestigation=N+8.f; return false; }
        Look(Goal+FVector(0,0,60.f)); return true;
    }
    void Forage()
    {
        // Compatibility callers use the same committed loot sequence. They
        // cannot start a second harvest/exploration scheduler during ammo evasion.
        if(!LootSpoils()) GroundLoot(false);
    }
    void BeginDoorPass(ABuildingSMActor *Building)
    {
        auto Wall=Syndicate::As<ABuildingWall>(Building);
        if(!Wall || Wall->bDestroyed) return;
        auto Here=Pawn->K2_GetActorLocation();
        FVector Center{},Extent{}; Wall->GetActorBounds(false,&Center,&Extent);
        if(!Syndicate::Finite(Center) || !Syndicate::Finite(Extent)) return;
        if(Wall->DoorBoxComponent)
        {
            auto Trigger=Wall->DoorBoxComponent->K2_GetComponentLocation();
            if(Syndicate::Finite(Trigger) && std::abs(Trigger.X-Center.X)<=Extent.X+75.f &&
               std::abs(Trigger.Y-Center.Y)<=Extent.Y+75.f && std::abs(Trigger.Z-Center.Z)<=Extent.Z+75.f)
                Center=Trigger;
        }
        if(Syndicate::Distance2D(Here,Center)>700.f) return; // Never chase a stale or remote edited-door component.
        Center.Z=Here.Z;
        float R=(Wall->K2_GetActorRotation().Yaw+90.f)*3.14159265f/180.f;
        FVector Normal(std::cos(R),std::sin(R),0);
        if((Here.X-Center.X)*Normal.X+(Here.Y-Center.Y)*Normal.Y>0.f) Normal=Normal*-1.f;
        DoorPass.Set(Wall); DoorCenter=Center; DoorAcross=Normal;
        DoorPassUntil=Syndicate::Now()+6.f; StopMove(); NextPath=0.f;
    }
    bool TickDoorPass()
    {
        if(!DoorPass.Pointer) return false;
        auto Wall=DoorPass.Get(); float N=Syndicate::Now(); auto Here=Pawn->K2_GetActorLocation();
        float Across=(Here.X-DoorCenter.X)*DoorAcross.X+(Here.Y-DoorCenter.Y)*DoorAcross.Y;
        float Side=std::abs((Here.X-DoorCenter.X)*DoorAcross.Y-(Here.Y-DoorCenter.Y)*DoorAcross.X);
        bool Pressured=N-LastDamageTime<0.5f && Enemy.Get() && Syndicate::Distance2D(Here,LastSeen)<900.f;
        bool ClosedTimeout=Wall && !Wall->IsDoorOpen() && N>=DoorPassUntil-4.5f;
        if(!Wall || Wall->bDestroyed || N>=DoorPassUntil || ClosedTimeout || Pressured || (Across>120.f && Side<220.f))
        {
            if(N>=DoorPassUntil || ClosedTimeout) { RouteMemory.Fail(DoorCenter,N); IgnoredBreach.Set(Wall); IgnoreBreachUntil=N+15.f; }
            DoorPass.Clear(); StopMove(); NextPath=0.f; NextEdit=N+3.f;
            return false;
        }
        SetFire(false); SetADS(false);
        BotInteraction::OpenDoor(PC,Pawn,Wall,NextDoor);
        // Align with the actual door trigger before crossing the edited wall.
        auto Goal=DoorCenter+DoorAcross*(Wall->IsDoorOpen() ? 320.f : -120.f);
        bool Changed=!HasGoal || !LocalSteering || Syndicate::Distance2D(MoveGoal,Goal)>30.f;
        if(Changed) { PC->StopMovement(); PC->K2_ClearFocus(); SteeringDirection={}; NextSteer=N; }
        Sprint(true); HasGoal=LocalSteering=true; RampSteering=GroundGoal=false;
        MoveGoal=Goal; MoveRadius=65.f;
        Look(Goal+FVector(0,0,50.f));
        return true;
    }
    void EvadeForAmmo()
    {
        if(CanFight()) { EvadeUntil=0.f; return; }
        float N=Syndicate::Now(); auto Here=Pawn->K2_GetActorLocation();
        SetFire(false); SetADS(false); HealingStarted=false;
        HighGroundStage=0; HighGroundUntil=0.f; GatheringBurst=false;
        bool Threatened=AmmoThreatened();
        if(!Threatened) { EvadeUntil=0.f; Forage(); return; }
        // Keep the actual ammo objective when it leads away from the last observed opponent.
        if(auto Target=LootTarget.Get(); Target && !BotTactics::AmmoEscapeTarget(Here,Target->K2_GetActorLocation(),LastSeen))
        { Release(this); LootTarget.Clear(); StopMove(); }
        if(LootTarget.Get()) { EvadeUntil=0.f; Loot(); return; }
        if(N>=NextLootSearch)
        {
            NextLootSearch=N+BotDifficulty::Delay(PC,BotDifficulty::Looting,.6f);
            if(auto Target=FindLootTarget(false); Target &&
               BotTactics::AmmoEscapeTarget(Here,Target->K2_GetActorLocation(),LastSeen))
            { HarvestTarget.Clear(); BreachTarget.Clear(); RampActive=false; StopMove(); BeginLootTarget(Target); EvadeUntil=0.f; Loot(); return; }
        }
        if(HarvestTarget.Get() || BreachTarget.Get()) { HarvestTarget.Clear(); BreachTarget.Clear(); StopMove(); }
        if(N>=NextBuild && MaterialCount()>=10 && Syndicate::Distance2D(Here,LastSeen)<3000.f)
        { DefensiveBuild(); NextBuild=N+.35f; if(QueuedBuild.Get()) return; }
        BotState=EBotState::Looting;
        if(RampActive && EnvironmentAction()) return; // Finish an actual escape stair, never stand and dry-fire.
        if(N<EvadeUntil && HasGoal && N-LastProgress<3.f && AllowedDestination(EvadeGoal))
        { Move(EvadeGoal,130.f,true,true); return; }
        auto D=Here-LastSeen; float L=std::max(1.f,std::hypot(D.X,D.Y));
        FVector Away(D.X/L,D.Y/L,0);
        StopMove();
        for(int I=0;I<3;++I)
        {
            auto Goal=Here+Away*(I==0 ? 1400.f : 650.f)+FVector(-Away.Y,Away.X,0)*(I==1 ? 900.f : (I==2 ? -900.f : 0.f));
            if(!AllowedDestination(Goal) || RouteMemory.Blocked(Goal,N)) continue;
            if(Move(Goal,130.f,true,true)) { EvadeGoal=Goal; EvadeUntil=N+3.f; return; }
        }
        Forage(); // No safe escape route: keep looking for real ammunition rather than pursuing the opponent.
    }
    bool AmmoThreatened()
    {
        auto Target=Enemy.Get(); float N=Syndicate::Now();
        if(!Target || N-LastSeenTime>=6.f) return false;
        if((Threat.Get()==Target && N<ThreatUntil) || N-LastDamageTime<3.f) return true;
        // Seeing another unarmed looter is not a reason to abandon arming.
        // Visible, loaded guns nearby or a real recent attacker warrant evasion.
        auto Weapon=Target->CurrentWeapon;
        return Weapon && IsGun(Weapon->WeaponData) && Weapon->AmmoCount>0 &&
            Syndicate::Distance2D(Pawn->K2_GetActorLocation(),LastSeen)<1800.f &&
            PC->LineOfSightTo(Target,AimOrigin(),true);
    }
    void FireWhileRouting()
    {
        auto P=Enemy.Get(); float N=Syndicate::Now();
        if(BreachTarget.Get() || HarvestTarget.Get()) return;
        if(!P || P->IsDead() || QueuedBuild.Get() || WallEdit.Active() || !CanFight() ||
           !PC->LineOfSightTo(P,AimOrigin(),true)) {if(CombatActorFocus){PC->K2_ClearFocus();CombatActorFocus=false;}SetFire(false);SetADS(false);return;}
        float Distance=Syndicate::Distance2D(Pawn->K2_GetActorLocation(),P->K2_GetActorLocation());
        if(Distance>12000.f) {SetFire(false);return;}
        SimpleSwitchToWeapon(Distance);
        auto W=Pawn->CurrentWeapon;
        if(!W || !W->WeaponData || !IsGun(W->WeaponData) ||
           !BotMission::WeaponInRange(Role(W->WeaponData),Distance)) {SetFire(false);SetADS(false);return;}
        Syndicate::Ref<AFortWeapon> WeaponGuard(W);
        SetADS(Role(W->WeaponData)!=0 && Distance>700.f);
        if(N>NextAimError)
        { AimError=FVector(Syndicate::Between(-CombatSkill::AimError,CombatSkill::AimError)*BotDifficulty::Scale(PC,BotDifficulty::AimAccuracy,12.f),Syndicate::Between(-CombatSkill::AimError,CombatSkill::AimError)*BotDifficulty::Scale(PC,BotDifficulty::AimAccuracy,12.f),Syndicate::Between(32.f,58.f)); NextAimError=N+0.6f; }
        CombatActorFocus=true;
        auto AimPoint=PredictCombatAim(P,W->WeaponData,Distance)+AimError;
        Look(AimPoint);
        W=WeaponGuard.Get();
        if(!W || Pawn->CurrentWeapon!=W) { SetFire(false); return; }
        if(W->AmmoCount<=0 || W->bIsReloadingWeapon)
        { SetFire(false); BotReload::Check(PC,W,NextReload); return; }
        auto Desired=UKismetMathLibrary::FindLookAtRotation(AimOrigin(),AimPoint);
        if(N<ReactionReady || !BotTactics::AimAligned(Desired,PC->GetControlRotation(),Pawn->K2_GetActorRotation()))
        { SetFire(false); return; }
        if(TickGunTrigger(W,N))return;
        if(Firing && N>BurstEnd) { SetFire(false); NextBurst=N+0.20f; }
        else if(!Firing && N>NextBurst) { SetFire(true); BurstEnd=N+0.8f; }
        // Keep the movement owner/actual ramp active; firing uses the weapon's native cadence.
    }
    bool HandleRouteThreat(AFortSafeZoneIndicator *Zone)
    {
        auto P=Enemy.Get(); float N=Syndicate::Now();
        if(!P || P->IsDead()) return false;
        bool Critical=Pawn->bIsInAnyStorm && Pawn->GetHealth()<35.f;
        bool Visible=PC->LineOfSightTo(P,AimOrigin(),true);
        auto Here=Pawn->K2_GetActorLocation();
        bool FightTime=Zone && BotTactics::CanCommitFight(Here,Zone->NextCenter,Zone->NextRadius,N,
            Zone->SafeZoneStartShrinkTime,Pawn->bIsInAnyStorm);
        bool ActiveFight=(RouteFightPermitHeight && N<RouteFightUntil) ||
            ((HighGroundStage || RampActive) && N<HighGroundUntil) || (BotState==EBotState::Combat && N<PursuitUntil);
        bool Committed=FightTime && ActiveFight && N-LastSeenTime<12.f;
        bool RecentHit=N-LastDamageTime<3.f || N-LastShotCueAt<1.f;
        if(!Visible && !Committed && !(RecentHit && N-LastSeenTime<3.f)) return false;
        float Distance=Syndicate::Distance2D(Here,Visible ? P->K2_GetActorLocation() : LastSeen);
        if(Distance>6000.f || (!RecentHit && Distance>2500.f)) return false;
        if(HarvestTarget.Get() || BreachTarget.Get()) SetFire(false);
        HarvestTarget.Clear(); BreachTarget.Clear();
        if(PressureCover(!FightTime || Critical || !CanFight())) return true;
        if(Critical || !CanFight() || (!Visible && !Committed)) return false;
        if(Committed)
        { RouteFightPermitHeight=true;BotState=EBotState::Combat; Combat(false,false); return true; } // Finish a recent fight/search while travel time remains; urgent escape still overrides.
        if(RampActive && !FightTime) return false; // An urgent escape stair retains movement; a safe combat stair retains its fight.
        if(N>=NextRouteFight && (RecentHit || Distance<2500.f))
        {
            RouteFightPermitHeight=FightTime;
            RouteFightUntil=N+(RouteFightPermitHeight ? 10.f : 2.f);
            NextRouteFight=N+(RouteFightPermitHeight ? 14.f : 6.f);
        }
        if(N<RouteFightUntil)
        { BotState=EBotState::Combat; Combat(false,!RouteFightPermitHeight); return true; }
        return false;
    }
    void RotateToZone(AFortSafeZoneIndicator *Zone,bool KeepFire=false)
    {
        float N = Syndicate::Now();
        auto Here = Pawn->K2_GetActorLocation();
        float Distance = Syndicate::Distance2D(Here, Zone->NextCenter);
        if (!RotationActive ||
            Syndicate::Distance2D(RotationGoal, Zone->NextCenter) > 1200.f)
        {
            bool FinishStair=RampActive && RampActor.Get() && !RampEntering;
            StopMove(); SetFire(false); HarvestTarget.Clear(); BreachTarget.Clear(); RampActive=FinishStair;
            RotationProgressAt = N; RotationBestDistance = Distance; RotationBestHeight=Here.Z;
            AscentUntil=0.f; AscentSteps=0; HighGroundStage=0; HighGroundUntil=0.f;
            RotationGoal = Zone->NextCenter; RotationBlocker.Clear(); RotationActive=true;
            RotationSide=CombatSide; RotationDetourUntil=0.f;
        }
        if(!KeepFire) SetADS(false); HealingStarted = false; BoxActive = false;
        Release(this); LootTarget.Clear(); BotState = EBotState::MovingToSafeZone;
        bool Progress = Distance + 100.f < RotationBestDistance || (RampActive && Here.Z>RotationBestHeight+50.f);
        if (Progress) RotationBestHeight=Here.Z;
        if (auto Blocker = BreachTarget.Get())
        {
            float Health = Blocker->GetHealth();
            if (RotationBlocker.Get() == Blocker && Health + 0.5f < RotationBlockerHealth) Progress = true;
            RotationBlocker.Set(Blocker); RotationBlockerHealth = Health;
        }
        else RotationBlocker.Clear();
        if (Progress) { RotationProgressAt = N; RotationBestDistance = Distance; }
        if (N - RotationProgressAt > (Pawn->bIsInAnyStorm ? 4.f : 18.f) && !(RampActive && N-RampProgressAt<2.5f))
        {
            if (auto Blocker = BreachTarget.Get())
            { IgnoredBreach.Set(Blocker); IgnoreBreachUntil = N + 20.f; }
            SetFire(false); BreachTarget.Clear(); HarvestTarget.Clear(); RampActive = false; StopMove();
            BlockedGoal = BotTactics::ZoneWaypoint(Here, Zone->NextCenter, Zone->NextRadius);
            BlockedUntil = N + 30.f; NextPath = 0.f;
            RouteMemory.Fail(BlockedGoal,N);
            AscentUntil=0.f; AscentSteps=0;
            RotationProgressAt = N; RotationBestDistance = Distance;
        }
        if ((BreachTarget.Get() || RampActive) && EnvironmentAction()) return;
        if(!KeepFire) SetFire(false); // Preserve a held native pickaxe swing while its obstacle action is active.
        if (HasGun()) SimpleSwitchToWeapon();
        if (!HasGoal || Syndicate::Distance2D(Here, MoveGoal) <= 180.f)
        {
            auto Goal = BotTactics::ZoneWaypoint(Here, Zone->NextCenter, Zone->NextRadius);
            if (RouteMemory.Blocked(Goal,N) || (N < BlockedUntil && Syndicate::Distance2D(Goal, BlockedGoal) < 500.f))
            {
                auto D = Zone->NextCenter - Here; float L = std::max(1.f, std::hypot(D.X,D.Y));
                if (N >= RotationDetourUntil)
                { RotationSide=-RotationSide; RotationDetourUntil=N+12.f; }
                Goal = Here + FVector(D.X/L,D.Y/L,0)*250.f + FVector(-D.Y/L,D.X/L,0)*(RotationSide*1600.f);
            }
            bool Started=Move(Goal,180.f,true,true);
            if(!Started && ReachEnvironment(Goal)) return;
            if (!Started)
            {
                // Do not alternate the same two failed side goals indefinitely.
                // A native reachable detour stays within local generated tiles.
                for (int Attempt = 0; Attempt < 2; ++Attempt)
                {
                    FVector Detour{};
                    if (NativeNavigation::RandomReachable(Here, 1200.f, Detour) &&
                        Syndicate::Finite(Detour) && Syndicate::Distance2D(Here,Detour) > 300.f &&
                        Syndicate::Distance2D(Detour,Zone->NextCenter) < Distance + (Pawn->bIsInAnyStorm ? 120.f : 400.f) &&
                        !RouteMemory.Blocked(Detour,N) && Move(Detour,180.f,true,true)) break;
                }
            }
        }
        else
        {
            if (N - LastProgress > (Pawn->bIsInAnyStorm ? 1.5f : 3.f) && ReachEnvironment(MoveGoal)) return;
            if (!Move(MoveGoal, 180.f, true, true)) { StopMove(); NextPath = 0.f; }
        }
    }
#include "BotDeviceActions.h"
#include "CombatSkills.h"
#include "BotGroundAI.h"
    bool PressureCover(bool Escape=false)
    {
        float N=Syndicate::Now();auto Target=Enemy.Get();
        if(RampActive || HighGroundStage || WindowCover.Get() || WindowPending.Get() || WallEdit.Active()) return false;
        if(!Escape && Target && !Target->IsDead() && CanFight() && N-LastSeenTime<12.f) return false;
        if((N-LastDamageTime>=3.f && N-LastShotCueAt>=1.f) || (!Target && N-LastSeenTime>=3.f) ||
           N<=NextBuild || MaterialCount()<10 || (Pawn->CurrentWeapon && Pawn->CurrentWeapon->bIsReloadingWeapon)) return false;
        DefensiveBuild();
        if(!QueuedBuild.Get()) return false;
        NextBuild=N+Syndicate::Between(.22f,.35f);return true;
    }
    bool WindowAttack()
    {
        auto Wall=WindowCover.Get();auto Target=Enemy.Get();float N=Syndicate::Now();
        if(!Wall || WallEdit.Active()) return false;
        SetADS(false);StopMove();
        if(!Target || Target->IsDead() || !CanFight() || Pawn->bIsInAnyStorm ||
           N-LastSeenTime>2.f || N>=WindowCloseAt)
        { SetFire(false);WindowCloseAt=N;return true; }
        auto Point=LastSeen+FVector(0,0,50.f);
        // Check the actual edited aperture. Ignoring this wall here would permit firing into its solid portions.
        auto Blocker=ShotObstacle(Point);
        if(Blocker && Blocker!=Target) { SetFire(false);WindowCloseAt=N;return true; }
        SimpleSwitchToWeapon(Syndicate::Distance2D(Pawn->K2_GetActorLocation(),LastSeen));
        auto W=Pawn->CurrentWeapon;
        if(!W || !IsGun(W->WeaponData)) { SetFire(false);WindowCloseAt=N;return true; }
        if(WindowWeapon.Get()!=W) { WindowWeapon.Set(W);WindowAmmo=W->AmmoCount; }
        else if(W->AmmoCount<WindowAmmo)
        { WindowFired=true;WindowAmmo=W->AmmoCount;WindowCloseAt=std::min(WindowCloseAt,N+.12f); }
        if(W->AmmoCount<=0 || W->bIsReloadingWeapon)
        { SetFire(false);WindowCloseAt=N;return true; } // Reload behind closed protection instead of wasting an exposed edit.
        AimError=FVector(0,0,50.f);Look(Point);
        auto Desired=UKismetMathLibrary::FindLookAtRotation(AimOrigin(),Point);
        SetFire(N>=ReactionReady && BotTactics::AimAligned(Desired,PC->GetControlRotation(),Pawn->K2_GetActorRotation()));
        return true;
    }
    bool AttackEnemyCover()
    {
        float N=Syndicate::Now();auto Target=Enemy.Get();
        if(!Target || Target->IsDead() || !CanFight() || N-LastSeenTime>6.f ||
           RampActive || HighGroundStage || WallEdit.Active() || WindowCover.Get() || WindowPending.Get() ||
           Pawn->bIsInAnyStorm || !AllowedDestination(LastSeen)) return false;
        auto Here=Pawn->K2_GetActorLocation();float D=Syndicate::Distance2D(Here,LastSeen);
        if(D>5000.f) return false;
        if(Enemy.Get()==PressureTarget.Get()&&N<PressureUntil&&D>1000.f)return false; // Close on the box before holding a spray lane.
        SimpleSwitchToWeapon(D);
        auto W=Pawn->CurrentWeapon;
        if(!W || !IsGun(W->WeaponData)) return false;
        auto Point=LastSeen+FVector(0,0,50.f);
        auto Cover=Syndicate::As<ABuildingSMActor>(ShotObstacle(Point));
        if(!Cover || !Cover->bPlayerPlaced || Cover->bDestroyed || Cover->IsActorBeingDestroyed() ||
           Cover->GetOwner()==PC || Cover->TeamIndex==PlayerState->TeamIndex || Cover->GetHealth()<=0.f) return false;
        if(EnemyCover.Get()==Cover && N<EnemyCoverRetryAt) return false;
        if(EnemyCover.Get()!=Cover)
        { EnemyCover.Set(Cover);EnemyCoverHealth=Cover->GetHealth();EnemyCoverProgressAt=N;EnemyCoverRetryAt=0.f; }
        if(Cover->GetHealth()+.5f<EnemyCoverHealth) EnemyCoverProgressAt=N;
        EnemyCoverHealth=Cover->GetHealth();
        if(N-EnemyCoverProgressAt>2.5f)
        { SetFire(false);EnemyCoverRetryAt=N+3.f;return false; } // A non-damaging firing lane yields to a real route.
        StopMove();Sprint(false);SetADS(Role(W->WeaponData)!=0 && D>700.f);
        AimError=FVector(0,0,50.f);Look(Point);
        if(W->AmmoCount<=0 || W->bIsReloadingWeapon)
        { SetFire(false);BotReload::Check(PC,W,NextReload);return true; }
        auto Desired=UKismetMathLibrary::FindLookAtRotation(AimOrigin(),Point);
        if(N<ReactionReady || !BotTactics::AimAligned(Desired,PC->GetControlRotation(),Pawn->K2_GetActorRotation()))
        { SetFire(false);return true; }
        // This is ordinary weapon input into the first opposing building hit by the real muzzle trace.
        // Native weapon collision, cadence, ammunition and building damage decide when it breaks.
        if(Firing && N>BurstEnd) { SetFire(false);NextBurst=N+.15f; }
        else if(!Firing && N>=NextBurst) { SetFire(true);BurstEnd=N+.65f; }
        return true;
    }
};
inline PlayerBot *Find(AFortPlayerPawnAthena *P)
{
    for (auto B : PlayerBotArray)
        if (B && B->Pawn == P)
            return B;
    return nullptr;
}
inline void ObserveCelebration(AFortPlayerStateAthena *Killer,AFortPlayerStateAthena *Victim)
{
    for(auto B:PlayerBotArray)if(B&&B->StateRef.Get()==Killer&&B->Valid()){B->QueueCelebration(Victim);break;}
}
inline void ObserveKillLoot(AFortPlayerStateAthena *Killer,const FVector &Point)
{
    for(auto B:PlayerBotArray)
        if(B && B->StateRef.Get()==Killer && B->Valid()) { B->RecordKillLoot(Point); break; }
}
inline void ObserveDamage(AFortPlayerPawnAthena *Victim,AController *Instigator,float Damage,const FVector &Momentum)
{
    if (!Victim || !Instigator || Damage<=0.f) return;
    auto Controller=Syndicate::Ref<AController>(Instigator).Get();
    auto Attacker=Controller ? Syndicate::As<AFortPlayerPawnAthena>(Controller->Pawn) : nullptr;
    for(auto Bot:PlayerBotArray)
        if(Bot&&Bot->PC==Controller&&Bot->Valid())
            if(auto W=Bot->Pawn->CurrentWeapon;W&&IsGun(W->WeaponData)&&Syndicate::Now()-W->LastFireTime<.6f)Bot->ConfirmPressureHit(Victim,Damage);
    for (auto Bot : PlayerBotArray)
        if (Bot && Bot->PawnRef.Get()==Victim && Bot->ControllerAlive())
        { Bot->CancelCelebration();Bot->RecordDamage(Attacker,Momentum); break; }
}
inline void ObserveStormDamage(AFortPlayerPawnAthena *Victim,AController *Instigator,float Damage,
                              const FGameplayTagContainer &Tags,AActor *Causer)
{
    if (!Victim || !Victim->bIsInAnyStorm || !std::isfinite(Damage) || Damage<=0.f) return;
    auto Cause=AFortPlayerStateAthena::ToDeathCause(Tags,Victim->bIsDBNO);
    auto Source=Syndicate::Ref<AActor>(Causer).Get();
    auto Name=Source ? Source->GetName() : std::string{};
    // Native storm damage may retain an earlier attacker's controller for kill
    // credit. Classify the damage, never require an empty instigator pointer.
    bool Storm=Cause==EDeathCause::OutsideSafeZone ||
        (!Instigator && (Name.contains("SafeZone") || Name.contains("Storm")));
    if (!Storm) return; // A bullet/fall while inside storm is not a storm event.
    for (auto B : PlayerBotArray)
        if (B && B->PawnRef.Get()==Victim && B->ControllerAlive())
        { B->StormRecovery.Damage(Syndicate::Now(),Damage,true); ++UnseenStormRecovery::StormEvents; break; }
}
inline void AfterStormRelocation(PlayerBot *B)
{
    // Abandon stale world-space work. Preserve every item, ammo count, resource,
    // cosmetic, health/shield value and native elimination state.
    float N=Syndicate::Now();
    B->SetFire(false); B->SetADS(false); B->StopMove(); B->PC->StopMovement();
    if (B->HealingStarted) B->CancelHeal();
    B->WallEdit.Clear(B->Pawn,B->PlayerState);
    B->QueuedBuild.Clear(); B->BuildingVisual=false; B->BuildingUntil=N;
    B->FightBuild.Clear(); B->HighGroundStage=0; B->HighGroundUntil=0.f;
    B->RampActive=B->RampEntering=B->RotationActive=false;
    B->TerrainMission.Clear(); B->TerrainDetourUntil=B->HeightExitUntil=0.f;
    B->LootTarget.Clear(); B->HarvestTarget.Clear(); B->BreachTarget.Clear();
    B->LootInteraction.Clear(); B->IgnoredLoot.Clear(); B->NextLootApproach=B->LootApproachUntil=0.f;
    B->PlayerCombat.Reset(); B->CombatHeightBudget={}; B->CombatPrediction={}; B->CombatShootUntil=0.f;
    B->Investigation={}; B->PursuitSearching=false;
    B->Enemy.Clear(); B->Threat.Clear(); B->PursuitUntil=0.f; B->LastSeenTime=-100.f;
    B->HeldCover.Clear(); B->WindowCover.Clear(); B->WindowPending.Clear();
    B->BoxActive=false; B->LeavingBox=false; B->DoorPass.Clear(); B->DoorPassUntil=0.f;
    B->Brain.Seeded=false; B->Navigation={}; B->RouteMemory={};
    B->MotionLocation=B->LastProgressLocation=B->Pawn->K2_GetActorLocation();
    B->LastMotionAt=B->MotionSampleAt=N; B->NextSense=B->NextLootSearch=B->NextPath=0.f;
    B->BotState=EBotState::Looting; B->AimReady=false; Release(B);
    if (B->HasGun()) B->SimpleSwitchToWeapon(); else B->EquipPickaxe();
}
inline void ObserveCoverDamage(ABuildingSMActor *Building,AController *Instigator,float Damage,const FVector &Momentum)
{
    if(!Building || !std::isfinite(Damage) || Damage<=0.f) return;
    for(auto B:PlayerBotArray)
        if(B && B->ControllerAlive() && B->PawnRef.Get() && B->BotState==EBotState::Healing)
            for(int I=0;I<6;++I)
                if(B->BoxWalls[I].Get()==Building)
                {
                    B->BoxDamageAt=Syndicate::Now(); B->CancelHeal();
                    B->BoxActive=false; B->BotState=EBotState::Combat; B->NextBuild=0.f;
                    auto Attacker=Instigator ? Syndicate::As<AFortPlayerPawnAthena>(Instigator->Pawn) : nullptr;
                    if(Attacker) B->RecordDamage(Attacker,Momentum);
                    return;
                }
}
inline void LogStormDeath(PlayerBot *B,AFortGameStateAthena *GS)
{
    if(!B || B->StormDeathLogged) return;
    auto P=B->PawnRef.Get();
    if(!P || (!P->bIsInAnyStorm && B->PlayerState->DeathInfo.DeathCause!=EDeathCause::OutsideSafeZone)) return;
    B->StormDeathLogged=true;
    auto Here=P->K2_GetActorLocation(); auto Zone=GS->SafeZoneIndicator; float N=Syndicate::Now();
    auto Reason=B->MaterialCount()<10 ? "no usable building materials" :
        (B->RampActive ? "stair entry/climb incomplete" :
         (B->BreachTarget.Get() ? "obstruction breach incomplete" :
          (B->Navigation.Stalled(N,10.f) ? "zone route stalled" : "storm travel outlasted remaining health")));
    Log(std::format("Storm death diagnostic: bot={} reason={} state={} health={} materials={} position={},{},{} goal={},{},{} zoneDistance={} nextRadius={} progressAge={} ramp={} ascentSteps={} routeRecoveries={} terrainStage={} terrainProgressAge={} terrainFailures={}",
        B->PC->GetName(),Reason,static_cast<int>(B->BotState),P->GetHealth(),B->MaterialCount(),Here.X,Here.Y,Here.Z,
        B->MoveGoal.X,B->MoveGoal.Y,B->MoveGoal.Z,Zone?Syndicate::Distance2D(Here,Zone->NextCenter):0.f,
        Zone?Zone->NextRadius:0.f,N-B->Navigation.At,B->RampActive,B->AscentSteps,B->RouteRecoveries,
        static_cast<int>(B->TerrainMission.Step),N-B->TerrainMission.ProgressAt,B->TerrainMission.Failures));
}
inline void PhaseEvent(UObject *Object, UFunction *Function, void *Parameters, bool Before)
{
    if(Before && Function && Parameters)
    {
        if(Function==NativeNoiseFunction)
        {
            auto Event=static_cast<SDK::Params::FortAIFunctionLibrary_MakeNoiseEvent *>(Parameters);
            if(auto Source=Syndicate::Ref<AActor>(Event->NoiseMaker).Get()) ObserveSound(Source,Source->K2_GetActorLocation(),Event->MaxRange);
            return;
        }
        if(Function==NativeNoiseLocationFunction)
        {
            auto Event=static_cast<SDK::Params::FortAIFunctionLibrary_MakeNoiseEventAtLocation *>(Parameters);
            ObserveSound(Event->NoiseMaker,Event->NoiseLocation,Event->MaxRange); return;
        }
        if(Function==NativeReportNoiseFunction)
        {
            auto Event=static_cast<SDK::Params::AISense_Hearing_ReportNoiseEvent *>(Parameters);
            if(std::isfinite(Event->Loudness) && Event->Loudness>0.f)
                ObserveSound(Event->Instigator,Event->NoiseLocation,(Event->MaxRange>0.f ? Event->MaxRange : 3500.f)*std::min(Event->Loudness,1.f));
            return;
        }
    }
    if(Before && Function && Function==NativePerceptionFunction && Parameters)
    {
        auto Controller=Syndicate::As<AFortAthenaAIBotController>(Object);
        auto Event=static_cast<SDK::Params::FortAthenaAIBotController_OnPerceptionSensed *>(Parameters);
        for(auto B:PlayerBotArray) if(B && B->PC==Controller && B->Valid())
        { B->Hear(Event->SourceActor,Event->Stim); break; }
        return;
    }
    if (!NativePhaseFunction || Function != NativePhaseFunction || !Parameters)
        return;
    auto Controller = Syndicate::As<AFortAthenaAIBotController>(Object);
    if (!Controller)
        return;
    auto Phase = *static_cast<EAthenaGamePhase *>(Parameters);
    for (auto B : PlayerBotArray)
        if (B && B->PC == Controller && B->ControllerAlive())
        {
            if (Before)
                B->PreparePhase(Phase);
            else
                B->StopNative();
            break;
        }
}
inline void ResetWorld()
{
    BotDifficulty::Reset();
    UnseenStormRecovery::Reset();
    for (auto B : PlayerBotArray)
        delete B;
    PlayerBotArray.clear();
    Reservations.clear();
    RecentBuilds.clear();
    RecentSounds.clear();RecentShots.clear();
    NativeSoundCount=NativeHearingCount=InvestigationsStarted=0;
    BotSpatial::Reset();
    PickupCache.clear();
    ContainerCache.clear();
    PawnCache.clear();
    ResourceCache.clear();
    FoundationCache.clear();
    POICache.clear();
    BotLanding::Reset();
    NextRegionScan=0.f;
    NextResourceScan = 0;
    NextContainerScan = 0;
    NextScan = 0;
    LastTick = -1;
    NextReport = 0;
}
class BotsBTService_InventoryManager
{
  public:
    void CompletePickupAnimation(PlayerBot *B, AFortPickup *P)
    {
        if (B && B->Valid())
            B->FinishPickup(P);
    }
};
struct FootstepEmitter { Syndicate::Ref<AFortPlayerPawnAthena> Pawn; BotTactics::Footfall Motion; Syndicate::Ref<AFortWeapon> Weapon; BotTactics::ShotEvidence Shots; };
inline std::vector<FootstepEmitter> Footsteps;
#include "BotFireInput.h"
inline float NextFootsteps=0.f;
inline UWorld *FootstepWorld=nullptr;
inline void ReportFootsteps(float N)
{
    if(FootstepWorld!=UWorld::GetWorld()) { Footsteps.clear();NextFootsteps=0.f;FootstepWorld=UWorld::GetWorld(); }
    if(N<NextFootsteps) return;NextFootsteps=N+.2f;
    std::erase_if(Footsteps,[](const auto &E){auto P=E.Pawn.Get();return !P || P->IsDead() || P->IsActorBeingDestroyed();});
    for(auto Ref:PawnCache)
        if(auto P=Ref.Get(); P && !P->IsDead() && !P->IsActorBeingDestroyed() && P->CharacterMovement)
        {
            auto State=Syndicate::As<AFortPlayerStateAthena>(P->PlayerState);
            if(!State || State->IsPlayerDead() || State->bInAircraft) continue;
            auto I=std::find_if(Footsteps.begin(),Footsteps.end(),[P](const auto &E){return E.Pawn.Get()==P;});
            if(I==Footsteps.end()) { if(Footsteps.size()>=128) continue;Footsteps.push_back({{P},{}});I=std::prev(Footsteps.end()); }
            auto Weapon=P->CurrentWeapon;
            if(Weapon && IsGun(Weapon->WeaponData))
            {
                if(I->Weapon.Get()!=Weapon) { I->Weapon.Set(Weapon);I->Shots={}; }
                auto Evidence=I->Shots.Sample(Weapon->LastFireTime,Weapon->AmmoCount,Weapon->bIsReloadingWeapon);
                if(Evidence!=BotTactics::ShotEvidence::Kind::None)
                {
                    FVector Start=Weapon->CurrentDamageStartLocation,Direction=Weapon->CurrentAdjustedAimDirection;
                    if(Evidence==BotTactics::ShotEvidence::Kind::ConsumedRound)
                    {
                        FRotator Eyes{};P->GetActorEyesViewPoint(&Start,&Eyes);
                        Direction=UKismetMathLibrary::GetForwardVector(P->GetBaseAimRotation());
                        // Human client-owned weapons do not necessarily update these server firing fields.
                        // Actual loaded-round consumption gives a gunshot/replicated-aim bearing, not an invented bullet hit.
                    }
                    if(Syndicate::Finite(Start) && Syndicate::Finite(Direction) &&
                       Syndicate::Distance2D(Start,P->K2_GetActorLocation())<300.f)
                    {
                        std::erase_if(RecentShots,[N](const auto &E){return !E.Source.Get() || N-E.Time>1.f;});
                        if(RecentShots.size()>=64) RecentShots.erase(RecentShots.begin());
                        RecentShots.push_back({{P},Start,Direction,N});ObserveSound(P,Start,3500.f);
                    }
                }
            }
            auto Velocity=P->GetVelocity();float Speed=std::hypot(Velocity.X,Velocity.Y);
            auto Here=P->K2_GetActorLocation();
            bool Ground=(P->CharacterMovement->MovementMode==EMovementMode::MOVE_Walking || P->CharacterMovement->MovementMode==EMovementMode::MOVE_NavWalking) && !P->bIsSkydiving && !P->bIsParachuteOpen;
            if(I->Motion.Sample(Here,N,Ground,Speed,P->bIsCrouched))
            {
                float Range=P->bIsCrouched ? 500.f : (Speed>450.f ? 2500.f : 1500.f);
                UAISense_Hearing::ReportNoiseEvent(UWorld::GetWorld(),Here,1.f,P,Range,{});
            }
        }
}
inline void TickBots(float DeltaSeconds = 1.f / 30.f)
{
    auto W = UWorld::GetWorld();
    if (!W)
        return;
    if (ActiveWorld != W)
    {
        ResetWorld();
        ActiveWorld = W;
    }
    float N = Syndicate::Now();
    if (N == LastTick)
        return;
    LastTick = N;
    BotWork::Scope FrameMeasure(BotWork::Frame);
    ScanWorld();
    auto GM = Syndicate::As<AFortGameModeAthena>(W->AuthorityGameMode);
    auto GS = Syndicate::As<AFortGameStateAthena>(W->GameState);
    if (!GM || !GS)
        return;
    if (N >= NextReport)
    {
        BotWork::Report();
        NextReport = N + 5.f;
        int Choices[5]{}, States[static_cast<int>(EBotState::MAX)]{}, Armed = 0, Pawnless = 0, Airborne = 0, Local = 0, Moving = 0,
            Emoting = 0, Shooting = 0, ADS = 0, NoCollision = 0, Boarded = 0, Departed = 0, MissingBB = 0, Passengers = 0, Thanked = 0, Goals = 0, Stalled = 0, Harvesting = 0, GunFire = 0, Recoveries = 0;
        unsigned int HeightTasks=0,HeightExits=0,HeightCrests=0,HeightRejected=0,HeightStalls=0,LootFailures=0;
        unsigned int CombatActions[6]{},Transfers=0,PickupWaits=0,SearchHolds=0;
        unsigned int GroundGoals[8]{},BuildWaits=0,MissionRecoveries=0,PhysicalStalls=0,ChestClaims=0,HeightRetakes=0,PlanRejects=0;
        unsigned int ClimbSteps[9]{},ClimbReasons[12]{},CombatDry=0,CombatPoor=0,TurnWaits=0;
        for (auto B : PlayerBotArray)
            if (B && B->ControllerAlive())
            {
                MissingBB += !B->PC->Blackboard;
                if (auto State = B->StateRef.Get())
                {
                    Passengers += State->bInAircraft;
                    Thanked += State->bThankedBusDriver;
                }
                Boarded += B->AircraftBoarded;
                Departed += B->NativeExitAccepted;
                if (!B->RefreshPawn()) { ++Pawnless; continue; }
                ++Choices[static_cast<int>(B->WarmupChoice)];
                ++States[static_cast<int>(B->BotState)];
                Armed += B->HasGun();
                Airborne += B->Pawn->bIsSkydiving || B->Pawn->bIsParachuteOpen;
                Local += B->LocalSteering;
                Goals += B->HasGoal;
                Stalled += B->HasGoal && N - B->LastMotionAt > 2.f;
                Harvesting += B->HarvestTarget.Get() != nullptr;
                GunFire += B->Firing && B->Pawn->CurrentWeapon && IsGun(B->Pawn->CurrentWeapon->WeaponData);
                Recoveries += B->RouteRecoveries;
                HeightTasks+=B->FightBuild.Active() || B->HighGroundStage || (B->RampActive && N<B->HighGroundUntil);
                HeightExits+=N<B->HeightExitUntil;HeightCrests+=B->StairCrests;
                if(B->Brain.Seeded && !B->Pawn->IsDead())
                {
                    ++GroundGoals[static_cast<unsigned int>(B->Brain.Intent)];
                    BuildWaits+=B->FightBuild.Waiting;MissionRecoveries+=B->Brain.Recoveries;
                    PhysicalStalls+=B->HasGoal && B->Navigation.Stalled(N,3.f);
                    if(B->FightBuild.Active()) ++ClimbSteps[static_cast<unsigned int>(B->FightBuild.Step)];
                    if(B->Brain.Intent==BotBrain::Goal::Fight)
                    {
                        CombatDry+=!B->CanFight();CombatPoor+=B->MaterialCount()<20;
                        if(B->FightFailureReason>0 && B->FightFailureReason<12) ++ClimbReasons[B->FightFailureReason];
                    }
                    TurnWaits+=B->QueuedBuild.Get() && N>=B->BuildPlaceAt;
                }
                HeightRejected+=B->HeightBuildRejects;HeightStalls+=B->HeightClimbAborts;LootFailures+=B->LootRouteFailures;
                ChestClaims+=B->ChestOutputClaims;HeightRetakes+=B->FightRetakes;PlanRejects+=B->FightPlanRejects;
                Transfers+=B->LootTransfers;PickupWaits+=B->WaitingForPickup();SearchHolds+=B->LootInteraction.Target!=0;
                if(B->Brain.Intent==BotBrain::Goal::Fight) ++CombatActions[static_cast<unsigned int>(B->PlayerCombat.Intent)];
                Emoting += B->Pawn->bIsPlayingEmote;
                Shooting += B->Firing;
                ADS += B->Pawn->bIsTargeting;
                NoCollision += !B->Pawn->GetActorEnableCollision() ||
                    !B->Pawn->CapsuleComponent || B->Pawn->CapsuleComponent->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
                auto Velocity = B->Pawn->GetVelocity();
                Moving += std::hypot(Velocity.X, Velocity.Y) > 30.f;
            }
        auto Native = GS->GetPlayersLeft();
        Log(std::format("Storm recovery: damageEvents={} relocated={} viewBlocks={} noSafeDestination={} queued={}",
            UnseenStormRecovery::StormEvents,UnseenStormRecovery::Relocations,UnseenStormRecovery::VisibleBlocks,
            UnseenStormRecovery::NoDestination,UnseenStormRecovery::Waiting.size()));
        Log(std::format("AI status: phase={} target={} wrappers={} nativeHumans={} nativeBots={} nativeTotal={} "
                        "moving={} airborne={} pawnless={} boarded={} departed={} missingBlackboard={} passengers={} thanked={} localSteering={} emoting={} firing={} ads={} collisionDisabled={} "
                        "goals={} stalled={} harvesting={} gunFire={} routeRecoveries={} roles[idle/emote/watch/roam/fight]={}/{}/{}/{}/{} armed={} match[loot/combat/heal/zone]={}/{}/{}/{}",
                        static_cast<int>(GS->GamePhase), BotPopulation::Target, PlayerBotArray.size(),
                        Native.Humans, Native.Bots, Native.Total, Moving, Airborne, Pawnless, Boarded, Departed, MissingBB, Passengers, Thanked, Local,
                        Emoting, Shooting, ADS, NoCollision, Goals, Stalled, Harvesting, GunFire, Recoveries, Choices[0], Choices[1], Choices[2], Choices[3], Choices[4], Armed,
                        States[static_cast<int>(EBotState::Looting)], States[static_cast<int>(EBotState::Combat)],
                        States[static_cast<int>(EBotState::Healing)], States[static_cast<int>(EBotState::MovingToSafeZone)]));
        Log(std::format("Ground probes: accepted={} missingFloor={} steep={} ledge={} obstacle={} nativeMain={}",
                        BotPhysics::ProbeAccepted, BotPhysics::ProbeMissingFloor, BotPhysics::ProbeSteepFloor,
                        BotPhysics::ProbeLedge, BotPhysics::ProbeObstacle,
                        NativeNavigation::Data() && UWorld::GetWorld()->NavigationSystem &&
                        Cast<UFortNavSystem>(UWorld::GetWorld()->NavigationSystem)->MainNavData != nullptr));
        Log(std::format("AI task diagnostics: heightTasks={} outdoorExits={} completedHeightStairs={} heightPlacementRejects={} heightClimbStalls={} expiredLootRoutes={}",
            HeightTasks,HeightExits,HeightCrests,HeightRejected,HeightStalls,LootFailures));
        Log(std::format("Chest/height actions: actualChestPickupClaims={} continuedCombatStories={} rejectedClimbPlans={}",ChestClaims,HeightRetakes,PlanRejects));
        Log(std::format("Combat flow: climb[platform/center/floor/side/rear/stair/entry/crest]={}/{}/{}/{}/{}/{}/{}/{} turnWaits={} dryFighters={} lowMaterialFighters={} reason[headroom/plan/stale/commit/staging/floor/stair/travel/continuation/jump/state]={}/{}/{}/{}/{}/{}/{}/{}/{}/{}/{}",
            ClimbSteps[1],ClimbSteps[2],ClimbSteps[3],ClimbSteps[4],ClimbSteps[5],ClimbSteps[6],ClimbSteps[7],ClimbSteps[8],TurnWaits,CombatDry,CombatPoor,
            ClimbReasons[1],ClimbReasons[2],ClimbReasons[3],ClimbReasons[4],ClimbReasons[5],ClimbReasons[6],ClimbReasons[7],ClimbReasons[8],ClimbReasons[9],ClimbReasons[10],ClimbReasons[11]));
        Log(std::format("Ground brain: arm/resupply/fight/heal/gather/loot/investigate/rotate={}/{}/{}/{}/{}/{}/{}/{} pendingPieces={} recoveryAttempts={} physicallyStalled={}",
            GroundGoals[0],GroundGoals[1],GroundGoals[2],GroundGoals[3],GroundGoals[4],GroundGoals[5],GroundGoals[6],GroundGoals[7],
            BuildWaits,MissionRecoveries,PhysicalStalls));
        Log(std::format("Reference actions: search/approach/climb/peek/reload/escapeAmmo={}/{}/{}/{}/{}/{} nativeLootTransfers={} pickupFlights={} physicalSearchHolds={}",
            CombatActions[0],CombatActions[1],CombatActions[2],CombatActions[3],CombatActions[4],CombatActions[5],Transfers,PickupWaits,SearchHolds));
        if (BotPhysics::ProbeObstacle)
        {
            auto Obstacle = BotPhysics::LastObstacle.Get();
            auto BlockedPawn = BotPhysics::LastBlockedPawn.Get();
            auto N = BotPhysics::LastObstacleNormal;
            auto S = BotPhysics::LastProbeStart, E = BotPhysics::LastProbeEnd;
            Log(std::format("Ground blocker: pawn={} actor={} penetrating={} normal={},{},{} start={},{},{} end={},{},{}",
                            BlockedPawn ? BlockedPawn->GetName() : "expired",
                            Obstacle ? Obstacle->GetName() : "none/expired", BotPhysics::LastProbePenetrating,
                            N.X, N.Y, N.Z, S.X, S.Y, S.Z, E.X, E.Y, E.Z));
        }
        BotPhysics::ProbeAccepted = BotPhysics::ProbeMissingFloor = BotPhysics::ProbeSteepFloor =
            BotPhysics::ProbeLedge = BotPhysics::ProbeObstacle = 0;
    }
    ReportFootsteps(N);
    for (int i = static_cast<int>(PlayerBotArray.size()) - 1; i >= 0; --i)
    {
        auto B = PlayerBotArray[i];
        if (!B || !B->ControllerAlive())
        {
            Log("Bot AI controller left the native object registry; decision wrapper released.");
            Release(B);
            delete B;
            PlayerBotArray.erase(PlayerBotArray.begin() + i);
            continue;
        }
        B->ObservePhase(GS->GamePhase);
        if (GS->GamePhase == EAthenaGamePhase::Aircraft && !B->NativeExitAccepted)
            B->JumpFromBus(GS->GetAircraft(0)); // Boarding intentionally has no pawn until native departure.
        if (GS->GamePhase > EAthenaGamePhase::Warmup && B->StateRef.Get() && B->PlayerState->IsPlayerDead())
        {
            LogStormDeath(B,GS);
            if (auto DeadPawn = B->PawnRef.Get())
                Elimination::Finish(DeadPawn);
            for (int j = GM->AliveBots.Num() - 1; j >= 0; --j)
                if (GM->AliveBots[j] == B->PC)
                    GM->AliveBots.Remove(j);
            Release(B);
            delete B;
            PlayerBotArray.erase(PlayerBotArray.begin() + i);
            BotPopulation::Reconcile(GM, GS);
            continue;
        }
        if (!B->RefreshPawn())
            continue; // Native bus boarding can intentionally unpossess/destroy the warmup pawn.
        B->Delta = std::clamp(DeltaSeconds, 0.f, 0.1f);
        ++B->tick_counter;
        if (B->Pawn->IsDead() || B->PlayerState->IsPlayerDead())
        {
            if (GS->GamePhase <= EAthenaGamePhase::Warmup)
            {
                B->SetFire(false);
                B->SetADS(false);
                B->StopMove();
                continue; // Native warmup respawn/aircraft phase owns the replacement pawn.
            }
            // Native death effects stay in the engine. Only remove our decision wrapper and stale alive membership.
            LogStormDeath(B,GS);
            Elimination::Finish(B->Pawn);
            B->SetFire(false);
            B->SetADS(false);
            B->StopMove();
            B->StopNative();
            Release(B);
            for (int j = GM->AliveBots.Num() - 1; j >= 0; --j)
                if (GM->AliveBots[j] == B->PC)
                    GM->AliveBots.Remove(j);
            BotPopulation::Reconcile(GM, GS);
            delete B;
            PlayerBotArray.erase(PlayerBotArray.begin() + i);
            continue;
        }
        if (B->Pawn->bPressedJump && N >= B->JumpReleaseAt)
        {
            B->Pawn->StopJumping();
        }
        if (!B->WaitingForPickup() && UnseenStormRecovery::Try(B->Pawn,GS,B->StormRecovery,PawnCache))
        {
            AfterStormRelocation(B);
            continue;
        }
        if (GS->GamePhase == EAthenaGamePhase::Warmup)
        {
            B->BotState = EBotState::Warmup;
            if (N >= B->NextPhysicsCheck)
            {
                B->NextPhysicsCheck = N + 2.f;
                BotPhysics::MaintainGroundPawn(B->Pawn);
            }
            B->WarmupTick();
        }
        else if (GS->GamePhase >= EAthenaGamePhase::EndGame)
        {
            B->SetFire(false);
            B->SetADS(false);
            B->StopMove();
        }
        else if (!B->bHasJumpedFromBus || B->Pawn->bIsSkydiving || B->Pawn->bIsParachuteOpen ||
                 B->BotState == EBotState::Skydiving || B->BotState == EBotState::Gliding)
        {
            B->SetFire(false);
            B->SetADS(false);
            B->AirTick();
        }
        else if (!B->Pawn->bIsDBNO)
            B->GroundTick();
        B->UpdatePosture();
        if (GS->GamePhase == EAthenaGamePhase::Warmup || B->bHasJumpedFromBus)
            B->SteeringTick();
        if (!B->Pawn->bIsSkydiving && !B->Pawn->bIsParachuteOpen &&
            (GS->GamePhase == EAthenaGamePhase::Warmup || B->bHasJumpedFromBus))
            B->UpdateGroundView();
    }
}
} // namespace PlayerBots
