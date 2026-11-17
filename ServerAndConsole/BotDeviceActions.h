#pragma once
// Included inside PlayerBot. Devices are real, looted inventory entries.
    FFortItemEntry FindDevice(BotDevices::Kind Kind)
    {
        if(auto I=FortInventory::Get(PC))for(auto E:I->Inventory.ReplicatedEntries)
            if(E.Count>0)if(auto D=Cast<UFortTrapItemDefinition>(E.ItemDefinition))
            {
                auto FloorDef=PlayerTraps::Definition(D,EBuildingAttachmentType::ATTACH_Floor);
                if(FloorDef&&BotDevices::Classify(FloorDef->GetName())==Kind)return E;
            }
        return {};
    }
    bool DeviceCell(FVector Point,FVector &Center,FVector &Half,float &Story)
    {
        UBuildingStructuralSupportSystem *Grid=nullptr;
        UFortKismetLibrary::GetBuildingStructuralSupportSystem(UWorld::GetWorld(),&Grid);if(!Grid)return false;
        auto Cell=Grid->K2_GetGridBoxFromWorldLoc(Point);
        if(!Cell.IsValid||!Syndicate::Finite(Cell.min_0)||!Syndicate::Finite(Cell.max_0))return false;
        Half=(Cell.max_0-Cell.min_0)*.5f;Story=Cell.max_0.Z-Cell.min_0.Z;
        Center=FVector((Cell.min_0.X+Cell.max_0.X)*.5f,(Cell.min_0.Y+Cell.max_0.Y)*.5f,Cell.min_0.Z);
        return Half.X>0&&Half.Y>0&&Story>0&&Center.Z<=Point.Z+80.f&&Center.Z>=Point.Z-Story;
    }
    ABuildingSMActor *DeviceFloor()
    {
        auto Fits=[&](ABuildingSMActor *B)
        {
            if(!B||B->bDestroyed||B->IsActorBeingDestroyed()||B->BuildingType!=EFortBuildingType::Floor||
               (B->bPlayerPlaced&&B->TeamIndex!=PlayerState->TeamIndex))return false;
            float R=B->K2_GetActorRotation().Yaw*3.14159265f/180.f;
            auto Center=B->K2_GetActorLocation()+FVector(-std::sin(R),std::cos(R),0)*DeviceHalf.Y;
            return (Center-DeviceCenter).Magnitude()<35.f;
        };
        if(auto B=LastBuilt.Get();Fits(B))return B;
        for(auto Ref:Syndicate::ActorsOf<ABuildingSMActor>())if(auto B=Ref.Get();Fits(B))return B;
        return nullptr;
    }
    void FinishDevice(bool Launched=false)
    {
        DeviceResult=Launched?"launched":("finished stage "+std::to_string(DeviceStage));
        if(Launched)
        {
            TargetDropZone=DeviceGoal;DeviceAirUntil=Syndicate::Now()+30.f;++DeviceFlights;
            if(Pawn->bIsSkydiving||Pawn->bIsParachuteOpen)BotState=EBotState::Skydiving;
        }
        if(DeviceStage==8)RampActive=false;
        DeviceStage=0;DeviceKind=BotDevices::Kind::None;DeviceDefinition.Clear();DeviceSupport.Clear();DeviceVictim.Clear();
        NextDevice=Syndicate::Now()+(Launched?20.f:8.f);BuildingVisual=false;BuildingUntil=Syndicate::Now();
        StopMove();SetFire(false);if(HasGun())SimpleSwitchToWeapon();else EquipPickaxe();
    }
    bool BeginDevice(FFortItemEntry E,BotDevices::Kind Kind,FVector Goal,AFortPlayerPawnAthena *Victim=nullptr)
    {
        auto D=Cast<UFortTrapItemDefinition>(E.ItemDefinition);if(!D)return false;
        auto Source=Victim?Victim:Pawn;auto Feet=Source->K2_GetActorLocation();
        if(Source->CapsuleComponent)Feet.Z-=Source->CapsuleComponent->GetScaledCapsuleHalfHeight();
        if(!DeviceCell(Feet,DeviceCenter,DeviceHalf,DeviceStory))return false;
        // Do not box the builder together with its victim.
        auto Here=Pawn->K2_GetActorLocation();
        if(Victim&&std::abs(Here.X-DeviceCenter.X)<DeviceHalf.X+35.f&&std::abs(Here.Y-DeviceCenter.Y)<DeviceHalf.Y+35.f)return false;
        ReleaseGroundAction(true);DeviceDefinition.Set(D);DeviceKind=Kind;DeviceVictim.Set(Victim);
        DeviceGoal=Goal;DeviceRamps=0;DeviceStage=1;DeviceUntil=Syndicate::Now()+(Victim?4.f:6.f);DeviceLaunched=false;
        return true;
    }
    bool TryCombatTrap()
    {
        float N=Syndicate::Now();if(N<NextDevice||DeviceStage||HealingStarted||BoxActive||FightBuild.Active()||RampActive||MaterialCount()<50)return false;
        auto E=Enemy.Get();if(!E||E->IsDead()||N-LastSeenTime>.5f||E->GetDistanceTo(Pawn)>650.f)return false;
        auto Trap=FindDevice(BotDevices::Kind::Damage);if(!Trap.ItemDefinition){NextDevice=N+3.f;return false;}
        return BeginDevice(Trap,BotDevices::Kind::Damage,LastSeen,E);
    }
    bool TryMobilityDevice(FVector Goal)
    {
        float N=Syndicate::Now();if(N<NextDevice||DeviceStage)return false;
        auto Here=Pawn->K2_GetActorLocation();float Distance=Syndicate::Distance2D(Here,Goal);
        bool Busy=HealingStarted||BoxActive||FightBuild.Active()||RampActive||QueuedBuild.Get()||WallEdit.Active()||
            N-LastDamageTime<1.f||N-LastShotCueAt<.6f;
        if(Busy||!AllowedDestination(Goal)||Distance<800.f)return false;
        auto Kind=Distance>=9000.f?BotDevices::Kind::Launch:BotDevices::Kind::Bounce;
        if(!BotDevices::Useful(Kind,Distance,Goal.Z-Here.Z,Busy,true))return false;
        auto E=FindDevice(Kind);NextDevice=N+3.f;
        if(!E.ItemDefinition)return false;
        return BeginDevice(E,Kind,Goal);
    }
    bool TickDeviceAction(bool Urgent,bool Incoming)
    {
        float N=Syndicate::Now();
        if(DeviceAirUntil>N&&!Pawn->bIsSkydiving&&!Pawn->bIsParachuteOpen&&Pawn->CharacterMovement&&
           Pawn->CharacterMovement->MovementMode==EMovementMode::MOVE_Falling)
        {
            auto D=TargetDropZone-Pawn->K2_GetActorLocation();float L=std::hypot(D.X,D.Y);
            if(L>150.f)Pawn->AddMovementInput(FVector(D.X/L,D.Y/L,0),1.f,true);
            Look(TargetDropZone,true);return true;
        }
        if(!DeviceStage)return false;
        if(N>=DeviceUntil||HealingStarted||BoxActive||(DeviceKind==BotDevices::Kind::Damage&&Urgent)||Incoming)
        {FinishDevice();return false;}
        auto Victim=DeviceVictim.Get();
        if(DeviceKind==BotDevices::Kind::Damage&&(!Victim||Victim->IsDead()||
           !BotDevices::KeepVictim(Syndicate::Distance2D(Victim->K2_GetActorLocation(),DeviceCenter),
                Victim->K2_GetActorLocation().Z-DeviceCenter.Z,N-(DeviceUntil-4.f))))
        {FinishDevice();return false;}
        if(DeviceStage==8)
        {
            if(RampActive&&EnvironmentAction())return true;
            FVector Center{},Half{};float Story=0.f;
            if(!BuildingCell(Center,Half,Story)||Center.Z<DeviceCenter.Z+DeviceStory*.5f)
            {FinishDevice();return false;}
            DeviceCenter=Center;DeviceHalf=Half;DeviceStory=Story;DeviceStage=1;DeviceUntil=N+5.f;return true;
        }
        if(DeviceStage==1)
        {
            if(auto Support=DeviceFloor()){DeviceSupport.Set(Support);DeviceStage=DeviceKind==BotDevices::Kind::Damage?2:6;}
            else if(!Build(Floor,DeviceCenter,0.f))
            {
                // Uneven terrain can reject a ground-level floor. Reach one
                // real connected stair crest before trying its upper platform.
                if(DeviceKind!=BotDevices::Kind::Damage&&DeviceRamps==0&&MaterialCount()>=20&&StartRamp(DeviceGoal,true))
                {++DeviceRamps;DeviceStage=8;DeviceUntil=N+10.f;return true;}
                FinishDevice();return false;
            }
            return true;
        }
        if(DeviceStage>=2&&DeviceStage<6)
        {
            int Side=DeviceStage++-2;
            const FVector Offsets[]={FVector(DeviceHalf.X,0,0),FVector(0,DeviceHalf.Y,0),FVector(-DeviceHalf.X,0,0),FVector(0,-DeviceHalf.Y,0)};
            Build(Wall,DeviceCenter+Offsets[Side],float(Side*90+90));return true;
        }
        if(DeviceStage==6)
        {
            auto Entry=FortInventory::FindItemEntry(PC,DeviceDefinition.Get());
            auto Support=DeviceSupport.Get();if(!Entry||!Support){FinishDevice();return false;}
            SetFire(false);SetADS(false);Equip(*Entry);
            auto Tool=Cast<AFortDecoTool>(Pawn->CurrentWeapon);
            auto Trap=Tool?PlayerTraps::Place(Tool,BotTactics::FloorRoot(DeviceCenter,DeviceHalf,0.f),FRotator{},Support,EBuildingAttachmentType::ATTACH_Floor):nullptr;
            if(!Trap){FinishDevice();return false;}
            ActiveDevice.Set(Trap);++DevicesPlaced;
            if(DeviceKind==BotDevices::Kind::Damage){FinishDevice();return true;}
            TargetDropZone=DeviceGoal;DeviceStage=7;DeviceUntil=N+5.f;NextDeviceTrigger=N;
            BuildingVisual=false;BuildingUntil=N;if(HasGun())SimpleSwitchToWeapon();
            NextJump=std::min(NextJump,N);RequestJump();
            return true;
        }
        if(DeviceStage==7)
        {
            auto V=Pawn->GetVelocity();auto Here=Pawn->K2_GetActorLocation();
            if(Pawn->bIsSkydiving||Pawn->bIsParachuteOpen||(V.Z>650.f&&Here.Z-DeviceCenter.Z>150.f))
            {FinishDevice(true);return true;}
            if(!ActiveDevice.Get()){FinishDevice();return false;}
            if(N>=NextDeviceTrigger)
            {
                NextDeviceTrigger=N+1.f;float Capsule=Pawn->CapsuleComponent?Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight():88.f;
                if(Syndicate::Distance2D(Here,DeviceCenter)<180.f&&std::abs(Here.Z-Capsule-DeviceCenter.Z)<120.f)
                    if(auto Trap=ActiveDevice.Get();Trap&&Trap->IsOverlappingActor(Pawn))
                    {
                        if(auto Launcher=Cast<AFortLauncherAthena>(Trap))Launcher->ReTriggerForPawn(Pawn);
                        else Trap->TriggerIfAppropriate();
                    }
            }
            WalkBuildSurface(DeviceCenter+FVector(0,0,90.f),25.f);RequestJump();return true;
        }
        FinishDevice();return false;
    }
