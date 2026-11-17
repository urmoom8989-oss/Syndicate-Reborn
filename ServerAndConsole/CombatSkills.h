#pragma once
// PlayerBot extensions. Weapons, ammo and consumables come only from real loot.
// Only the explicitly requested timed material/ammunition income is granted.
    int KitSlot(UFortItemDefinition *D)
    {
        if(!D)return -1;
        auto N=D->GetName();std::transform(N.begin(),N.end(),N.begin(),[](unsigned char C){return std::tolower(C);});
        bool Health=N.contains("medkit")||N.contains("bandage")||N.contains("slurp")||N.contains("chug");
        return CombatSkill::Slot(LoadoutKind(D),Health);
    }
    float KitQuality(UFortItemDefinition *D)
    {
        if(!D)return -1000.f;
        auto N=D->GetName();std::transform(N.begin(),N.end(),N.begin(),[](unsigned char C){return std::tolower(C);});
        return CombatSkill::Quality(LoadoutKind(D),static_cast<int>(D->Rarity),PreferSMG,N.contains("medkit"))+(!PreferSMG&&KitSlot(D)==2&&!IsGun(D)?50.f:0.f);
    }
    float LootImprovement(UFortItemDefinition *D)
    {
        int Slot=KitSlot(D);if(Slot<0)return 0.f;
        float Best=-1000.f;
        if(auto I=FortInventory::Get(PC))for(auto E:I->Inventory.ReplicatedEntries)
            if(E.Count>0&&KitSlot(E.ItemDefinition)==Slot&&FortInventory::GetQuickBars(E.ItemDefinition)==EFortQuickBars::Primary)
                Best=std::max(Best,KitQuality(E.ItemDefinition));
        return Best<-900.f?2000.f:std::clamp((KitQuality(D)-Best)*50.f,0.f,4500.f);
    }
    bool NeedsBetterLoot()
    {
        int Target=Syndicate::Now()-LandedAt>300.f?3:2;
        bool Shotgun=false,Rifle=false;
        if(auto I=FortInventory::Get(PC))for(auto E:I->Inventory.ReplicatedEntries)
            if(E.Count>0&&E.ItemDefinition&&static_cast<int>(E.ItemDefinition->Rarity)>=Target)
            {Shotgun|=LoadoutKind(E.ItemDefinition)==0;Rifle|=LoadoutKind(E.ItemDefinition)==1;}
        return !Shotgun||!Rifle;
    }
    void TickResources()
    {
        int AmmoGrant=AmmoIncome.Due(Syndicate::Now());
        if(AmmoGrant>0)
            for(auto Path:{"/Game/Athena/Items/Ammo/AthenaAmmoDataShells.AthenaAmmoDataShells",
                           "/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsLight.AthenaAmmoDataBulletsLight",
                           "/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsMedium.AthenaAmmoDataBulletsMedium"})
                if(auto D=StaticLoadObject<UFortAmmoItemDefinition>(Path))
                {
                    int Cap=D->MaxStackSize>0.f?std::clamp(static_cast<int>(D->MaxStackSize),1,999):999;
                    int Add=std::min(AmmoGrant,std::max(0,Cap-FortInventory::CountDefinition(PC,D)));if(Add>0)GiveItem(D,Add);
                }
        int Grant=MaterialIncome.Due(Syndicate::Now());if(!Grant)return;
        for(auto Type:{EFortResourceType::Wood,EFortResourceType::Stone,EFortResourceType::Metal})
            if(auto D=UFortKismetLibrary::K2_GetResourceItemDefinition(Type))
            {
                int Count=FortInventory::CountDefinition(PC,D),Add=std::min(Grant,std::max(0,999-Count));
                if(Add>0)GiveItem(D,Add);
            }
    }
    void CancelCelebration()
    {
        bool Active=Celebration.Active;Celebration.Clear();
        if(Active){CancelEmote();SetFire(false);NextSense=NextPath=0.f;if(HasGun())SimpleSwitchToWeapon();}
    }
    void QueueCelebration(AFortPlayerStateAthena *Victim)
    {
        if(Victim)Celebration.Offer(Syndicate::Now(),Syndicate::Pick(100),!Victim->bIsABot);
    }
    bool CelebrationSafe()
    {
        float N=Syndicate::Now();
        if(Pawn->bIsInAnyStorm||Pawn->bIsDBNO||Pawn->GetHealth()<45.f||HealingStarted||BoxActive||
           FightBuild.Active()||QueuedBuild.Get()||WallEdit.Active()||N-LastDamageTime<1.f||N-LastShotCueAt<1.2f)return false;
        auto GS=Cast<AFortGameStateAthena>(UWorld::GetWorld()->GameState);auto Z=GS?GS->SafeZoneIndicator:nullptr;
        if(Z&&BotTactics::ShouldRotate(Pawn->K2_GetActorLocation(),Z->NextCenter,Z->NextRadius,N,Z->SafeZoneStartShrinkTime,false))return false;
        for(auto Ref:PawnCache)
            if(auto P=Ref.Get();P&&P!=Pawn&&!P->IsDead()&&P->GetDistanceTo(Pawn)<6000.f)
                if(auto S=Cast<AFortPlayerStateAthena>(P->PlayerState);S&&S->TeamIndex!=PlayerState->TeamIndex&&
                   PC->LineOfSightTo(P,AimOrigin(),true))return false;
        return true;
    }
    bool TickCelebration()
    {
        if(!Celebration.Pending&&!Celebration.Active)return false;
        float N=Syndicate::Now();bool Safe=CelebrationSafe();
        if(Celebration.Active)
        {
            if(Celebration.Stop(N,!Safe)||(N-Celebration.Started>1.f&&!Pawn->bIsPlayingEmote))
            {CancelCelebration();return false;}
            StopMove();SetFire(false);SetADS(false);return true;
        }
        if(!Celebration.Begin(N,Syndicate::Between(3.f,8.f),Safe))return false;
        if(Dances.empty()){Celebration.Clear();return false;}
        ReleaseGroundAction(false);StopMove();SetFire(false);SetADS(false);
        Emote(Dances[Syndicate::Pick(static_cast<int>(Dances.size()))]);
        return true;
    }
    bool TickGunTrigger(AFortWeapon *W,float N)
    {
        if(W->WeaponData->GetTriggerType()==EFortWeaponTriggerType::Automatic)return false;
        if(SingleShotWeapon.Get()!=W){SingleShotWeapon.Set(W);SingleShot={};SetFire(false);}
        auto Stats=Looting::GetWeaponStats(W->WeaponData);
        float Rate=Stats?reinterpret_cast<const FFortRangedWeaponStats*>(Stats)->FiringRate:2.f;
        auto Action=SingleShot.Step(N,Firing,Rate);
        if(Action==CombatSkill::TriggerAction::Press)SetFire(true);
        else if(Action==CombatSkill::TriggerAction::Release)SetFire(false);
        return true;
    }
    void ObserveCombatOpponent(AFortPlayerPawnAthena *Target)
    {
        float N=Syndicate::Now();CombatHeightBudget.Observe(N,Pawn->K2_GetActorLocation().Z);
        auto V=Target->GetVelocity();
        CombatPrediction.Observe(reinterpret_cast<std::uintptr_t>(Target),N,V.X,V.Y,V.Z);
        auto W=Target->CurrentWeapon;
        bool Vulnerable=W&&IsGun(W->WeaponData)&&W->bIsReloadingWeapon;
        bool Challenged=N-LastDamageTime<2.f||N-LastShotCueAt<1.f;
        bool Engage=Syndicate::Distance2D(Pawn->K2_GetActorLocation(),LastSeen)<=8500.f;
        if((Vulnerable||Challenged||Engage)&&Pawn->GetHealth()>=45.f&&CanFight())
        {PressureTarget.Set(Target);PressureUntil=std::max(PressureUntil,N+(Vulnerable?1.2f:3.f));}
    }
    FVector PredictCombatAim(AFortPlayerPawnAthena *Target,UFortItemDefinition *Weapon,float Distance)
    {
        float N=Syndicate::Now();auto V=Target->GetVelocity();
        CombatPrediction.Observe(reinterpret_cast<std::uintptr_t>(Target),N,V.X,V.Y,V.Z);
        auto Lead=CombatPrediction.Offset(reinterpret_cast<std::uintptr_t>(Target),N,Distance,LoadoutKind(Weapon)==0);
        return Target->K2_GetActorLocation()+FVector(Lead[0],Lead[1],Lead[2])*BotDifficulty::Pct(PC,BotDifficulty::Prediction);
    }
    void ConfirmPressureHit(AFortPlayerPawnAthena *Target,float Damage)
    {
        if(!std::isfinite(Damage)||Damage<=0.f)return;
        auto S=Target?Cast<AFortPlayerStateAthena>(Target->PlayerState):nullptr;
        if(!S||S->TeamIndex==PlayerState->TeamIndex||Target==Pawn)return;
        float N=Syndicate::Now();
        PressureHits=PressureTarget.Get()==Target&&N-LastPressureHit<4.f?PressureHits+1:1;
        PressureTarget.Set(Target);LastPressureHit=N;
        if(CombatSkill::PressWounded(PressureHits,Damage,Target->GetHealth()+Target->GetShield()))
        {
            PressureUntil=N+10.f*BotDifficulty::Scale(PC,BotDifficulty::Aggression,.35f);
            Enemy.Set(Target);LastSeen=Target->K2_GetActorLocation();LastSeenTime=N;PursuitUntil=PressureUntil;
        }
    }
    bool PressurePush()
    {
        float N=Syndicate::Now();auto E=Enemy.Get();auto Here=Pawn->K2_GetActorLocation();
        if(!E||E!=PressureTarget.Get()||E->IsDead()||N>=PressureUntil||Pawn->GetHealth()<30.f||
           N-LastSeenTime>10.f||FightBuild.Active()||RampActive||HealingStarted||BoxActive)return false;
        float Distance=Syndicate::Distance2D(Here,LastSeen);
        if(Distance>8500.f*BotDifficulty::Scale(PC,BotDifficulty::Aggression,.25f)||std::abs(LastSeen.Z-Here.Z)>FightStory*.75f)return false;
        bool Visible=PC->LineOfSightTo(E,AimOrigin(),true);
        if(!Visible&&Distance<=1000.f&&AttackEnemyCover())return true;
        if(Visible&&Distance<450.f)return false; // Close gunfight owns strafe and shotgun/SMG follow-ups.
        // An exposed long approach can use a protected rush. A boxed target
        // instead gets a retained ground approach, not another height recipe.
        if(Visible&&Distance>2200.f&&MaterialCount()>=40&&WantsCombatHeight()&&BeginGroundClimb(true))
        {GroundClimb();return true;}
        if(Move(LastSeen,180.f,true,true))
        {
            Look(LastSeen+FVector(0,0,50.f));SimpleSwitchToWeapon(Distance);
            GroundWeaponFire(true);++CombatPursuits;return true;
        }
        return ReachEnvironment(LastSeen);
    }
    void TravelStyle()
    {
        auto V=Pawn->GetVelocity();float Speed=std::hypot(V.X,V.Y);
        bool Ready=HasGoal&&!Enemy.Get()&&!HealingStarted&&!BoxActive&&!BuildingVisual&&!RampActive&&
            !FightBuild.Active()&&!Pawn->bIsInAnyStorm&&Syndicate::Distance2D(Pawn->K2_GetActorLocation(),MoveGoal)>1000.f&&
            Pawn->CharacterMovement&&Pawn->CharacterMovement->MovementMode==EMovementMode::MOVE_Walking&&
            Syndicate::Finite(V)&&Speed>220.f;
        if(TravelHop.Due(Syndicate::Now(),Ready,Syndicate::Pick(100))&&
           BotPhysics::SafeStep(Pawn,FVector(V.X/Speed,V.Y/Speed,0),nullptr,false,180.f))RequestJump();
    }
    bool ReuseBoxPiece(int Piece,FVector At,float Yaw)
    {
        auto Type=Piece<4?EFortBuildingType::Wall:Piece==4?EFortBuildingType::Floor:EFortBuildingType::Roof;
        if(Piece>=4)At=BotTactics::FloorRoot(At,BoxHalf,Yaw);
        for(auto Ref:OwnedBuilds)
            if(auto B=Ref.Get();B&&!B->bDestroyed&&B->GetOwner()==PC&&B->GetHealth()>0.f&&B->BuildingType==Type&&
               (B->K2_GetActorLocation()-At).Magnitude()<25.f&&
               (Piece>=4||std::abs(std::remainder(B->K2_GetActorRotation().Yaw-Yaw,180.f))<5.f))
            {BoxWalls[Piece]=Ref;BoxWallHealth[Piece]=B->GetHealth();return true;}
        return false;
    }
    bool ProtectedHeal()
    {
        float N=Syndicate::Now();
        auto Item=HealItem();float Need=Item.ItemDefinition&&Role(Item.ItemDefinition)==4?std::min(Pawn->GetHealth(),Pawn->GetShield()):Pawn->GetHealth();
        if(!BoxActive && Need>=BotDifficulty::HealThreshold(PC))return false;
        if(!CombatSkill::BoxHeal(Need,Item.ItemDefinition!=nullptr,MaterialCount(),BoxActive,N<SuppressHealUntil))return false;
        if(!BoxActive){ReleaseGroundAction(false);SetFire(false);DefensiveBuild(true);return BoxActive;}
        if(BoxDamaged()||N-BoxDamageAt<.5f){CancelHeal();BoxActive=false;return false;}
        if(BoxPiece<6){DefensiveBuild(true);return BoxActive;}
        if(N-LastDamageTime<.7f||N-LastShotCueAt<.7f){SetFire(false);return true;}
        BotState=EBotState::Healing;Heal();return true;
    }
    bool ExploreWiderArea()
    {
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        std::erase_if(SearchedAreas,[N](const auto &R){return N>=R.second;});
        if(TourUntil>N&&AllowedDestination(TourGoal)&&!RouteMemory.Blocked(TourGoal,N))
        {
            if(Syndicate::Distance2D(Here,TourGoal)>350.f&&N-LastProgress<5.f)
            {if(TryMobilityDevice(TourGoal))return true;
        return Move(TourGoal,200.f,true,true);}
            TourUntil=0.f;
        }
        if(N<NextTour||N<LandedAt+20.f)return false;
        NextTour=N+Syndicate::Between(12.f,20.f);
        SearchedAreas.push_back({Here,N+100.f});if(SearchedAreas.size()>12)SearchedAreas.erase(SearchedAreas.begin());
        FVector Best{};float Score=100000.f;
        size_t Start=BotLanding::Regions.empty()?0:Syndicate::Pick(static_cast<int>(BotLanding::Regions.size()));
        for(size_t I=0;I<std::min<size_t>(BotLanding::Regions.size(),8);++I)
        {
            auto &R=BotLanding::Regions[(Start+I)%BotLanding::Regions.size()];
            float Distance=Syndicate::Distance2D(Here,R.Center);
            if(Distance<4500.f||Distance>30000.f||!AllowedDestination(R.Center)||
               std::any_of(SearchedAreas.begin(),SearchedAreas.end(),[&](const auto &V){return Syndicate::Distance2D(V.first,R.Center)<3500.f;}))continue;
            auto Containers=BotSpatial::Containers.Nearby(R.Center,std::min(9000.f,std::hypot(R.Extent.X,R.Extent.Y)));
            for(auto Ref:Containers)
                if(auto C=Ref.Get();C&&!C->bAlreadySearched&&!C->bDestroyed&&C->SearchLootTierGroup.ToString().contains("Treasure"))
                {
                    auto At=C->K2_GetActorLocation();FVector Projected{};
                    if(!AllowedDestination(At)||RouteMemory.Blocked(At,N)||!NativeNavigation::Project(At,Projected))continue;
                    float Rank=Syndicate::Distance2D(Here,At);if(Rank<Score){Best=At;Score=Rank;}break;
                }
        }
        if(Best.IsZero())return false;
        TourGoal=Best;TourUntil=N+45.f;StopMove();Navigation.BeginMission(N);
        if(TryMobilityDevice(TourGoal))return true;
        return Move(TourGoal,200.f,true,true);
    }

    bool CombatStrafeAndHop()
    {
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        float Distance=Syndicate::Distance2D(Here,LastSeen);
        if(FightBuild.Active()||BoxActive||HealingStarted||Distance>2200.f||std::abs(Here.Z-LastSeen.Z)>220.f)return false;
        if(N>=NextCombatSide){CombatSide*=-1.f;NextCombatSide=N+Syndicate::Between(1.6f,2.6f);}
        if(N>=GroundCombatMoveAt)
        {
            GroundCombatMoveAt=N+.65f;auto D=LastSeen-Here;float L=std::max(1.f,std::hypot(D.X,D.Y));D=FVector(D.X/L,D.Y/L,0);
            auto Goal=Here+D*std::clamp(Distance-650.f,-120.f,260.f)+FVector(-D.Y,D.X,0)*(CombatSide*260.f*BotDifficulty::Pct(PC,BotDifficulty::Strafing));
            auto Offset=Goal-Here;float OL=std::max(1.f,std::hypot(Offset.X,Offset.Y));
            if(AllowedDestination(Goal)&&BotPhysics::SafeStep(Pawn,Offset*(1.f/OL),nullptr,true))Move(Goal,65.f,true,true);
        }
        if(N>=NextCombatHop)
        {NextCombatHop=N+Syndicate::Between(2.f,3.5f);if(Distance>350.f&&Syndicate::Pick(100)<35*BotDifficulty::Pct(PC,BotDifficulty::Jumping))RequestJump();}
        return true;
    }
    bool RoamClearedPOI()
    {
        float N=Syndicate::Now();auto Here=Pawn->K2_GetActorLocation();
        if(Enemy.Get()||N-LastDamageTime<3.f||N-LastShotCueAt<2.f||Pawn->bIsInAnyStorm||HealingStarted||BoxActive||!HasGun()||!HasEngagementAmmo())return false;
        if(TourUntil>N)
        {
            if(!AllowedDestination(TourGoal)||RouteMemory.Blocked(TourGoal,N)||N-LastProgress>6.f||Syndicate::Distance2D(Here,TourGoal)<500.f)
            {TourUntil=0.f;NextTour=N+5.f;StopMove();return false;}
            if(BreachTarget.Get()||RampActive){if(EnvironmentAction())return true;}
            if(TryMobilityDevice(TourGoal))return true;
            return Move(TourGoal,250.f,true,true)||ReachEnvironment(TourGoal);
        }
        if(N<NextTour||N<LandedAt+60.f||ChestPickup())return false;
        for(auto Ref:LocalPickups(1800.f))if(auto P=Ref.Get();P&&!P->bPickedUp&&!P->bHidden&&P->PrimaryPickupItemEntry.Count>0&&
            Wants(P->PrimaryPickupItemEntry.ItemDefinition,P->PrimaryPickupItemEntry.LoadedAmmo)&&LootVisible(P)&&
            (LootImprovement(P->PrimaryPickupItemEntry.ItemDefinition)>0.f||Role(P->PrimaryPickupItemEntry.ItemDefinition)>=4))return false;
        NextTour=N+5.f;
        int LocalEnemies=0,LocalChests=0;
        for(auto Ref:PawnCache)if(auto P=Ref.Get();P&&P!=Pawn&&!P->IsDead()&&!P->bIsSkydiving&&!P->bIsParachuteOpen)
            if(auto S=Cast<AFortPlayerStateAthena>(P->PlayerState);S&&S->bIsABot&&S->TeamIndex!=PlayerState->TeamIndex&&P->GetDistanceTo(Pawn)<4500.f)++LocalEnemies;
        for(auto Ref:LocalContainers(3000.f))if(auto C=Ref.Get();C&&!C->bAlreadySearched&&!C->bDestroyed&&C->SearchLootTierGroup.ToString().contains("Treasure"))++LocalChests;
        if(!CombatSkill::CanDepartPOI(N-LandedAt,true,true,false,true,LocalEnemies,LocalChests))return false;
        if(NeedsBetterLoot()) {NextTour=N;if(ExploreWiderArea())return true;}
        FVector Best{};float Score=100000.f;
        // Permitted bot-only director knowledge selects a travel destination.
        // Humans still require perception; travelling never fires through walls.
        for(auto Ref:PawnCache)if(auto P=Ref.Get();P&&P!=Pawn&&!P->IsDead()&&!P->bIsSkydiving&&!P->bIsParachuteOpen)
            if(auto S=Cast<AFortPlayerStateAthena>(P->PlayerState);S&&S->bIsABot&&S->TeamIndex!=PlayerState->TeamIndex)
            {
                auto At=P->K2_GetActorLocation();float Distance=Syndicate::Distance2D(Here,At);FVector Projected{};
                if(Distance<5000.f||Distance>60000.f||!AllowedDestination(At)||RouteMemory.Blocked(At,N)||!NativeNavigation::Project(At,Projected))continue;
                if(Distance<Score){Score=Distance;Best=Projected;}
            }
        if(Best.IsZero())return ExploreWiderArea();
        ReleaseGroundAction(true);TourGoal=Best;TourUntil=N+90.f;NextTour=N+10.f;
        Navigation.BeginMission(N);++RoamTrips;
        return Move(TourGoal,250.f,true,true);
    }
