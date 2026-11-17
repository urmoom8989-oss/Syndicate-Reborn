#pragma once
// Included inside PlayerBot: a bounded native-input escape from repeated
// accepted/retired route requests. No pose, velocity or collision writes.
    bool StartGroundRescue(const BotBrain::Facts &F)
    {
        float N=Syndicate::Now();
        if((F.Enemy && F.CanFire && !HasGoal && FightFailureReason==0) ||
           (F.Incoming && F.CanFire) || N<GroundRescueAt || N-Navigation.At<.75f ||
           FightBuild.Active() || QueuedBuild.Get() || WallEdit.Active() || HealingStarted ||
           RampActive || WaitingForPickup() ||
           (HarvestTarget.Get() && HarvestTarget.Get()->GetDistanceTo(Pawn)<300.f) ||
           (BreachTarget.Get() && BreachTarget.Get()->GetDistanceTo(Pawn)<300.f)) return false;
        if(HasGoal && !LocalSteering && N-Navigation.RequestedAt<1.5f) return false;
        // An accepted path with no actual motion loses ownership after its
        // bounded attempt; repeated mission selection cannot hide the stall.
        GroundRescueAt=N+.6f;auto Here=Pawn->K2_GetActorLocation();
        FVector Toward=MoveGoal;
        if(F.Rotate) Toward=RotationGoal;
        else if(auto Target=LootTarget.Get()) Toward=Target->K2_GetActorLocation();
        else if(auto Target=HarvestTarget.Get()) Toward=Target->K2_GetActorLocation();
        else if(Investigation.Active(N)) Toward=Investigation.Point;
        auto D=Toward-Here;float L=std::hypot(D.X,D.Y);if(L<1.f) D=FVector(1,0,0);else D=FVector(D.X/L,D.Y/L,0);
        const float Angles[]={90.f,-90.f,45.f,-45.f,135.f,-135.f,0.f,180.f};
        int Start=static_cast<int>((N-Navigation.At)/.8f)%8;
        for(int I=0;I<8;++I)
        {
            float A=Angles[(Start+I)%8]*3.14159265f/180.f;
            FVector Heading(D.X*std::cos(A)-D.Y*std::sin(A),D.X*std::sin(A)+D.Y*std::cos(A),0);
            auto At=Here+Heading*180.f;
            if(!AllowedDestination(At) || !BotPhysics::NativeGroundStep(Pawn,Heading)) continue;
            StopMove();SetFire(false);SetADS(false);NextPath=N+.7f;
            GroundRescueGoal=At;GroundRescueUntil=N+.6f;MoveGoal=At;MoveRadius=30.f;
            HasGoal=LocalSteering=GroundGoal=true;RampSteering=false;SteeringDirection=Heading;
            NextSteer=N;Look(At+FVector(0,0,50.f));Sprint(true);++Brain.Recoveries;return true;
        }
        return false;
    }
