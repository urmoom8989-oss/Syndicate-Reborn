#pragma once
// Included inside PlayerBot. All actions below use the same native pawn,
// inventory, structural grid and interaction adapters as human gameplay.
    BotBrain::Facts GroundFacts(AFortGameStateAthena *GS)
    {
        BotBrain::Facts F;F.Now=Syndicate::Now();F.Health=Pawn->GetHealth();F.Shield=Pawn->GetShield();
        F.Armed=HasGun();F.CanFire=CanFight();F.NeedsAmmo=NeededAmmo()!=nullptr;
        F.AmmoForGather=HasEngagementAmmo();
        auto Target=Enemy.Get();F.Enemy=Target && !Target->IsDead() && F.Now-LastSeenTime<12.f;
        F.Incoming=F.Now-LastDamageTime<2.f || F.Now-LastShotCueAt<1.f;
        if(F.Enemy)
        {
            F.EnemyDistance=Syndicate::Distance2D(Pawn->K2_GetActorLocation(),LastSeen);
            F.CombatCommitted=Brain.Seeded && Brain.Intent==BotBrain::Goal::Fight;
        }
        F.HealOwned=HealItem().ItemDefinition!=nullptr;F.HealActive=HealingStarted;
        F.HealAvailable=F.Health<50.f && !F.Enemy;
        F.Materials=MaterialCount();F.MaterialTarget=BotDifficulty::MaterialTarget(PC,BotSkillPolicy::MaterialFloor);F.HealThreshold=BotDifficulty::HealThreshold(PC);F.LootCommitted=LootTarget.Get()!=nullptr;F.ChestLoot=ChestPickup()!=nullptr;
        F.Interest=Investigation.Active(F.Now);
        auto Zone=GS->SafeZoneIndicator;auto Here=Pawn->K2_GetActorLocation();
        if(Zone && GS->GamePhase>=EAthenaGamePhase::SafeZones)
        {
            F.Rotate=BotTactics::ShouldRotate(Here,Zone->NextCenter,Zone->NextRadius,F.Now+BotDifficulty::RotationLead(PC),
                                            Zone->SafeZoneStartShrinkTime,Pawn->bIsInAnyStorm);
            F.Urgent=F.Rotate && !BotTactics::CombatRotationBudget(Here,Zone->NextCenter,
                Zone->NextRadius,F.Now,Zone->SafeZoneFinishShrinkTime,Pawn->bIsInAnyStorm);
        }
        return F;
    }
    void ReleaseGroundAction(bool ReleaseLoot)
    {
        QueuedBuild.Clear();BuildingVisual=false;BuildingUntil=Syndicate::Now();
        FightBuild.Clear();FightFloor.Clear();HighGroundStage=0;HighGroundUntil=0.f;HeightResumeCorner=false;
        RampActive=false;AscentUntil=0.f;HeightExitUntil=0.f;TerrainMission.Clear();
        HarvestTarget.Clear();BreachTarget.Clear();GatheringBurst=false;
        SetFire(false);SetADS(false);StopMove();PlayerCombat.Reset();CombatPeekUntil=0.f;GroundRecoverySurface=false;
        if(ReleaseLoot) {Release(this);LootTarget.Clear();}
        if(HasGun()) SimpleSwitchToWeapon();
    }
    bool NativePlatform(const FVector &Center)
    {
        FHitResult H{};TArray<AActor*> Ignored;Ignored.Add(Pawn);
        bool Hit=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),Center+FVector(0,0,70.f),
            Center-FVector(0,0,50.f),BotPhysics::PawnProfile(),false,Ignored,EDrawDebugTrace::None,&H,true,{},{},0);
        Ignored.Free();bool Supported=Hit && !H.bStartPenetrating && H.ImpactNormal.Z>=.7f &&
            !Syndicate::As<APawn>(H.Actor.Get()) && std::abs(H.ImpactPoint.Z-Center.Z)<40.f;
        if(Supported) FightFloor.Set(Syndicate::As<ABuildingSMActor>(H.Actor.Get()));
        return Supported; // Retain an existing real platform too; no duplicate placement or invented support.
    }
    bool WalkBuildSurface(const FVector &At,float Radius)
    {
        if(!Syndicate::Finite(At)) return false;
        if(!HasGoal || !RampSteering || Syndicate::Distance2D(MoveGoal,At)>25.f || std::abs(MoveGoal.Z-At.Z)>35.f)
        {PC->StopMovement();PC->K2_ClearFocus();NextSteer=Syndicate::Now();SteeringDirection={};}
        HasGoal=LocalSteering=RampSteering=true;GroundGoal=false;MoveGoal=At;MoveRadius=Radius;
        if(FightBuild.Active())
        {
            auto D=FightPlan.Top-FightPlan.Entry;float L=std::max(1.f,std::hypot(D.X,D.Y));
            Look(Pawn->K2_GetActorLocation()+FVector(D.X/L,D.Y/L,0)*700.f+FVector(0,0,30.f));
        }
        else Look(At+FVector(0,0,60.f));
        Sprint(FightBuild.Active()&&Syndicate::Distance2D(Pawn->K2_GetActorLocation(),At)>220.f);return true;
    }
