#pragma once
#include "FortInventory.h"
#include "VehiclePolicy.h"
#include "SDK/SDK/FortniteGame_parameters.hpp"

namespace PlayerVehicles
{
struct Rider
{
    Syndicate::Ref<AFortPlayerPawnAthena> Pawn;
    Syndicate::Ref<AFortAthenaVehicle> Vehicle;
    FGuid InventoryGuid{};
    int Seat=-1, Attempts=0;
    float NextEquip=0.f;
};
inline std::vector<Rider> Riders;
inline Syndicate::Ref<UWorld> World;
inline float NextTick=0.f;
struct MoveObservation { Syndicate::Ref<AFortAthenaVehicle> Vehicle; FVector Before; float Time; };
inline std::vector<MoveObservation> Moving;
struct MoveClock
{
    Syndicate::Ref<AFortAthenaVehicle> Vehicle;Syndicate::Ref<AFortPlayerPawnAthena> Driver;
    float Accepted=0.f,Entry=0.f,ReceivedAt=0.f;
    FVector Position{},Velocity{},Angular{};FQuat Rotation{};bool HasPose=false;
    FVector Origin{};
};
inline std::vector<MoveClock> MoveClocks;
inline UFunction *MoveEvent=nullptr;
inline UFunction *ExitEvent=nullptr;
inline UFunction *PawnExitEvent=nullptr;
struct ExitRequest
{
    Syndicate::Ref<AFortPlayerControllerAthena> PC;
    Syndicate::Ref<AFortPlayerPawnAthena> Pawn;
    Syndicate::Ref<AFortAthenaVehicle> Vehicle;
    float At=0.f,Until=0.f,NativeAt=0.f,Entry=0.f;
    FName Socket{};
    bool HasSocket=false,Retried=false;
};
inline std::vector<ExitRequest> Exits;
inline AFortPlayerPawnAthena *PawnFor(AFortPlayerController *PC)
{
    if(!PC) return nullptr;
    if(auto P=Cast<AFortPlayerPawnAthena>(PC->Pawn)) return P;
    auto V=Cast<AFortAthenaVehicle>(PC->Pawn);
    auto P=Cast<AFortPlayerPawnAthena>(PC->MyFortPawn);
    // Some seats possess the physics pawn. MyFortPawn is only accepted when
    // the real native driver/vehicle relationship confirms the character.
    return V && P && V->GetDriver()==P && P->GetVehicle()==V ? P : nullptr;
}

inline UFortVehicleSeatWeaponComponent *Mount(AFortPlayerPawnAthena *P,AFortAthenaVehicle *V)
{
    if(!P||!V) return nullptr;
    int Seat=V->FindSeatIndex(P);
    return Seat>=0 ? V->GetSeatWeaponComponent(Seat) : nullptr;
}
inline FWeaponSeatDefinition *SeatWeapon(UFortVehicleSeatWeaponComponent *C,int Seat)
{
    if(C) for(auto &D:C->WeaponSeatDefinitions)
        if(D.SeatIndex==Seat && (D.VehicleWeaponOverride || D.VehicleWeapon)) return &D;
    return nullptr;
}
inline bool CanUseInventory(AFortPlayerPawnAthena *P)
{
    if(!P) return false;
    auto V=P->GetVehicle();
    bool Seated=P->IsInVehicle() || P->VehicleStateRep.Vehicle;
    if(!Seated) return true;
    if(!V) return false; // A seat transition must not equip a pickaxe over its mounted gun.
    int Seat=V->FindSeatIndex(P);
    bool Mounted=SeatWeapon(Mount(P,V),Seat)!=nullptr;
    return VehiclePolicy::InventoryAllowed(true,Mounted,P->CanShootFromVehicle(),P->IsDrivingVehicle(),P->CanUseInventoryAsDriver());
}
inline void Reset()
{ Riders.clear();Moving.clear();MoveClocks.clear();Exits.clear();World.Clear();NextTick=0.f; }
inline bool ServerWorld()
{auto W=UWorld::GetWorld();return W&&W->AuthorityGameMode&&W->NetDriver&&!W->NetDriver->ServerConnection;}
inline bool ApplyVehiclePose(AFortAthenaVehicle *V,const MoveClock &Pose)
{
    auto Root=Cast<UPrimitiveComponent>(V->RootComponent);if(!Root) return false;
    auto Rotation=UKismetMathLibrary::Quat_Rotator(Pose.Rotation);FHitResult Hit{};
    Root->K2_SetWorldLocationAndRotation(Pose.Position,Rotation,false,&Hit,true);
    Root->SetPhysicsLinearVelocity(Pose.Velocity,false,FName());
    Root->SetPhysicsAngularVelocityInDegrees(Pose.Angular,false,FName());
    V->ReplicatedMovement.Location=Pose.Position;V->ReplicatedMovement.Rotation=Rotation;
    V->ReplicatedMovement.LinearVelocity=Pose.Velocity;V->ReplicatedMovement.AngularVelocity=Pose.Angular;
    V->ForceNetUpdate();return true;
}
inline const MoveClock *LatestPose(AFortAthenaVehicle *V,AFortPlayerPawnAthena *P)
{
    const MoveClock *Best=nullptr;
    for(const auto &M:MoveClocks)
        if(M.Vehicle.Get()==V&&M.Driver.Get()==P&&M.HasPose&&
           Syndicate::Finite(M.Position)&&Syndicate::Finite(M.Origin))
        {
            if(M.Entry==P->VehicleStateRep.EntryTime) return &M;
            if(!Best||M.ReceivedAt>Best->ReceivedAt) Best=&M;
        }
    return Best;
}
inline bool StaleEntryPose(const MoveClock &M,const FVector &Here)
{
    // A pose still sitting at the boarding spot must never pull a vehicle that
    // the server has already moved away from back to the entry location.
    return (M.Position-M.Origin).Magnitude()<=500.f&&(Here-M.Origin).Magnitude()>1000.f;
}
inline void RefreshExitPose(AFortAthenaVehicle *V,AFortPlayerPawnAthena *P)
{
    auto M=LatestPose(V,P);
    if(!M) return;
    float Age=Syndicate::Now()-M->ReceivedAt;
    float Speed=std::hypot(std::hypot(M->Velocity.X,M->Velocity.Y),M->Velocity.Z);
    auto Here=V->K2_GetActorLocation();
    if(Age<0.f||Age>.25f||!Syndicate::Finite(Here)||StaleEntryPose(*M,Here)) return;
    if((Here-M->Position).Magnitude()>800.f+Speed*Age*1.5f)
        ApplyVehiclePose(V,*M); // Last validated current-driver pose, never a stale entry/re-entry sample.
}
inline void ObserveExit(UObject *Object,UFunction *Function,void *Parameters,bool Before)
{
    if(!Function||!ServerWorld()) return;
    if(Before && ExitEvent && Function->Name==ExitEvent->Name)
    {
        auto PC=Cast<AFortPlayerControllerAthena>(Object);auto P=PawnFor(PC);
        auto V=P ? P->GetVehicle() : nullptr;
        if(!PC||!P||!V||P->IsDead()||V->bPendingDeath||V->FindSeatIndex(P)<0) return;
        RefreshExitPose(V,P);
        // Capture BEFORE native exit clears the vehicle/possession relationship.
        // Repeated hold-exit RPCs must not renew the same stalled deadline.
        auto I=std::find_if(Exits.begin(),Exits.end(),[P](const auto &E){return E.Pawn.Get()==P;});
        if(I!=Exits.end()&&I->Vehicle.Get()==V&&I->Entry==P->VehicleStateRep.EntryTime) return;
        std::erase_if(Exits,[P](const auto &E){return !E.Pawn.Get()||E.Pawn.Get()==P;});
        float N=Syndicate::Now();ExitRequest E;E.PC.Set(PC);E.Pawn.Set(P);E.Vehicle.Set(V);
        E.At=N+.75f;E.Until=N+2.f;E.Entry=P->VehicleStateRep.EntryTime;Exits.push_back(E);
    }
    if(Before && PawnExitEvent && Function->Name==PawnExitEvent->Name && Parameters)
    {
        auto V=Cast<AFortAthenaVehicle>(Object);
        auto Args=static_cast<SDK::Params::FortAthenaVehicle_OnPawnExitVehicle*>(Parameters);
        auto P=Cast<AFortPlayerPawnAthena>(Args->PlayerPawn);
        for(auto &E:Exits) if(E.Pawn.Get()==P && E.Vehicle.Get()==V)
        {E.Socket=Args->ExitSocketName;E.HasSocket=true;E.NativeAt=Syndicate::Now();break;}
    }
}
inline bool RepairExit(const ExitRequest &E)
{
    auto PC=E.PC.Get();auto P=E.Pawn.Get();auto V=E.Vehicle.Get();
    if(!ServerWorld()||!PC||!P||!V||P->IsDead()||PC->Pawn!=P||P->Controller!=PC||
       P->GetVehicle()||P->IsInVehicle()||P->VehicleStateRep.Vehicle||!E.HasSocket) return false;
    USceneComponent *Mesh=nullptr;
    if(auto SK=Cast<AFortAthenaSKVehicle>(V)) Mesh=SK->SkeletalMesh;
    if(!Mesh) Mesh=Cast<USceneComponent>(V->RootComponent);
    if(!Mesh||!Mesh->DoesSocketExist(E.Socket)) return false;
    auto Pose=LatestPose(V,P);
    if(!Pose) return false;
    float PoseAge=Syndicate::Now()-Pose->ReceivedAt;
    auto Here=P->K2_GetActorLocation();
    if(PoseAge<0.f||PoseAge>1.5f||!Syndicate::Finite(Here)) return false;
    // If the pose is still at the boarding spot but the server vehicle has
    // already moved, the pose is stale: use the server's current transform and
    // never pull the vehicle back to the entry. Conversely, if both are still
    // at the boarding spot and the pawn is elsewhere, there is no reliable
    // current pose to repair from.
    auto Current=V->K2_GetActorLocation();
    if(!Syndicate::Finite(Current)) return false;
    if(!StaleEntryPose(*Pose,Current))
    {
        if(StaleEntryPose(*Pose,Here)) return false;
        float Speed=std::hypot(std::hypot(Pose->Velocity.X,Pose->Velocity.Y),Pose->Velocity.Z);
        if((Current-Pose->Position).Magnitude()>800.f+Speed*PoseAge*1.5f) ApplyVehiclePose(V,*Pose);
    }
    auto At=Mesh->GetSocketLocation(E.Socket);
    if(!VehiclePolicy::StaleExit(Here,At,V->K2_GetActorLocation(),V->GetVelocity(),Syndicate::Now()-E.NativeAt)) return false;
    // Use the exit socket selected by the native eject, never an invented
    // offset or the old entry position. Native teleport performs collision
    // handling and player movement/prediction resynchronization together.
    UFortMissionLibrary::TeleportPlayerPawn(P,At,P->K2_GetActorRotation(),false,false);
    if((P->K2_GetActorLocation()-At).Magnitude()>200.f) return false;
    P->ForceNetUpdate();V->ForceNetUpdate();
    Log("Reconciled stale native vehicle exit at selected socket: "+V->GetName());
    return true;
}
inline void TickExits(float N)
{
    // Native eject reenters ProcessEvent. Never keep erase_if iterators or
    // references into Exits across that callback.
    auto Pending=Exits;
    for(auto E:Pending)
    {
        auto PC=E.PC.Get();auto P=E.Pawn.Get();auto V=E.Vehicle.Get();
        bool Remove=!PC||!P||!V||P->IsDead()||N>E.Until;
        if(!Remove && P->GetVehicle()==V && P->VehicleStateRep.EntryTime!=E.Entry) Remove=true;
        // Native unpossession/possession can span this observation. Retain the
        // bounded request until the controller has its character back.
        if(!Remove && PawnFor(PC)!=P)
        {
            if(PC->Pawn==V||PC->MyFortPawn==P) continue;
            Remove=true;
        }
        if(!Remove && P->GetVehicle()!=V)
        {
            if(PC->Pawn==P&&P->Controller!=PC&&!P->GetVehicle()) continue;
            // The native exit can clear its vehicle pointer before the seat
            // flags. Keep the short-lived request until that transition ends.
            if(!P->GetVehicle()&&(P->IsInVehicle()||P->VehicleStateRep.Vehicle==V)) continue;
            Remove=true;
            if(!P->GetVehicle()&&!P->IsInVehicle()) RepairExit(E);
        }
        if(Remove)
        {std::erase_if(Exits,[&E](const auto &R){return R.Pawn.Get()==E.Pawn.Get()&&R.Vehicle.Get()==E.Vehicle.Get()&&R.Entry==E.Entry;});continue;}
        if(N<E.At||E.Retried) continue;
        for(auto &R:Exits) if(R.Pawn.Get()==P&&R.Vehicle.Get()==V) R.Retried=true;
        if(V->FindSeatIndex(P)>=0 && V->EjectPlayerPawn(P)) Log("Native vehicle exit completed after stalled request: "+V->GetName());
    }
}
inline void ObserveMove(UObject *Object,UFunction *Function,void *Parameters,bool Before)
{
    ObserveExit(Object,Function,Parameters,Before);
    if(!MoveEvent || !Function || Function->Name!=MoveEvent->Name || !Parameters) return;
    auto V=Cast<AFortAthenaVehicle>(Object);
    auto W=UWorld::GetWorld();
    if(!V || V->bPendingDeath || !W || !W->AuthorityGameMode || !W->NetDriver || W->NetDriver->ServerConnection) return;
    auto Driver=V->GetDriver();
    auto PC=Driver ? Cast<AFortPlayerControllerAthena>(Driver->Controller) : nullptr;
    if(!PC) PC=Cast<AFortPlayerControllerAthena>(V->Controller);
    if(!PC || PawnFor(PC)!=Driver || Driver->IsDead() || Driver->GetVehicle()!=V) return;
    float Now=Syndicate::Now();
    std::erase_if(MoveClocks,[](const auto &M){return !M.Vehicle.Get() || !M.Driver.Get();});
    auto Clock=std::find_if(MoveClocks.begin(),MoveClocks.end(),[V](const auto &M){return M.Vehicle.Get()==V;});
    float Entry=Driver->VehicleStateRep.EntryTime;
    if(!std::isfinite(Entry)||Entry<0.f||Entry>Now) Entry=Now;
    if(Clock==MoveClocks.end())
    {
        MoveClocks.push_back({Syndicate::Ref<AFortAthenaVehicle>(V),Syndicate::Ref<AFortPlayerPawnAthena>(Cast<AFortPlayerPawnAthena>(Driver)),std::max(Entry,Now-2.f),Entry});
        Clock=std::prev(MoveClocks.end());
        Clock->Origin=V->K2_GetActorLocation();
    }
    if(Clock->Driver.Get()!=Driver||Clock->Entry!=Entry)
    {Clock->Driver.Set(Cast<AFortPlayerPawnAthena>(Driver));Clock->Accepted=std::max(Entry,Now-.25f);Clock->Entry=Entry;Clock->HasPose=false;Clock->Origin=V->K2_GetActorLocation();}
    if(Before)
    {
        std::erase_if(Moving,[V,Now](const auto &M){return !M.Vehicle.Get() || M.Vehicle.Get()==V || Now-M.Time>1.f;});
        Moving.push_back({Syndicate::Ref<AFortAthenaVehicle>(V),V->K2_GetActorLocation(),Clock->Accepted});
        return;
    }
    auto I=std::find_if(Moving.begin(),Moving.end(),[V](const auto &M){return M.Vehicle.Get()==V;});
    if(I==Moving.end()) return;
    FVector Previous=I->Before;float Age=std::max(.016f,Now-I->Time);Moving.erase(I);
    const auto &S=static_cast<SDK::Params::FortPhysicsPawn_ServerMove *>(Parameters)->InState;
    if(!VehiclePolicy::ValidMove<FVector,FQuat>(S.Translation,Previous,S.LinearVelocity,S.AngularVelocity,S.Rotation,Age)) return;
    Clock->Position=S.Translation;Clock->Velocity=S.LinearVelocity;Clock->Angular=S.AngularVelocity;
    Clock->Rotation=S.Rotation;Clock->ReceivedAt=Now;Clock->HasPose=true;
    // Preserve native handling when it updates the authority. The shipping
    // server path can leave it at entry position while the client drives away.
    auto Current=V->K2_GetActorLocation();
    if(!Syndicate::Finite(Current)) return;
    if(VehiclePolicy::NativeMoveApplied(Previous,Current,FVector(S.Translation.X,S.Translation.Y,S.Translation.Z))) {Clock->Accepted=Now;return;}
    if(ApplyVehiclePose(V,*Clock)) Clock->Accepted=Now;
}
inline void Tick(AFortGameModeAthena *GM)
{
    auto W=UWorld::GetWorld();
    if(!W || !GM) return;
    if(World.Get()!=W) {Reset();World.Set(W);}
    float N=Syndicate::Now();if(N<NextTick) return;NextTick=N+.1f;
    TickExits(N);
    std::erase_if(Riders,[](const auto &R){auto P=R.Pawn.Get();return !P||P->IsDead();});
    for(auto PC:GM->AlivePlayers)
    {
        auto P=PawnFor(PC);
        if(!P||P->IsDead()) continue;
        auto I=std::find_if(Riders.begin(),Riders.end(),[P](const auto &R){return R.Pawn.Get()==P;});
        if(I==Riders.end()) {Riders.push_back({Syndicate::Ref<AFortPlayerPawnAthena>(P)});I=std::prev(Riders.end());}
        auto V=P->GetVehicle();int Seat=V ? V->FindSeatIndex(P) : -1;
        bool Transition=I->Vehicle.Get()!=V || I->Seat!=Seat;
        if(Transition) {I->Attempts=0;I->NextEquip=0.f;}
        if(!V)
        {
            if(I->Vehicle.Get() && !P->IsInVehicle() &&
                (!P->CurrentWeapon || !FortInventory::FindItemEntry(PC,P->CurrentWeapon->ItemEntryGuid)))
            {
                if(!FortInventory::Equip(PC,P,I->InventoryGuid)) FortInventory::EquipPickaxe(PC,P);
            }
            if(P->CurrentWeapon && FortInventory::FindItemEntry(PC,P->CurrentWeapon->ItemEntryGuid)) I->InventoryGuid=P->CurrentWeapon->ItemEntryGuid;
        }
        else if(auto C=Mount(P,V))
        {
            auto D=SeatWeapon(C,Seat);
            auto Desired=D ? (D->VehicleWeaponOverride ? D->VehicleWeaponOverride : D->VehicleWeapon) : nullptr;
            if(Desired && (!P->CurrentWeapon || P->CurrentWeapon->WeaponData!=Desired) && N>=I->NextEquip && I->Attempts<3)
            {
                ++I->Attempts;I->NextEquip=N+.5f;
                C->EquipVehicleWeapon(P,D,1);
                if(P->CurrentWeapon && P->CurrentWeapon->WeaponData==Desired)
                    Log("Native mounted weapon equipped: "+V->GetName()+", seat="+std::to_string(Seat));
            }
        }
        I->Vehicle.Set(V);I->Seat=Seat;
    }
}
inline void Initialise()
{
    MoveEvent=AFortPhysicsPawn::StaticClass()->GetFunction("FortPhysicsPawn","ServerMove");
    ExitEvent=AFortPlayerControllerZone::StaticClass()->GetFunction("FortPlayerControllerZone","ServerAttemptExitVehicle");
    PawnExitEvent=AFortAthenaVehicle::StaticClass()->GetFunction("FortAthenaVehicle","OnPawnExitVehicle");
    FortInventory::EquipmentAllowed=CanUseInventory;
}
}
