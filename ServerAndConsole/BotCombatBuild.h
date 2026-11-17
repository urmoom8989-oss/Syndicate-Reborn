#pragma once
// Included inside PlayerBot: one retained combat build/movement owner. Native
// grid approval, paid inventory and real support are still authoritative.
    bool RequestFightPiece(UFortBuildingItemDefinition *Def,const FVector &At,float Yaw,BotBrain::BuildStep Next)
    {
        auto Previous=LastBuilt.Get();
        if(!Build(Def,At,Yaw)) return false;
        FightBuild.Step=Next;FightBuild.Await(reinterpret_cast<std::uintptr_t>(Previous),Syndicate::Now());
        return true;
    }
    bool WantsCombatHeight() const
    {
        auto Target=Enemy.Get();auto Here=Pawn->K2_GetActorLocation();
        if(CombatSkill::Enabled && !CombatSkill::CanClimb(Syndicate::Now(),CombatShootUntil,Here.Z,CombatBaseZ,FightStory)) return false;
        if(!CombatHeightBudget.Allows(Here.Z,FightStory))return false;
        return Target && !Target->IsDead() && !Target->bIsSkydiving && !Target->bIsParachuteOpen && Syndicate::Now()-LastSeenTime<10.f &&
            BotSkillPolicy::BuildPressure(Syndicate::Distance2D(Here,LastSeen),Here.Z-LastSeen.Z,FightStory);
    }
    bool RejectFightClimb(int Reason)
    {
        float N=Syndicate::Now();
        FightFailureReason=Reason;FightBlockedCell=FightCell;FightBlockedUntil=N+2.f;
        FightBuild.Fail(N);PlayerCombat.Failed(N);++HeightClimbAborts;
        QueuedBuild.Clear();BuildingVisual=false;BuildingUntil=N;RampActive=false;
        HighGroundUntil=0.f;StopMove();if(HasGun()) SimpleSwitchToWeapon();
        return false;
    }
    void ObserveCombatEntry(BotTactics::RampPlan &Plan)
    {
        FightEntryGroundZ=FightCell.Z;FightEntrySupported=false;
        if(FightBuild.Active()) return; // Upper turns require their new paid platform.
        auto Rise=Plan.Top-Plan.Entry;float L=std::max(1.f,std::hypot(Rise.X,Rise.Y));
        FVector D(Rise.X/L,Rise.Y/L,0);float Along=std::abs(D.X)>.5f ? FightHalf.X : FightHalf.Y;
        float Capsule=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f;
        float Low=1.f-Syndicate::Distance2D(Plan.Root,Plan.Entry)/(2.f*Along);
        for(float Fraction:{Low,.24f,.34f,.44f,.54f,.64f,.74f})
        {
            auto At=Plan.Root-D*(2.f*Along*(1.f-Fraction));float Surface=FightCell.Z+FightStory*Fraction;
            TArray<AActor*> Ignored;Ignored.Add(Pawn);FHitResult H{};
            bool Found=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
                FVector(At.X,At.Y,std::max(Pawn->K2_GetActorLocation().Z,Surface+Capsule)+70.f),
                FVector(At.X,At.Y,FightCell.Z-80.f),BotPhysics::PawnProfile(),false,
                Ignored,EDrawDebugTrace::None,&H,true,{},{},0);
            Ignored.Free();
            if(!Found || H.bStartPenetrating || Syndicate::As<APawn>(H.Actor.Get()) ||
               !Syndicate::Finite(H.ImpactPoint) || H.ImpactNormal.Z<.7f ||
               Syndicate::Distance2D(H.ImpactPoint,At)>2.f) continue;
            float Join=Surface-H.ImpactPoint.Z;
            if(Join<10.f || Join>80.f) continue;
            Plan.Entry=At+FVector(0,0,Capsule+FightStory*Fraction+19.f);
            FightEntryGroundZ=H.ImpactPoint.Z;FightEntrySupported=true;return;
        }
    }
    bool PlanCombatStory()
    {
        if(!BuildingCell(FightCell,FightHalf,FightStory)) return false;
        float N=Syndicate::Now();
        auto D=FightDirection;float L=std::max(1.f,std::hypot(D.X,D.Y));D=D*(1.f/L);D.Z=0.f;
        bool SameFailure=N<FightBlockedUntil && Syndicate::Distance2D(FightCell,FightBlockedCell)<80.f &&
            std::abs(FightCell.Z-FightBlockedCell.Z)<80.f;
        int Skip=SameFailure && !FightBuild.Active() ? 1 : 0;
        // Retain the requested quarter-turn/forward heading in an upper
        // sequence. Choosing 90/180-degree alternatives each frame made an
        // obstructed builder rotate and stack unrelated pieces in place.
        for(int I=0;I<(FightBuild.Active() ? 1 : 3);++I)
        {
            int Choice=(I+Skip)%3;
            float Angle=(Choice==0 ? 0.f : Choice==1 ? 90.f*CombatSide : Choice==2 ? -90.f*CombatSide : 180.f)*3.14159265f/180.f;
            FVector Direction(D.X*std::cos(Angle)-D.Y*std::sin(Angle),D.X*std::sin(Angle)+D.Y*std::cos(Angle),0);
            auto Plan=BotTactics::CombatRamp(FightCell,FightHalf,FightStory,
                Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f,Direction);
            ObserveCombatEntry(Plan);
            if(FightBuild.Active())
            {
                auto Axis=Plan.Top-Plan.Entry;float Length=std::max(1.f,std::hypot(Axis.X,Axis.Y));
                FVector Across(-Axis.Y/Length,Axis.X/Length,0);
                auto Offset=Pawn->K2_GetActorLocation()-FightCell;
                float Width=std::abs(Across.X)>.5f ? FightHalf.X : FightHalf.Y;
                float Lateral=std::clamp(Offset.X*Across.X+Offset.Y*Across.Y,-Width+70.f,Width-70.f);
                Plan.Entry=Plan.Entry+Across*Lateral;
            }
            if(!AllowedDestination(Plan.Top) || !RampClearance(Plan,FightHalf,FightStory)) continue;
            FightPlan=Plan;FightDirection=Direction;
            FightStageDistance=100000.f;return true;
        }
        ++FightPlanRejects;return false;
    }
    bool BeginGroundClimb(bool Visible)
    {
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        if(!BotDifficulty::Allow(PC,BotDifficulty::BuildFrequency,N)||!BotDifficulty::Allow(PC,BotDifficulty::HeightSeeking,N))return false;
        if(N<FightBuild.RetryAt || Pawn->bIsInAnyStorm || !CanFight() || MaterialCount()<20 ||
           !WantsCombatHeight() || (!Visible && N-LastSeenTime>6.f)) return false;
        auto Feet=Here-FVector(0,0,Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f);
        if(!HeightHeadroom(Feet)) {FightFailureReason=1;TryHeightExit();return false;}
        FightDirection=LastSeen-Here;FightDirection.Z=0.f;
        if(FightDirection.IsZero()) FightDirection=FVector(1,0,0);
        auto OpeningCover=HeldCover.Get();
        bool CoveredStart=OpeningCover && !OpeningCover->bDestroyed && OpeningCover->GetHealth()>0.f &&
            Syndicate::Distance2D(Here,HeldCoverLocation)<700.f;
        bool StartRush=BotSkillPolicy::Rush(Syndicate::Distance2D(Here,LastSeen),Here.Z-LastSeen.Z,FightStory);
        if((CoveredStart && !StartRush) || (Syndicate::Distance2D(Here,LastSeen)<1400.f && LastSeen.Z>Here.Z+120.f))
            FightDirection=FVector(-FightDirection.Y*CombatSide,FightDirection.X*CombatSide,0);
        if(!PlanCombatStory()) {FightBuild.RetryAt=N+.2f;FightFailureReason=2;return false;}
        CombatHeightBudget.Observe(N,Here.Z);
        FightHeight=CombatHeightBudget.Goal(LastSeen.Z,FightStory);FightBuild.Begin(N);HighGroundUntil=N+30.f;
        auto Toward=LastSeen-Here;float Range=std::max(1.f,std::hypot(Toward.X,Toward.Y));
        HighGroundStage=0;CoveredRampRush=StartRush && (Toward.X*FightDirection.X+Toward.Y*FightDirection.Y)>Range*.5f;
        CornerCellReady=false;RampActive=false;AscentUntil=0.f;FightFloor.Clear();
        HarvestTarget.Clear();BreachTarget.Clear();GatheringBurst=false;
        SetFire(false);SetADS(false);StopMove();BuildingVisual=true;FightFailureReason=0;return true;
    }
    bool GroundClimb()
    {
        using Step=BotBrain::BuildStep;
        if(!FightBuild.Active()) return false;
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        if(!CanFight() || !Enemy.Get() || Enemy.Get()->IsDead() || Pawn->bIsInAnyStorm ||
           N-LastSeenTime>12.f || Enemy.Get()->bIsSkydiving || Enemy.Get()->bIsParachuteOpen || N-FightBuild.Started>30.f) return RejectFightClimb(3);
        if(N<CombatShootUntil){BuildingVisual=false;return true;}
        SetFire(false);SetADS(false);BuildingVisual=true;
        if(FightBuild.Waiting)
        {
            auto Next=FightBuild.Step;bool Optional=Next==Step::FrontWall || Next==Step::Stair;
            if(!FightBuild.Resolve(QueuedBuild.Get()!=nullptr,reinterpret_cast<std::uintptr_t>(LastBuilt.Get()),N))
            {
                if(FightBuild.Active()) return true;
                if(!Optional) {++HeightBuildRejects;return RejectFightClimb(4);}
                FightBuild.Step=Next;FightBuild.RetryAt=0.f;FightBuild.LastProgress=N;++HeightBuildRejects;
            }
            else
            {
                ++GroundNativeSerial;
                if(Next==Step::Center) FightFloor.Set(LastBuilt.Get());
            }
        }
        float Capsule=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f;
        // Consume bookkeeping transitions in this update, rather than adding
        // a server frame for every no-op stage. Actual native requests/travel
        // always yield and are checked on the following observation.
        for(int Budget=0;Budget<8;++Budget)
        {
            float Facing=(FightPlan.ActorYaw-90.f)*3.14159265f/180.f;
            FVector Direction(std::cos(Facing),std::sin(Facing),0),Side(-Direction.Y,Direction.X,0);
            float Along=std::abs(Direction.X)>.5f ? FightHalf.X : FightHalf.Y;
            float Across=std::abs(Side.X)>.5f ? FightHalf.X : FightHalf.Y;
            if((LastSeen.X-FightCell.X)*Side.X+(LastSeen.Y-FightCell.Y)*Side.Y<0.f) Side=Side*-1.f;
            if(FightBuild.Step==Step::Floor)
            {
                // Jump from the proven crest BEFORE inserting the upper
                // floor beside the capsule. This avoids an upper slab clipping
                // its feet and then blocking the route to the next stair.
                if(!FightBuild.JumpIssued)
                {
                    auto Velocity=Pawn->GetVelocity();
                    if(!Syndicate::Finite(Velocity)) return RejectFightClimb(12);
                    if(std::hypot(Velocity.X,Velocity.Y)>90.f)
                    {
                        // Jumping retains walking momentum. Release ordinary
                        // input and let native braking settle at the inset
                        // crest before asking for the corner jump.
                        StopMove();
                        if(N-FightBuild.CornerAt>.45f) return RejectFightClimb(12);
                        return true;
                    }
                }
                if(NativePlatform(FightCell)) {FightBuild.Step=Step::Platform;continue;}
                if(Here.Z-Capsule<FightCell.Z+24.f)
                {
                    if(!FightBuild.JumpIssued && RequestJump()) FightBuild.JumpIssued=true;
                    Look(Here+Direction*700.f+FVector(0,0,30.f));
                    if(N-FightBuild.CornerAt>.85f) return RejectFightClimb(10);
                    return true;
                }
                FightBuild.Step=Step::Platform;FightBuild.LastProgress=N;continue;
            }
            if(FightBuild.Step==Step::Platform)
            {
                if((!FightBuild.Corner && FightEntrySupported) || NativePlatform(FightCell))
                {FightBuild.Step=Step::Center;continue;}
                if(!RequestFightPiece(Floor,FightCell,0.f,Step::Center))
                {++HeightBuildRejects;return RejectFightClimb(6);}
                return true;
            }
            if(FightBuild.Step==Step::Center)
            {
                auto Start=FightPlan.Entry;Start.Z=(FightBuild.Corner ? FightCell.Z : FightEntryGroundZ)+Capsule+3.f;
                float Distance=(Here-Start).Magnitude();
                if(Distance+8.f<FightStageDistance) {FightStageDistance=Distance;FightBuild.LastProgress=N;}
                if(Syndicate::Distance2D(Here,Start)>55.f || std::abs(Here.Z-Start.Z)>65.f)
                {
                    if(N-FightBuild.LastProgress>1.2f) return RejectFightClimb(5);
                    RampEntering=true;WalkBuildSurface(Start,30.f);
                    // Cover is queued during the supported approach. Keep the
                    // native movement owner; the stair waits for staging below.
                    FightBuild.Step=Step::SideWall;continue;
                }
                FightBuild.Step=Step::SideWall;FightBuild.LastProgress=N;continue;
            }
            if(FightBuild.Step==Step::SideWall || FightBuild.Step==Step::FrontWall)
            {
                bool SidePiece=FightBuild.Step==Step::SideWall;auto Next=SidePiece ? Step::FrontWall : Step::Stair;
                float Yaw=SidePiece ? std::atan2(Side.Y,Side.X)*180.f/3.14159265f+90.f : FightPlan.ActorYaw;
                auto At=SidePiece ? FightCell+Side*Across : FightPlan.Root;
                auto Normal=SidePiece ? Side : Direction;
                float Clearance=std::abs((Here.X-At.X)*Normal.X+(Here.Y-At.Y)*Normal.Y);
                float Radius=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleRadius() : 42.f;
                if(Clearance<Radius+12.f)
                {
                    // Do not grow a wall through the capsule at a crest/turn.
                    // Keep physical staging movement until the builder fits.
                    auto Start=FightPlan.Entry;Start.Z=(FightBuild.Corner ? FightCell.Z : FightEntryGroundZ)+Capsule+3.f;
                    float Distance=(Here-Start).Magnitude();
                    if(Distance+8.f<FightStageDistance){FightStageDistance=Distance;FightBuild.LastProgress=N;}
                    if(N-FightBuild.LastProgress>1.2f) return RejectFightClimb(5);
                    RampEntering=true;WalkBuildSurface(Start,25.f);return true;
                }
                if(MaterialCount()>=20 && RequestFightPiece(Wall,At,std::remainder(Yaw,360.f),Next)) return true;
                FightBuild.Step=Next;FightBuild.LastProgress=N;continue;
            }
            if(FightBuild.Step==Step::Stair)
            {
                auto Start=FightPlan.Entry;Start.Z=(FightBuild.Corner ? FightCell.Z : FightEntryGroundZ)+Capsule+3.f;
                float Distance=(Here-Start).Magnitude();
                if(Distance+8.f<FightStageDistance) {FightStageDistance=Distance;FightBuild.LastProgress=N;}
                if(Syndicate::Distance2D(Here,Start)>55.f || std::abs(Here.Z-Start.Z)>65.f)
                {
                    if(N-FightBuild.LastProgress>1.2f) return RejectFightClimb(5);
                    RampEntering=true;WalkBuildSurface(Start,30.f);return true;
                }
                if(!RampClearance(FightPlan,FightHalf,FightStory))
                {++HeightBuildRejects;return RejectFightClimb(7);}
                ABuildingSMActor *Existing=nullptr;
                for(auto Ref:OwnedBuilds)
                    if(auto B=Ref.Get();B && !B->bDestroyed && B->bPlayerPlaced && B->GetOwner()==PC &&
                       B->GetName().contains("Stair") &&
                       (B->K2_GetActorLocation()-FightPlan.Root).Magnitude()<20.f &&
                       std::abs(std::remainder(B->K2_GetActorRotation().Yaw-FightPlan.ActorYaw,360.f))<2.f)
                    {Existing=B;break;}
                if(Existing)
                {
                    // A failed attempt can leave a paid, usable stair behind.
                    // Traverse that real actor rather than requesting an
                    // overlapping duplicate that native placement rejects.
                    RampActor.Set(Existing);FightBuild.Step=Step::Enter;FightBuild.LastProgress=N;
                }
                else if(!RequestFightPiece(Stair,FightPlan.Root,FightPlan.ActorYaw,Step::Enter))
                {++HeightBuildRejects;return RejectFightClimb(7);}
                RampEntry=FightPlan.Entry;RampGoal=FightPlan.Top;RampUpperZ=FightPlan.Root.Z+FightStory;
                RampUpperCell=FightPlan.Root+Direction*8.f;RampUpperCell.Z=RampUpperZ+2.f;
                RampExitFeet=RampUpperZ-15.f;RampEntering=true;RampActive=true;
                RampProgressAt=N;RampBestDistance=(Here-RampEntry).Magnitude();return true;
            }
            auto Ramp=RampActor.Get();
            if(!Ramp || Ramp->bDestroyed || N-RampProgressAt>1.2f) return RejectFightClimb(8);
            auto Goal=FightBuild.Step==Step::Enter ? RampEntry : RampGoal;
            float Distance=(Here-Goal).Magnitude();
            if(Distance+8.f<RampBestDistance)
            {RampBestDistance=Distance;RampProgressAt=N;FightBuild.LastProgress=N;}
            FHitResult Support{};TArray<AActor*> Ignored;Ignored.Add(Pawn);
            bool Found=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),Here,
                Here-FVector(0,0,Capsule+70.f),BotPhysics::PawnProfile(),false,Ignored,EDrawDebugTrace::None,&Support,true,{},{},0);
            Ignored.Free();bool OnRamp=Found && !Support.bStartPenetrating && Support.Actor.Get()==Ramp && Support.ImpactNormal.Z>=.65f &&
                Here.Z-Capsule-Support.ImpactPoint.Z>=-20.f && Here.Z-Capsule-Support.ImpactPoint.Z<70.f;
            // The stair is now a paid actor. Its low-end incline may be
            // above the walking step limit under a builder staged on a flat
            // floor. A real jump clears it; never force the pawn up the slope.
            if(FightBuild.Step==Step::Enter && !OnRamp && Here.Z-Capsule<RampEntry.Z-Capsule-25.f)
                RequestJump();
            if(FightBuild.Step==Step::Enter && OnRamp && (Syndicate::Distance2D(Here,RampGoal)<Syndicate::Distance2D(RampEntry,RampGoal) || BotTactics::Reached(Here,RampEntry,90.f) ||
                BotTactics::SupportedRampProgress(Here,RampEntry,RampGoal,Here.Z-Capsule,Support.ImpactPoint)))
            {
                FightBuild.Step=Step::Climb;RampEntering=false;RampBestDistance=(Here-RampGoal).Magnitude();
                RampProgressAt=FightBuild.LastProgress=N;
            }
            if(FightBuild.Step==Step::Climb && OnRamp &&
               Syndicate::Distance2D(Here,RampGoal)<45.f && Here.Z-Capsule>=RampUpperZ-80.f &&
               Here.Z-Capsule<=RampUpperZ+75.f && Here.Z-Capsule-Support.ImpactPoint.Z<30.f)
            {
                ++StairCrests;++GroundNativeSerial;PlayerCombat.Crest(N,Here.Z);RampActive=false;StopMove();
                if(CombatSkill::Enabled && ((StairCrests%2)==0 || !BotSkillPolicy::BuildPressure(Syndicate::Distance2D(Here,LastSeen),Here.Z-LastSeen.Z,FightStory)))
                {
                    // Complete the supported stair before putting the gun out.
                    // Both duelists need a shooting turn instead of endlessly
                    // chasing a moving two-story height target.
                    CombatShootUntil=N+CombatSkill::CrestShootPause;
                    PlayerCombat.PeekUntil=CombatShootUntil;
                    BuildingVisual=false;BuildingUntil=N;SimpleSwitchToWeapon();
                    BuildLevelHintZ=RampUpperZ;BuildLevelHintPoint={Here.X,Here.Y,RampUpperZ+2.f};BuildLevelHintUntil=N+4.f;
                }
                BuildLevelHintZ=RampUpperZ;
                bool Rush=CoveredRampRush && BotSkillPolicy::Rush(Syndicate::Distance2D(Here,LastSeen),Here.Z-LastSeen.Z,FightStory);
                // A rush advances to the NEXT native cell. A height retake
                // turns within this cell; stacking forward stairs over the
                // same footprint is a tower, not a push toward the opponent.
                BuildLevelHintPoint=Rush ? FightPlan.Root+Direction*16.f : FVector(Here.X,Here.Y,RampUpperZ+2.f);
                BuildLevelHintPoint.Z=RampUpperZ+2.f;BuildLevelHintUntil=N+3.f;
                // Retain the goal selected before this climb; target motion
                // cannot continually raise the same recipe into a tower.
                if(Here.Z>=FightHeight-50.f || !CombatHeightBudget.Allows(Here.Z,FightStory) || MaterialCount()<20)
                {FightBuild.Clear();HighGroundUntil=0.f;CombatShootUntil=std::max(CombatShootUntil,N+1.2f);FightBuild.RetryAt=CombatShootUntil;BuildingVisual=false;BuildingUntil=N;SimpleSwitchToWeapon();return false;}
                // One latched quarter turn per story. Do not rotate towards
                // the enemy again for every side/rear wall or target movement.
                CoveredRampRush=Rush;
                FightDirection=Rush ? Direction : FVector(-Direction.Y*CombatSide,Direction.X*CombatSide,0);
                if(!PlanCombatStory()) return RejectFightClimb(9);
                ++FightRetakes;FightBuild.NextStory(N);return true;
            }
            WalkBuildSurface(FightBuild.Step==Step::Enter ? RampEntry : RampGoal,
                             FightBuild.Step==Step::Enter ? 20.f : 8.f);
            return true;
        }
        return RejectFightClimb(11); // Corrupt/unhandled recipe cannot silently hold the pawn.
    }