#include "BotCombatBuild.h"
    bool BeginGroundSurfaceExit(const FVector &Toward)
    {
        auto Here=Pawn->K2_GetActorLocation();
        // A terrain path below a newly built stair does not describe how to
        // leave its crest. Descend the real connected stair before handing
        // movement back to the navigation mesh; never walk off a high ledge.
        if(Toward.Z>=Here.Z-120.f) return false;
        float Capsule=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f;
        TArray<AActor*> Ignored;Ignored.Add(Pawn);FHitResult Support{};
        bool Found=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),Here,
            Here-FVector(0,0,Capsule+70.f),BotPhysics::PawnProfile(),false,Ignored,EDrawDebugTrace::None,&Support,true,{},{},0);
        Ignored.Free();auto Under=Syndicate::As<ABuildingSMActor>(Support.Actor.Get());
        if(!Found || Support.bStartPenetrating || Support.ImpactNormal.Z<.65f || !Under ||
           Under->bDestroyed || Under->GetOwner()!=PC) return false;
        FVector Cell{},Half{};float Story=0.f;
        if(!BuildingCell(Cell,Half,Story)) return false;
        ABuildingSMActor *Selected=nullptr;float Best=700.f;FVector Low{};
        for(auto Ref:OwnedBuilds)
            if(auto B=Ref.Get();B && !B->bDestroyed && B->GetOwner()==PC && B->GetName().contains("Stair"))
            {
                auto Root=B->K2_GetActorLocation();float R=(B->K2_GetActorRotation().Yaw-90.f)*3.14159265f/180.f;
                FVector Direction(std::cos(R),std::sin(R),0);
                float Along=std::abs(Direction.X)>.5f ? Half.X : Half.Y;
                auto Plan=BotTactics::CombatRamp(Root-Direction*Along,Half,Story,Capsule,Direction);
                float Distance=Syndicate::Distance2D(Here,Plan.Top);
                if(std::abs((Here.Z-Capsule)-(Root.Z+Story))>90.f || Distance>=Best ||
                   !AllowedDestination(Plan.Entry)) continue;
                Best=Distance;Selected=B;Low=Plan.Entry;
            }
        if(!Selected) return false;
        StopMove();GroundRecoverySurface=true;GroundRecoveryGoal=Low;GroundRecoveryUntil=Syndicate::Now()+6.f;
        Navigation.BeginMission(Syndicate::Now());WalkBuildSurface(Low,30.f);return true;
    }
    bool RecoverGroundRoute(const FVector &Toward,bool Urgent)
    {
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        if(N<GroundRecoveryAt) return false;GroundRecoveryAt=N+1.5f;++Brain.Recoveries;
        GroundRecoverySurface=false;
        auto ExitToward=Toward;
        if(Urgent)
        {
            // Circle-center Z is a map marker, not the next walkable surface.
            // Use a verified nearby navigation projection for this handoff.
            auto D=Toward-Here;float L=std::max(1.f,std::hypot(D.X,D.Y));
            auto Sample=Here+FVector(D.X/L,D.Y/L,0)*500.f;FVector Projected{};
            if(NativeNavigation::Project(Sample,Projected) && Syndicate::Distance2D(Sample,Projected)<=180.f)
                ExitToward=Projected;
        }
        if(BeginGroundSurfaceExit(ExitToward)) return true;
        if(Urgent && TryTerrainDetour(Toward))
        {
            GroundRecoveryGoal=TerrainDetourGoal;GroundRecoveryUntil=TerrainDetourUntil;
            return true;
        }
        if(ReachEnvironment(Toward))
        {
            if(N<TerrainDetourUntil)
            {GroundRecoveryGoal=TerrainDetourGoal;GroundRecoveryUntil=TerrainDetourUntil;}
            return true;
        }
        auto D=Toward-Here;float Length=std::max(1.f,std::hypot(D.X,D.Y));D=D*(1.f/Length);D.Z=0.f;
        FVector Best{};float Score=100000.f;bool Found=false;
        for(float Range:{450.f,900.f}) for(float Angle:{0.f,60.f,-60.f,100.f,-100.f})
        {
            float R=Angle*3.14159265f/180.f;
            auto Sample=Here+FVector(D.X*std::cos(R)-D.Y*std::sin(R),D.X*std::sin(R)+D.Y*std::cos(R),0)*Range;
            FVector Goal{};
            if(!NativeNavigation::Project(Sample,Goal) || !Syndicate::Finite(Goal) ||
                Syndicate::Distance2D(Sample,Goal)>180.f || RouteMemory.Blocked(Goal,N) || !AllowedDestination(Goal)) continue;
            float Remaining=Syndicate::Distance2D(Goal,Toward);
            if(Urgent && !BotTactics::RotationContour(Here,Goal,Toward)) continue;
            float Value=Remaining+Syndicate::Distance2D(Here,Goal)*.15f;
            if(Value<Score) {Best=Goal;Score=Value;Found=true;}
        }
        if(!Found) return false;
        StopMove();NextPath=0.f;Navigation.Failed(N);
        // A selected alternative gets one bounded startup allowance. Repeated
        // requests for that route still cannot renew physical progress.
        Navigation.BeginMission(N);
        if(!Move(Best,90.f,true,true)) return false;
        GroundRecoveryGoal=Best;GroundRecoveryUntil=N+4.f;return true;
    }
    bool ContinueGroundRecovery()
    {
        float N=Syndicate::Now();
        if(N>=GroundRecoveryUntil)
        {
            if(GroundRecoveryUntil>0.f && Navigation.Stalled(N,3.f))
            {RouteMemory.Fail(GroundRecoveryGoal,N);StopMove();}
            GroundRecoveryUntil=0.f;GroundRecoverySurface=false;return false;
        }
        if(BotTactics::Reached(Pawn->K2_GetActorLocation(),GroundRecoveryGoal,GroundRecoverySurface ? 35.f : 100.f))
        {GroundRecoveryUntil=0.f;GroundRecoverySurface=false;StopMove();return false;}
        if(GroundRecoverySurface)
        {
            if(Navigation.Stalled(N,2.f))
            {GroundRecoveryUntil=0.f;GroundRecoverySurface=false;StopMove();return false;}
            return WalkBuildSurface(GroundRecoveryGoal,30.f);
        }
        if(!Move(GroundRecoveryGoal,90.f,true,true))
        {RouteMemory.Fail(GroundRecoveryGoal,N);GroundRecoveryUntil=TerrainDetourUntil=0.f;return false;}
        return true;
    }
    void GroundWeaponFire(bool Travelling)
    {
        if(HarvestTarget.Get() || BreachTarget.Get()) return; // Preserve native tool input owned by terrain/house recovery.
        auto Target=Enemy.Get();if(!Target || Target->IsDead() || !CanFight()) {if(CombatActorFocus){PC->K2_ClearFocus();CombatActorFocus=false;}SetFire(false);SetADS(false);return;}
        if(!PC->LineOfSightTo(Target,AimOrigin(),true))
        {
            if(CombatActorFocus){PC->K2_ClearFocus();CombatActorFocus=false;}
            SetFire(false);SetADS(false);
            if(CombatSkill::Enabled && !Travelling && !FightBuild.Active())
            {
                if(auto Blocker=OwnShotBlocker(LastSeen+FVector(0,0,50.f)))
                {if(!TryWindow(Blocker,LastSeen+FVector(0,0,50.f))) SupportedCombatPeek();}
                else if(!AttackEnemyCover()) SupportedCombatPeek();
            }
            return;
        }
        SimpleSwitchToWeapon(Syndicate::Distance2D(Pawn->K2_GetActorLocation(),LastSeen));
        if(auto Blocker=OwnShotBlocker(LastSeen+FVector(0,0,50.f)))
        {
            SetFire(false);SetADS(false);
            if(!Travelling && !FightBuild.Active() && !TryWindow(Blocker,LastSeen+FVector(0,0,50.f))) SupportedCombatPeek();
            return;
        }
        FireWhileRouting(); // Alignment, real magazine/reload and native held fire cadence.
    }
    bool RepositionCombatHeight()
    {
        float N=Syndicate::Now();if(N<GroundRecoveryAt) return false;
        GroundRecoveryAt=N+1.f;
        auto Here=Pawn->K2_GetActorLocation(),D=LastSeen-Here;
        float Length=std::max(1.f,std::hypot(D.X,D.Y));D=D*(1.f/Length);D.Z=0.f;
        float Capsule=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f;
        for(float Angle:{90.f*CombatSide,-90.f*CombatSide,0.f,180.f})
        {
            float R=Angle*3.14159265f/180.f;
            auto Sample=Here+FVector(D.X*std::cos(R)-D.Y*std::sin(R),D.X*std::sin(R)+D.Y*std::cos(R),0)*500.f;
            FVector Goal{};
            if(!NativeNavigation::Project(Sample,Goal) || !Syndicate::Finite(Goal) ||
               Syndicate::Distance2D(Sample,Goal)>150.f || std::abs(Goal.Z-(Here.Z-Capsule))>80.f ||
               !HeightHeadroom(Goal) || RouteMemory.Blocked(Goal,N)) continue;
            Goal.Z+=Capsule;
            if(!AllowedDestination(Goal)) continue;
            StopMove();NextPath=0.f;
            if(!Move(Goal,75.f,true,true)) continue;
            Navigation.BeginMission(N);GroundRecoveryGoal=Goal;GroundRecoveryUntil=N+3.f;
            ++Brain.Recoveries;return true;
        }
        return false;
    }
    bool SupportedCombatPeek()
    {
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        if(N<CombatPeekUntil)
        {
            if(BotTactics::Reached(Here,CombatPeekGoal,45.f))
            {CombatPeekUntil=0.f;StopMove();return false;}
            WalkBuildSurface(CombatPeekGoal,40.f);return true;
        }
        if(CombatPeekUntil>0.f) {CombatPeekUntil=0.f;StopMove();}
        if(N<NextCombatPeek || N-LastSeenTime>8.f || Here.Z<LastSeen.Z+180.f) return false;
        NextCombatPeek=N+.5f;
        auto D=LastSeen-Here;float L=std::max(1.f,std::hypot(D.X,D.Y));
        D=FVector(D.X/L,D.Y/L,0);FVector Side(-D.Y,D.X,0);
        float Capsule=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f;
        for(auto Offset:{Side*(140.f*CombatSide),Side*(-140.f*CombatSide),D*110.f})
        {
            auto At=Here+Offset;FHitResult FloorHit{};TArray<AActor*> Ignored;Ignored.Add(Pawn);
            bool Supported=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),At+FVector(0,0,40.f),
                At-FVector(0,0,Capsule+140.f),BotPhysics::PawnProfile(),false,Ignored,EDrawDebugTrace::None,&FloorHit,true,{},{},0);
            if(!Supported || FloorHit.bStartPenetrating || FloorHit.ImpactNormal.Z<.65f ||
               Syndicate::As<APawn>(FloorHit.Actor.Get()) || std::abs(FloorHit.ImpactPoint.Z-(Here.Z-Capsule))>65.f)
            {Ignored.Free();continue;}
            auto Goal=FloorHit.ImpactPoint+FVector(0,0,Capsule+3.f);
            FHitResult ShotHit{};
            bool Blocked=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),Goal+(AimOrigin()-Here),
                LastSeen+FVector(0,0,50.f),BotPhysics::PawnProfile(),false,Ignored,EDrawDebugTrace::None,&ShotHit,true,{},{},0);
            Ignored.Free();
            if((Blocked && ShotHit.Actor.Get()!=Enemy.Get()) || !AllowedDestination(Goal) ||
               !BotPhysics::SafeStep(Pawn,Offset*(1.f/std::max(1.f,std::hypot(Offset.X,Offset.Y))),nullptr,true)) continue;
            CombatPeekGoal=Goal;CombatPeekUntil=N+1.25f;SetFire(false);SetADS(false);
            return WalkBuildSurface(Goal,40.f);
        }
        return false;
    }
    void GroundFight()
    {
        using Action=BotPlayerStyle::Action;
        float N=Syndicate::Now();auto Target=Enemy.Get();auto Here=Pawn->K2_GetActorLocation();
        if(!Target || Target->IsDead()) {SetFire(false);PlayerCombat.Reset();return;}
        if(!CanFight()) {SetFire(false);GroundLoot(true);return;}
        BotState=EBotState::Combat;
        bool Visible=PC->LineOfSightTo(Target,AimOrigin(),true);
        if(Visible) {LastSeen=Target->K2_GetActorLocation();LastSeenTime=N;PursuitUntil=N+12.f;ObserveCombatOpponent(Target);}
        if(FightBuild.Active()&&CombatSkill::CloseIntrusion(Syndicate::Distance2D(Here,LastSeen),LastSeen.Z-Here.Z,Visible))
        {
            // An enemy occupying this stair is an immediate gunfight, not a
            // failed stair-travel timeout. Uncommitted pieces cost nothing.
            FightBuild.Clear();QueuedBuild.Clear();RampActive=false;HighGroundUntil=0.f;
            BuildingVisual=false;BuildingUntil=N;CombatShootUntil=N+.5f;StopMove();
            SimpleSwitchToWeapon(Syndicate::Distance2D(Here,LastSeen));CombatStrafeAndHop();GroundWeaponFire(false);return;
        }
        if(CombatSkill::Enabled && !FightBuild.Active() &&
           !CombatSkill::CanClimb(N,CombatShootUntil,Here.Z,CombatBaseZ,FightStory))
        {
            BuildingVisual=false;BuildingUntil=N;
            Look(LastSeen+FVector(0,0,50.f));
            SimpleSwitchToWeapon(Syndicate::Distance2D(Here,LastSeen));
            if(!Visible && AttackEnemyCover()) return;
            GroundWeaponFire(false);
            return;
        }
        if(ProtectedHeal())return;
        if(FightBuild.Active()&&N<CombatShootUntil)
        {BuildingVisual=false;BuildingUntil=N;SimpleSwitchToWeapon(Syndicate::Distance2D(Here,LastSeen));GroundWeaponFire(false);return;}
        float IncomingAt=std::max(LastDamageTime,LastShotCueAt);
        if(!FightBuild.Active() && N-IncomingAt>=0.f && N-IncomingAt<.9f &&
           IncomingAt>FightCoverHandledAt+.35f && MaterialCount()>=10)
        {
            // First defend against the observed hit/near miss. Consume the
            // burst once, so holding fire cannot repeatedly postpone climbing.
            FightCoverHandledAt=IncomingAt;NextBuild=N;
            GroundRecoveryUntil=0.f;GroundRecoverySurface=false;
            FightBuild.RetryAt=std::min(FightBuild.RetryAt,N);
            PlayerCombat.RetryAt=std::min(PlayerCombat.RetryAt,N);
            DefensiveBuild();
            if(QueuedBuild.Get()) return; // Actual commit precedes the climb.
        }
        if(N<CombatPeekUntil && !WantsCombatHeight() && Here.Z>LastSeen.Z+180.f)
        {if(SupportedCombatPeek()) {GroundWeaponFire(true);return;}}
        else if(CombatPeekUntil>0.f) {CombatPeekUntil=0.f;StopMove();}
        // An indoor escape/breach retains movement until the exit is real.
        // Replanning an outdoor height recipe each frame used to cancel it.
        if(!FightBuild.Active() && (BreachTarget.Get() || (RampActive && N<AscentUntil)))
        {if(EnvironmentAction()) return;}
        if(!Visible && !FightBuild.Active() && !WantsCombatHeight() &&
           Here.Z>LastSeen.Z+FightStory*.65f && SupportedCombatPeek()) return;
        if(!FightBuild.Active()) SimpleSwitchToWeapon(Syndicate::Distance2D(Here,LastSeen));
        auto Weapon=Pawn->CurrentWeapon;
        BotPlayerStyle::CombatFacts F;
        F.Now=N;F.Visible=Visible;F.CanFight=CanFight();F.Climbing=FightBuild.Active();
        F.Distance=Syndicate::Distance2D(Here,LastSeen);F.HeightAdvantage=Here.Z-LastSeen.Z;
        F.Story=FightStory;F.SeenAge=N-LastSeenTime;F.Materials=Pawn->bIsInAnyStorm ? 0 : MaterialCount();
        F.Loaded=Weapon && IsGun(Weapon->WeaponData) && Weapon->AmmoCount>0;
        F.Reloading=Weapon && Weapon->bIsReloadingWeapon;
        auto Choice=PlayerCombat.Update(reinterpret_cast<std::uintptr_t>(Target),F);
        if(FightBuild.Active())
        {
            if(GroundClimb()) return;
            // Recompute from the actual crest/failure, not an outdated action
            // chosen while the staircase still owned this frame.
            F.Climbing=false;Here=Pawn->K2_GetActorLocation();F.HeightAdvantage=Here.Z-LastSeen.Z;
            Choice=PlayerCombat.Update(reinterpret_cast<std::uintptr_t>(Target),F);
        }
        if(Visible&&TryCombatTrap())return;
        if(Visible&&F.Distance>9000.f&&TryMobilityDevice(LastSeen))return;
        if(PressurePush())return;
        if(Choice==Action::EscapeAmmo) {GroundLoot(true);return;}
        if(Choice==Action::Climb && WantsCombatHeight())
        {
            if(HeightExitAction()) return;
            // Finish a bounded, physically observed recovery before planning
            // another staircase. Replanning first cancels its movement owner
            // on every frame and can leave the bot pacing or stationary.
            if(N<GroundRecoveryUntil && ContinueGroundRecovery()) {GroundWeaponFire(true);return;}
            if(BeginGroundClimb(Visible)) {GroundRecoveryUntil=0.f;GroundClimb();return;}
            if(ContinueGroundRecovery()) {GroundWeaponFire(true);return;}
            if(HeightExitAction()) return;
            if(Navigation.Stalled(N,1.2f) && RecoverGroundRoute(LastSeen,false))
            {GroundWeaponFire(true);return;}
            if(N>=FightBuild.RetryAt && RepositionCombatHeight()) {GroundWeaponFire(true);return;}
            PlayerCombat.Failed(N);GroundWeaponFire(true);return;
        }
        if(ContinueGroundRecovery()) {GroundWeaponFire(true);return;}
        if(HeightExitAction()) return;
        if(Choice==Action::Search || Choice==Action::Approach)
        {
            // Follow an observation into the house instead of switching to a
            // remote loot target. Never refresh an unseen opponent's location.
            if(Choice==Action::Search && AttackEnemyCover()) return;
            if(N-LastProgress>1.8f && RecoverGroundRoute(LastSeen,false)) return;
            if(Pursue(false)) {++CombatPursuits;GroundWeaponFire(true);return;}
            Investigation.Offer(LastSeen,N);Enemy.Clear();SetFire(false);return;
        }
        if(Visible&&Syndicate::Distance2D(Here,LastSeen)<2200.f&&!F.Reloading&&CombatStrafeAndHop())
        {GroundWeaponFire(false);return;}
        if(MaintainCover(false)) return;
        bool Pressure=N-LastDamageTime<1.f || N-LastShotCueAt<.8f;
        bool WantCover=(Choice==Action::Reload || Pressure || N>=NextEngageCover) && F.Distance<4500.f;
        if(WantCover && MaterialCount()>=10 && N>=NextBuild && !HeldCover.Get())
        {
            NextEngageCover=N+2.f;DefensiveBuild();
            if(QueuedBuild.Get()) return;
        }
        if(Choice==Action::Reload)
        {
            SetFire(false);SetADS(false);
            if(Weapon && IsGun(Weapon->WeaponData)) BotReload::Check(PC,Weapon,NextReload);
            // Reload behind cover while keeping a purposeful sidestep. A
            // native reload must not be canceled by repeatedly swapping tools.
            if(F.Distance>2200.f && F.HeightAdvantage<F.Story*.7f) Pursue(false);
        }
        if(Brain.Stalled(N,3.f,false) && F.HeightAdvantage<300.f) RecoverGroundRoute(LastSeen,false);
        if(N>=GroundCombatMoveAt && Choice!=Action::Reload)
        {
            GroundCombatMoveAt=N+1.2f;
            if(Here.Z<=LastSeen.Z+300.f)
            {
                auto Goal=BotTactics::CombatGoal(Here,LastSeen,F.Distance>3000.f ? 1400.f : 700.f,CombatSide);
                if(AllowedDestination(Goal)) Move(Goal,90.f,true,true);
            }
            else StopMove(); // Shoot from a supported crest; never nav-project a high platform down to terrain.
        }
        if(Choice!=Action::Reload) {++CombatPeeks;GroundWeaponFire(false);}
    }
    void GroundLoot(bool Threatened=false)
    {
        BotState=EBotState::Looting;SetADS(false);
        // Preserve native held pickaxe input while a real breach owns the
        // route. Releasing it every frame restarts the swing indefinitely.
        if(!BreachTarget.Get() && !HarvestTarget.Get()) SetFire(false);
        if(WaitingForPickup()) {StopMove();return;}
        if(SeekCriticalHeal()) return;
        auto Nearby=ChestPickup();
        if(!Nearby) Nearby=Syndicate::As<AFortPickup>(LootTarget.Get());
        if(Nearby && !Nearby->bPickedUp && Nearby->GetDistanceTo(Pawn)<=350.f && LootVisible(Nearby))
        {
            // A completed route must hand off to the real pickup before an
            // older recovery waypoint can carry the bot away from its loot.
            GroundRecoveryUntil=0.f;RampActive=false;BreachTarget.Clear();HarvestTarget.Clear();StopMove();
            if(LootTarget.Get()!=Nearby) BeginLootTarget(Nearby);
            Loot(false);return;
        }
        if(ContinueGroundRecovery()) return;
        if(RampActive && AscentUntil>Syndicate::Now() && EnvironmentAction()) return;
        if(ExpireLoot()) GroundRecoveryAt=0.f;
        if(Threatened && LootTarget.Get() && !BotTactics::AmmoEscapeTarget(Pawn->K2_GetActorLocation(),
                LootTarget.Get()->K2_GetActorLocation(),LastSeen)) {Release(this);LootTarget.Clear();}
        if(auto Target=LootTarget.Get())
        {
            auto At=Target->K2_GetActorLocation();
            // Recover early; interaction always gets first chance inside actual reach.
            if(Target->GetDistanceTo(Pawn)>350.f && Navigation.Stalled(Syndicate::Now(),1.5f))
                if(RecoverGroundRoute(At,false)) return;
            Loot(false);return;
        }
        if(Threatened)
        {
            if(Syndicate::Now()>=NextLootSearch)
            {
                NextLootSearch=Syndicate::Now()+.3f;
                if(auto Target=FindLootTarget(false)) {BeginLootTarget(Target);Loot(false);return;}
            }
            EvadeForAmmo();return;
        }
        if(HasGun()) SimpleSwitchToWeapon();
        Loot(false); // Stable native exploration goal, never an invented pickup or weapon.
    }
