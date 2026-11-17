#pragma once
// Included inside DiagnosticGameplayTests and compiled only for the owned test host.
inline void QuadTick(AFortGameModeAthena *GM)
{
    static int State=0;static float NextAt=0.f;static Syndicate::Ref<AFortAthenaVehicle> TestVehicle;
    static bool SeatRequested=false;
    static FVector Entry;static Syndicate::Ref<AFortPlayerControllerAthena> TestPC;
    if(State>=5) return;
    float N=Syndicate::Now();if(N<NextAt) return;NextAt=N+.5f;
    AFortPlayerControllerAthena *PC=TestPC.Get();
    if(!PC) for(auto C:GM->AlivePlayers) if(C&&C->Pawn&&C->AcknowledgedPawn==C->Pawn) {PC=C;TestPC.Set(C);break;}
    auto P=PlayerVehicles::PawnFor(PC);if(!P||P->IsDead()) return;
    if(State==0)
    {
        Record("Private native gameplay test started. Quadcrasher movement/exit fault-injection fixture.");
        UClass *Class=nullptr;
        for(auto Ref:Syndicate::ActorsOf<AFortAthenaVehicleSpawner>())
            if(auto S=Ref.Get()) if(auto C=S->GetVehicleClass())
                if(auto Name=C->GetName();Name.contains("Antelope")||Name.contains("Quadcrasher")) {Class=C;break;}
        if(!Class) {Record("Quad fixture FAIL: no loaded Quadcrasher spawner class; not substituted.");State=5;return;}
        auto V=SpawnActor<AFortAthenaVehicle>(P->K2_GetActorLocation()+FVector(-180,0,50),{},nullptr,Class);
        if(!V) {Record("Quad fixture FAIL: native spawn rejected.");State=5;return;}
        TestVehicle.Set(V);Entry=V->K2_GetActorLocation();Record("Quad fixture native class: "+Class->GetName());
        State=1;NextAt=N+1.f;return;
    }
    auto V=TestVehicle.Get();if(!V) {Record("Quad fixture FAIL: vehicle lost.");State=5;return;}
    if(State==1)
    {
        auto C=Cast<UFortControllerComponent_Interaction>(PC->GetComponentByClass(UFortControllerComponent_Interaction::StaticClass()));
        if(!C) {Record("Quad fixture FAIL: native controller interaction component absent.");State=5;return;}
        Record("Quad fixture native interaction: controller RPC; canInteract="+std::to_string(V->BP_CanInteract(PC)));
        C->ServerAttemptInteract(V,Cast<UPrimitiveComponent>(V->RootComponent),ETInteractionType::IT_Simple,nullptr);
        State=2;NextAt=N+2.f;return;
    }
    if(State==2)
    {
        Record("Quad fixture seat state: vehicle="+std::to_string(P->GetVehicle()==V)+" driver="+std::to_string(V->GetDriver()==P)+" seat="+std::to_string(V->FindSeatIndex(P)));
        if(P->GetVehicle()==V&&V->GetDriver()!=P&&!SeatRequested)
        {SeatRequested=true;PC->ServerRequestSeatChange(0);NextAt=N+2.f;return;}
        if(P->GetVehicle()!=V||V->GetDriver()!=P) {Record("Quad fixture FAIL: native seat entry did not occur; not forced.");State=5;return;}
        Record("Quad fixture native seat entry: PASS");
        FReplicatedPhysicsPawnState Move{};auto At=V->K2_GetActorLocation()+FVector(2500,0,50);
        Move.Translation.X=At.X;Move.Translation.Y=At.Y;Move.Translation.Z=At.Z;
        // Stop at the moved position: the exit assertion must not confuse
        // continuing 100 m/s vehicle travel with a stale character handoff.
        Move.Rotation=FRotToQuat(V->K2_GetActorRotation());
        V->ServerMove(Move);
        Record(std::string("Quad fixture native movement RPC: ")+((V->K2_GetActorLocation()-At).Magnitude()<20.f?"PASS":"FAIL"));
        // Reproduce a regressed server physics root immediately before exit.
        // This is an explicit test fault, not normal driving or bot gameplay.
        if(auto Root=Cast<UPrimitiveComponent>(V->RootComponent))
        {FHitResult Hit{};Root->K2_SetWorldLocationAndRotation(Entry,V->K2_GetActorRotation(),false,&Hit,true);}
        PC->ServerAttemptExitVehicle();
        Record("Quad fixture exit return: vehicleDisplacement="+std::to_string((V->K2_GetActorLocation()-Entry).Magnitude())+
            " pawnDisplacement="+std::to_string((P->K2_GetActorLocation()-Entry).Magnitude())+
            " pending="+std::to_string(PlayerVehicles::Exits.size()));
        State=3;NextAt=N+.25f;return;
    }
    if(State==3)
    {
        bool Exit=!P->GetVehicle()&&!P->IsInVehicle();
        float Distance=(P->K2_GetActorLocation()-V->K2_GetActorLocation()).Magnitude();
        bool Away=(V->K2_GetActorLocation()-Entry).Magnitude()>1500.f;
        float PawnAway=(P->K2_GetActorLocation()-Entry).Magnitude();
        Record("Quad fixture exit after stale-root fault: "+std::string(Exit&&Distance<1200.f&&Away&&PawnAway>1500.f?"PASS":"FAIL")+
            " exited="+std::to_string(Exit)+" distanceToVehicle="+std::to_string(Distance)+" vehicleAwayFromEntry="+std::to_string(Away)+
            " pawnDisplacement="+std::to_string(PawnAway));
        Record("Private native gameplay test completed. Quad fixture only; ordinary driving remains a separate check.");State=5;
    }
}
