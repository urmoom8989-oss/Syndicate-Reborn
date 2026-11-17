#pragma once
// Included inside PlayerBot. A blocked circle route retains one terrain
// objective through contouring, real harvesting and connected paid stairs.
    bool TerrainNeedsRamp(const FVector &Toward)
    {
        auto Here=Pawn->K2_GetActorLocation();auto D=Toward-Here;float Length=std::hypot(D.X,D.Y);
        if(Length<200.f) return false;
        float Half=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 75.f;
        float Feet=Here.Z-Half;TArray<AActor*> Ignored;Ignored.Add(Pawn);bool Needed=false;
        for(float Range:{180.f,400.f})
        {
            auto At=Here+FVector(D.X/Length,D.Y/Length,0)*std::min(Range,Length);
            FHitResult Hit{};
            bool Found=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
                FVector(At.X,At.Y,Feet+1200.f),FVector(At.X,At.Y,Feet-250.f),
                BotPhysics::PawnProfile(),false,Ignored,EDrawDebugTrace::None,&Hit,true,{},{},0);
            auto Building=Syndicate::As<ABuildingSMActor>(Hit.Actor.Get());
            // Our preceding stair or a house roof is not evidence that the
            // mountain continues. Building access has its own route adapter.
            if(Building && (Building->bPlayerPlaced || Building->bCanBeDamaged)) continue;
            if(Found && !Hit.bStartPenetrating && !Syndicate::As<APawn>(Hit.Actor.Get()) &&
               Syndicate::Finite(Hit.ImpactPoint) && Hit.ImpactPoint.Z>Feet+80.f &&
               (Hit.ImpactNormal.Z<.7f || Hit.ImpactPoint.Z>Feet+180.f)) {Needed=true;break;}
        }
        Ignored.Free();return Needed; // Clear/downhill/empty space is not permission for another tower story.
    }
    bool TerrainRoute(const FVector &Toward)
    {
        using Stage=BotMission::TerrainStep;
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();int Materials=MaterialCount();
        if(TerrainMission.Active() && Syndicate::Distance2D(TerrainMissionGoal,Toward)>1200.f)
        {TerrainMission.Clear();TerrainDetourUntil=0.f;AscentUntil=0.f;}
        if(!TerrainMission.Active())
        {
            if(N<TerrainMission.ProbeAt || N-Navigation.At<.9f) return false;
            TerrainMission.ProbeAt=N+.5f;
            auto D=Toward-Here;float Length=std::hypot(D.X,D.Y);if(Length<300.f) return false;
            float Capsule=Pawn->CapsuleComponent ? Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight() : 88.f;
            auto Ahead=Here+FVector(D.X/Length,D.Y/Length,0)*500.f;float Feet=Here.Z-Capsule;
            TArray<AActor*> Ignored;Ignored.Add(Pawn);FHitResult Hit{};
            bool Found=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
                FVector(Ahead.X,Ahead.Y,Feet+900.f),FVector(Ahead.X,Ahead.Y,Feet-200.f),
                BotPhysics::PawnProfile(),false,Ignored,EDrawDebugTrace::None,&Hit,true,{},{},0);
            Ignored.Free();
            if(!Found || Hit.bStartPenetrating || Syndicate::As<APawn>(Hit.Actor.Get()) ||
                !Syndicate::Finite(Hit.ImpactPoint) || Hit.ImpactPoint.Z<=Feet+80.f ||
                (Hit.ImpactNormal.Z>=.7f && Hit.ImpactPoint.Z<=Feet+180.f)) return false;
            auto Building=Syndicate::As<ABuildingSMActor>(Hit.Actor.Get());
            if(Building && (Building->bPlayerPlaced || Building->bCanBeDamaged)) return false; // Doors/house walls use their own interaction adapter.
            TerrainMission.Begin(Here,N,Materials);TerrainMissionGoal=Toward;
            GroundRecoveryUntil=0.f;GroundRecoverySurface=false;StopMove();NextTerrainDetour=N;
        }
        TerrainMission.Observe(Here,N,Materials);
        if(Syndicate::Distance2D(Here,TerrainMissionGoal)<300.f)
        {TerrainMission.Clear();AscentUntil=0.f;TerrainDetourUntil=0.f;return false;}
        if(TerrainMission.Step==Stage::Contour)
        {
            if(N<TerrainDetourUntil && !BotTactics::Reached(Here,TerrainDetourGoal,150.f))
            {
                if(!Navigation.Stalled(N,2.f) && Move(TerrainDetourGoal,100.f,true,true)) return true;
                RouteMemory.Fail(TerrainDetourGoal,N);TerrainDetourUntil=0.f;StopMove();
            }
            else if(TerrainDetourUntil>0.f)
            {TerrainDetourUntil=0.f;TerrainMission.Clear();NextPath=0.f;return false;}
            if(TryTerrainDetour(TerrainMissionGoal)) {Navigation.BeginMission(N);return true;}
            TerrainMission.Step=Materials<60 ? Stage::Gather : Stage::Climb;
            TerrainMission.ProgressAt=N;NextHarvest=0.f;
        }
        if(TerrainMission.Step==Stage::Gather)
        {
            if(Materials>=80)
            {
                SetFire(false);HarvestTarget.Clear();GatheringBurst=false;StopMove();
                TerrainMission.Step=Stage::Climb;TerrainMission.ProgressAt=N;NextRamp=N;
            }
            else
            {
                GatheringBurst=true;GatherGoal=80;
                if(TerrainMission.Stalled(N,12.f))
                {
                    IgnoredHarvest=HarvestTarget;IgnoreHarvestUntil=N+30.f;
                    HarvestTarget.Clear();GatheringBurst=false;SetFire(false);StopMove();
                    TerrainMission.Step=Stage::Contour;TerrainMission.ProgressAt=N;NextTerrainDetour=N;
                    ++TerrainMission.Failures;return false;
                }
                if(Harvest()) return true; // Only native hits award materials.
                if(TerrainMission.Stalled(N,2.5f))
                {
                    // No reachable resource: abandon the failed slope and
                    // search a different reachable contour, never dry-build.
                    HarvestTarget.Clear();GatheringBurst=false;SetFire(false);
                    TerrainMission.Step=Stage::Contour;NextTerrainDetour=N;TerrainMission.ProgressAt=N;
                    ++TerrainMission.Failures;RouteMemory.Fail(Here,N);StopMove();
                    return false;
                }
                return true;
            }
        }
        if(TerrainMission.Step==Stage::Climb)
        {
            if(RampActive || BreachTarget.Get())
            {
                if(EnvironmentAction()) return true;
                ++TerrainMission.Failures;
            }
            if(!TerrainNeedsRamp(TerrainMissionGoal))
            {TerrainMission.Clear();AscentUntil=0.f;RampActive=false;NextPath=0.f;return false;}
            if(MaterialCount()<20)
            {TerrainMission.Step=Stage::Gather;TerrainMission.ProgressAt=N;NextHarvest=0.f;return true;}
            // A proven crest, rather than a new ground task, continues the
            // same ascent. Native support/clearance still approve every piece.
            AscentToward=TerrainMissionGoal;
            if(N>=AscentUntil) {AscentAnchor=Here;AscentSteps=0;AscentUntil=N+30.f;}
            if(N>=NextRamp && StartRamp(TerrainMissionGoal,true)) return true;
            if(TerrainMission.Stalled(N,2.f))
            {
                RouteMemory.Fail(RampEntry,N);RouteMemory.Fail(RampGoal,N);
                RampActive=false;AscentUntil=0.f;StopMove();NextTerrainDetour=N;
                TerrainMission.Step=Stage::Contour;TerrainMission.ProgressAt=N;++TerrainMission.Failures;
                return false;
            }
            return true;
        }
        return false;
    }