#include "BotTerrainAI.h"
    void GroundRotate(AFortSafeZoneIndicator *Zone)
    {
        if(!Zone) return;
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        BotState=EBotState::MovingToSafeZone;RotationActive=true;RotationGoal=Zone->NextCenter;
        float IncomingAt=std::max(LastDamageTime,LastShotCueAt);
        if(!RampActive && !BreachTarget.Get() && !GroundRecoverySurface && Enemy.Get() &&
           N-LastSeenTime<4.f && N-IncomingAt>=0.f && N-IncomingAt<.8f &&
           IncomingAt>FightCoverHandledAt+.35f && N-FightCoverHandledAt>1.25f && MaterialCount()>=10)
        {
            // Urgent travel still defends itself. A bounded cover pulse never
            // turns exposed storm escape into a stationary multi-story fight.
            FightCoverHandledAt=IncomingAt;NextBuild=N;DefensiveBuild();
            if(QueuedBuild.Get()) return;
        }
        FVector PadGoal(Zone->NextCenter.X,Zone->NextCenter.Y,Zone->NextCenter.Z);auto FromCenter=Here-Zone->NextCenter;
        float CenterDistance=std::max(1.f,std::hypot(FromCenter.X,FromCenter.Y));
        PadGoal=PadGoal+FVector(FromCenter.X/CenterDistance,FromCenter.Y/CenterDistance,0)*std::min(4500.f,std::max(0.f,Zone->NextRadius*.5f));
        PadGoal.Z=Here.Z;FVector ProjectedPadGoal{};
        if(NativeNavigation::Project(PadGoal,ProjectedPadGoal)&&Syndicate::Distance2D(PadGoal,ProjectedPadGoal)<300.f)PadGoal=ProjectedPadGoal;
        if(TryMobilityDevice(PadGoal))return; // Full safe destination, not the 12-metre walking segment.

        if(TerrainRoute(Zone->NextCenter) || TerrainMission.Active()) {GroundWeaponFire(true);return;}
        if(ContinueGroundRecovery()) {GroundWeaponFire(true);return;}
        if(N<TerrainDetourUntil)
        {
            GroundRecoveryGoal=TerrainDetourGoal;GroundRecoveryUntil=TerrainDetourUntil;
            if(ContinueGroundRecovery()) {GroundWeaponFire(true);return;}
        }
        if((RampActive || BreachTarget.Get()) && EnvironmentAction()) {GroundWeaponFire(true);return;}
        CollectNearbyLoot(false,false);
        if(HasGun()) SimpleSwitchToWeapon();
        if(N>=GroundRotationUntil || BotTactics::Reached(Here,GroundRotationGoal,120.f))
        {
            GroundRotationGoal=BotTactics::ZoneWaypoint(Here,Zone->NextCenter,Zone->NextRadius);
            GroundRotationUntil=N+6.f;
        }
        bool Started=Move(GroundRotationGoal,120.f,true,true);
        if(!Started || Navigation.Stalled(N,1.8f)) RecoverGroundRoute(Zone->NextCenter,true);
        GroundWeaponFire(true); // Defence never takes ownership away from urgent escape movement.
    }
#include "BotGroundRescue.h"
    void GroundTick()
    {
        auto World=UWorld::GetWorld();auto GS=World ? Syndicate::As<AFortGameStateAthena>(World->GameState) : nullptr;
        if(!GS) return;
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        TickResources();TravelStyle();
        Navigation.Observe(Here,N);ClearFinishedEnvironment();ReactToCues();Sense();
        if(TickCelebration())return;
        if((Pawn->GetHealth()<LastHealth && !Pawn->bIsInAnyStorm) || Pawn->GetShield()<LastShield) LastDamageTime=N;
        if(Pawn->GetHealth()>LastHealth+.5f || Pawn->GetShield()>LastShield+.5f) ++GroundNativeSerial;
        if(auto Weapon=Pawn->CurrentWeapon;Weapon && IsGun(Weapon->WeaponData))
        {
            if(GroundObservedWeapon.Get()!=Weapon) {GroundObservedWeapon.Set(Weapon);GroundShots={};}
            auto Shot=GroundShots.Sample(Weapon->LastFireTime,Weapon->AmmoCount,Weapon->bIsReloadingWeapon);
            if(Shot!=BotTactics::ShotEvidence::Kind::None)
            {
                ++GroundNativeSerial;
                if(Role(Weapon->WeaponData)==0) PlayerCombat.ShotgunShot(N);
            }
        }
        if(N>=NextPhysicsCheck) {NextPhysicsCheck=N+2.f;BotPhysics::MaintainGroundPawn(Pawn);}
        auto F=GroundFacts(GS);
        if(DeviceStage)
        {
            // A paid device transaction owns its few frames just like a native
            // build. Optional loot/gather replanning must not cancel its floor.
            ProcessBuild();if(QueuedBuild.Get())return;
            if(TickDeviceAction(F.Urgent,F.Incoming))return;
        }
        auto Previous=Brain.Intent;bool Changed=Brain.Update(F,Here,GroundNativeSerial+StairCrests);
        if(Changed)
        {
            Navigation.BeginMission(N);
            if(HealingStarted && Brain.Intent!=BotBrain::Goal::Heal) CancelHeal();
            if(Brain.Intent==BotBrain::Goal::Fight || Brain.Intent==BotBrain::Goal::Rotate || Previous==BotBrain::Goal::Fight)
                ReleaseGroundAction(Brain.Intent==BotBrain::Goal::Fight || Brain.Intent==BotBrain::Goal::Rotate);
            else if(Previous==BotBrain::Goal::Gather)
            {GatheringBurst=false;HarvestTarget.Clear();SetFire(false);StopMove();if(HasGun()) SimpleSwitchToWeapon();}
            if(Brain.Intent==BotBrain::Goal::Gather)
            {GatheringBurst=true;GatherGoal=std::min(BotSkillPolicy::MaterialGoal,std::max(BotSkillPolicy::MaterialFloor,F.Materials+180));BreachTarget.Clear();StopMove();SetFire(false);}
            if(Brain.Intent!=BotBrain::Goal::Rotate) RotationActive=false;
            GroundRecoveryUntil=0.f;GroundRecoverySurface=false;
        }
        if(BotState==EBotState::Landed)
        {NextLootSearch=0.f;LastProgress=N;LandingLootAnchor=Here;LandingLootUntil=N+25.f;LootInteraction.Clear();}
        LastHealth=F.Health;LastShield=F.Shield;
        if(N<GroundRescueUntil && !(F.Incoming && F.CanFire) && !QueuedBuild.Get() && !WallEdit.Active())
        {
            if(!BotTactics::Reached(Here,GroundRescueGoal,30.f))
            {
                HasGoal=LocalSteering=GroundGoal=true;RampSteering=false;MoveGoal=GroundRescueGoal;MoveRadius=30.f;
                if(F.Enemy && F.CanFire) GroundWeaponFire(true);
                return;
            }
            GroundRescueUntil=0.f;StopMove();NextPath=0.f;
        }
        else if(GroundRescueUntil>0.f)
        {GroundRescueUntil=0.f;StopMove();NextPath=0.f;}
        // Threat response precedes door traversal, healing, looting and route
        // recovery. A committed native piece finishes first; it cannot be
        // cancelled or charged twice to manufacture an instant second piece.
        float IncomingAt=std::max(LastDamageTime,LastShotCueAt);
        if(F.Incoming && F.Enemy && !FightBuild.Active() && !WallEdit.Active() && !QueuedBuild.Get() &&
           IncomingAt>FightCoverHandledAt+.35f && N-IncomingAt>=0.f && N-IncomingAt<.9f && F.Materials>=10)
        {
            FightCoverHandledAt=IncomingAt;NextBuild=N;
            if(HarvestTarget.Get() || BreachTarget.Get()) SetFire(false);
            HarvestTarget.Clear();BreachTarget.Clear();GatheringBurst=false;
            DoorPass.Clear(); // Native open door remains open; a threatened traversal no longer owns combat.
            GroundRecoveryUntil=0.f;GroundRecoverySurface=false;
            DefensiveBuild();
        }
        CloseWindow();
        if(WallEdit.Active())
        {
            SetFire(false);SetADS(false);StopMove();auto Old=WallEdit.Target.Get();
            if(WallEdit.Tick(PC,Pawn,PlayerState)) return;
            if(auto Edited=WallEdit.Result.Get())
            {
                for(auto &R:OwnedBuilds) if(R.Pointer==Old) R.Set(Edited);
                for(int I=0;I<6;++I) if(BoxWalls[I].Pointer==Old)
                {BoxWalls[I].Set(Edited);BoxWallHealth[I]=Edited->GetHealth();}
                CompleteCombatEdit(Old,Edited);++GroundNativeSerial;
            }
            else WindowPending.Clear();
            NextEdit=N+.25f;if(HasGun()) SimpleSwitchToWeapon();else EquipPickaxe();
        }
        ProcessBuild();
        if(QueuedBuild.Get()) return;
        if(TickDoorPass()) return;
        if(WindowCover.Get() && Brain.Intent==BotBrain::Goal::Fight &&
           !(WantsCombatHeight() && MaterialCount()>=20) && WindowAttack()) return;
        if(BuildingVisual && N<BuildingUntil) return;
        BuildingVisual=FightBuild.Active();
        if(TickDeviceAction(F.Urgent,F.Incoming))return;
        switch(Brain.Intent)
        {
        case BotBrain::Goal::Fight: GroundFight();break;
        case BotBrain::Goal::Rotate: GroundRotate(GS->SafeZoneIndicator);break;
        case BotBrain::Goal::Heal:
            BotState=EBotState::Healing;
            if(F.Incoming || BoxDamaged()) {CancelHeal();BoxActive=false;GroundFight();}
            else
            {
                if(!HealingStarted&&(BoxActive||F.Materials>=60))
                {if(!BoxActive){ReleaseGroundAction(false);NextBuild=N;}DefensiveBuild(true);if(BoxActive&&BoxPiece<6)break;}
                if(!QueuedBuild.Get())Heal();
            }
            break;
        case BotBrain::Goal::Gather:
            if(!F.Rotate&&RoamClearedPOI())break;
            BotState=EBotState::Looting;
            if(!Harvest()) {Brain.NextGather=N+(F.Materials<120 ? 2.f : 10.f);Brain.GatherEnds=N;GatheringBurst=false;HarvestTarget.Clear();GroundLoot();}
            break;
        case BotBrain::Goal::Investigate:
            if(!Investigate()) GroundLoot();break;
        case BotBrain::Goal::Arm: case BotBrain::Goal::Resupply:
            GroundLoot(F.Enemy);break;
        case BotBrain::Goal::Loot:
            if(!F.Rotate&&RoamClearedPOI())break;
            if(!LootSpoils()) GroundLoot();break;
        }
        StartGroundRescue(F);
    }
