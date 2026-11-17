#pragma once
#include <filesystem>
// Private owned-session integration checks, excluded from public modules.
namespace DiagnosticGameplayTests
{
inline void Record(const std::string &S)
{
 wchar_t Temp[MAX_PATH]{};GetTempPathW(MAX_PATH,Temp);
 std::ofstream F(std::filesystem::path(Temp)/(L"SyndicateSkillTests-"+std::to_wstring(GetCurrentProcessId())+L".log"),std::ios::app);F<<S<<std::endl;
}
inline float CensusAt=0.f;inline int Stage=0;inline float Next=0.f;inline bool ExtraChecked=false;inline PlayerBots::PlayerBot *EmoteBot=nullptr;inline float EmoteAt=0.f;inline PlayerBots::PlayerBot *HealBot=nullptr;inline float HealWatchAt=0.f;inline bool HealBoxSeen=false,HealReported=false;
inline PlayerBots::PlayerBot *PadBot=nullptr;inline float PadAt=0.f;inline bool PadChecked=false;inline bool BounceChecked=false;inline int PadTries=0;inline std::vector<PlayerBots::PlayerBot*> PadTried;
inline void CheckDevice(float N)
{
 if(PadBot)
 {
  if(std::find(PlayerBots::PlayerBotArray.begin(),PlayerBots::PlayerBotArray.end(),PadBot)==PlayerBots::PlayerBotArray.end()||!PadBot->Valid()){Record("PAD INCONCLUSIVE: fixture bot eliminated.");PadBot=nullptr;return;}
  Record(std::format("PAD watch age={} stage={} placed={} flights={} velocityZ={} sky={} glide={} held={} result={}",N-PadAt,PadBot->DeviceStage,PadBot->DevicesPlaced,PadBot->DeviceFlights,PadBot->Pawn->GetVelocity().Z,PadBot->Pawn->bIsSkydiving,PadBot->Pawn->bIsParachuteOpen,PadBot->Pawn->CurrentWeapon?PadBot->Pawn->CurrentWeapon->GetName():"none",PadBot->DeviceResult));
  if(auto T=PadBot->ActiveDevice.Get())
  {FVector C{},E{};T->GetActorBounds(true,&C,&E);Record(std::format("PAD body pawn={}/{}/{} trap={}/{}/{} extent={}/{}/{} excludeAI={} triggerParts={}",PadBot->Pawn->K2_GetActorLocation().X,PadBot->Pawn->K2_GetActorLocation().Y,PadBot->Pawn->K2_GetActorLocation().Z,C.X,C.Y,C.Z,E.X,E.Y,E.Z,bool(T->TriggerFilter.bExcludeAllAI),T->TriggerComponents.Num()));}
  if(PadBot->DeviceFlights||N-PadAt>12.f)
  {Record(std::format("PAD {}: realDevice={} nativeFlight={} kind={}",PadBot->DevicesPlaced&&PadBot->DeviceFlights?"PASS":"INCONCLUSIVE",PadBot->DevicesPlaced,PadBot->DeviceFlights,BounceChecked?"bounce":"launch"));PadBot->Pawn->bCanBeDamaged=true;if(!PadBot->DeviceFlights&&PadTries<5)PadChecked=false;else if(PadBot->DeviceFlights&&!BounceChecked){BounceChecked=true;PadChecked=false;}PadBot=nullptr;}
  return;
 }
 if(PadChecked)return;
 for(auto B:PlayerBots::PlayerBotArray)if(B&&B->Valid()&&B->LandedAt>0.f&&N-B->LandedAt>10.f&&B->CelebrationSafe()&&std::find(PadTried.begin(),PadTried.end(),B)==PadTried.end())
 {
  bool Quiet=true;for(auto Ref:PlayerBots::PawnCache)if(auto Other=Ref.Get();Other&&Other!=B->Pawn&&!Other->IsDead()&&Other->GetDistanceTo(B->Pawn)<10000.f){Quiet=false;break;}if(!Quiet)continue;
  FVector Center{},Half{};float Story=0.f;if(!B->BuildingCell(Center,Half,Story)||!B->HeightHeadroom(Center))continue;
  auto D=StaticLoadObject<UFortTrapItemDefinition>("/Game/Athena/Items/Traps/TID_Floor_Player_Launch_Pad_Athena.TID_Floor_Player_Launch_Pad_Athena");
  if(BounceChecked)D=StaticLoadObject<UFortTrapItemDefinition>("/Game/Athena/Items/Traps/TID_Context_BouncePad_Athena.TID_Context_BouncePad_Athena");
  if(!D){Record("PAD INCONCLUSIVE: launch pad definition unavailable.");PadChecked=true;return;}
  ++PadTries;PadTried.push_back(B);B->GiveItem(D,1);auto Wood=UFortKismetLibrary::K2_GetResourceItemDefinition(EFortResourceType::Wood);B->GiveItem(Wood,100);
  B->Enemy.Clear();B->NextSense=N+8.f;B->LastDamageTime=B->LastShotCueAt=-100.f;B->Pawn->bCanBeDamaged=false;
  auto E=FortInventory::FindItemEntry(B->PC,D);
  bool Began=E&&B->BeginDevice(*E,BounceChecked?BotDevices::Kind::Bounce:BotDevices::Kind::Launch,Center+FVector(BounceChecked?2000.f:12000.f,0,BounceChecked?400.f:0.f));
  Record(std::format("PAD fixture began={} source={} center={}/{}/{}",Began,D->GetName(),Center.X,Center.Y,Center.Z));
  if(Began){PadBot=B;PadAt=N;}else B->Pawn->bCanBeDamaged=true;
  PadChecked=Began||PadTries>=8;return;
 }
}
inline void Tick(AFortGameModeAthena *GM)
{
 if(std::wstring_view(GetCommandLineW()).find(L"-SyndicateGameplaySelfTest")==std::wstring_view::npos)return;
 auto GS=GM?Cast<AFortGameStateAthena>(GM->GameState):nullptr;if(!GS)return;
 AFortPlayerControllerAthena *PC=nullptr;AFortPlayerPawnAthena *P=nullptr;
 for(auto C:GM->AlivePlayers)if(auto Pawn=C?Cast<AFortPlayerPawnAthena>(C->Pawn):nullptr;Pawn&&!Pawn->IsDead()){PC=C;P=Pawn;break;}
 if(!P)return;P->bCanBeDamaged=false; // Owned observer; no public behavior change.
 float N=Syndicate::Now();
 static float NextAimProbe=0;static int AimSamples=0;
 if(GS->GamePhase==EAthenaGamePhase::SafeZones&&N>=NextAimProbe&&AimSamples<60)
   for(auto B:PlayerBots::PlayerBotArray)if(B&&B->Valid()&&B->bHasJumpedFromBus&&B->Firing&&B->Enemy.Get())
    if(auto W=B->Pawn->CurrentWeapon;W&&PlayerBots::IsGun(W->WeaponData)&&N-W->LastFireTime<.25f)
    {
      auto Control=B->PC->GetControlRotation();
      auto Base=B->Pawn->GetBaseAimRotation(),Native=B->Pawn->GetDefaultTargetingRotation();
      auto Cam=UKismetMathLibrary::Quat_Rotator(B->Pawn->GetTargetingTransform(EFortAbilityTargetingSource::Camera,nullptr).Rotation);
      auto Focus=UKismetMathLibrary::Quat_Rotator(B->Pawn->GetTargetingTransform(EFortAbilityTargetingSource::PawnTowardsFocus,nullptr).Rotation);
      auto Body=B->Pawn->K2_GetActorRotation();auto D=W->CurrentAdjustedAimDirection;
      auto Shot=UKismetMathLibrary::FindLookAtRotation(FVector{},D);
      Record(std::format("AIM nativeShot gun={} aim={}/{} base={}/{} default={}/{} camera={}/{} focus={}/{} adjusted={}/{} body={} mag={} control={}/{} actorFocus={}",W->WeaponData->GetName(),B->Aim.Pitch,B->Aim.Yaw,Base.Pitch,Base.Yaw,Native.Pitch,Native.Yaw,Cam.Pitch,Cam.Yaw,Focus.Pitch,Focus.Yaw,Shot.Pitch,Shot.Yaw,Body.Yaw,W->AmmoCount,Control.Pitch,Control.Yaw,B->CombatActorFocus));
      ++AimSamples;NextAimProbe=N+.25f;break;
    }
 if(N<Next)return;Next=N+1.f;
 if(Stage==0)
 {
  auto Class=StaticLoadObject<UClass>("/Game/Athena/SupplyDrops/AthenaSupplyDrop.AthenaSupplyDrop_C");
  if(!Class)Class=AAthenaSupplyDrop_C::StaticClass();
  auto At=P->K2_GetActorLocation()+FVector(250,0,50.f);
  auto Drop=SpawnActor<AAthenaSupplyDrop_C>(At,{},nullptr,Class,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if(!Drop){Record("SUPPLY FAIL: native crate spawn missing.");Stage=1;return;}
  Drop->GroundLocation=At;Drop->LandingLocation=At;Drop->bGroundLocationInitialized=true;Drop->FallHeight=0.f;
  if(Drop->ProjectileMovement){Drop->ProjectileMovement->StopMovementImmediately();Drop->ProjectileMovement->Deactivate();}
  FHitResult Hit{};Drop->K2_SetActorLocation(At,false,&Hit,false);
  Record(std::format("SUPPLY fixture distance={} gameThread={} currentThread={}",Drop->GetDistanceTo(P),PE::GameThread.load(),GetCurrentThreadId()));
  auto C=Cast<UFortControllerComponent_Interaction>(PC->GetComponentByClass(UFortControllerComponent_Interaction::StaticClass()));
  auto Count=[&](){int K=0;for(auto Ref:Syndicate::ActorsOf<AFortPickup>())if(auto A=Ref.Get();A&&Syndicate::Distance2D(A->K2_GetActorLocation(),At)<1300.f)++K;return K;};
  int Before=Count();
  if(C)C->ServerAttemptInteract(Drop,Cast<UPrimitiveComponent>(Drop->RootComponent),ETInteractionType::IT_Simple,nullptr);
  int After=Count();bool Done=InteractionLoot::Track(Drop).Done;
  if(C)C->ServerAttemptInteract(Drop,Cast<UPrimitiveComponent>(Drop->RootComponent),ETInteractionType::IT_Simple,nullptr);
  Record(std::format("SUPPLY {}: controller={} tier={} output={} once={} opened={}",Done&&After>Before&&Count()==After?"PASS":"FAIL",C!=nullptr,Drop->LootTableName.ToString(),After-Before,Count()==After,Drop->Opened));
  if(!Done){UDataTable *T=nullptr,*Pk=nullptr;if(Looting::Tables(T,Pk))for(auto &V:T->RowMap){auto R=reinterpret_cast<FFortLootTierData*>(V.Value());if(R&&R->TierGroup.ToString().contains("Supply"))Record("SUPPLY native tier "+R->TierGroup.ToString());}}
  Stage=1;Record("Normal map-wide population/landing test continues.");
 }
 if(EmoteBot&&N>=EmoteAt+2.f)
 {
  if(std::find(PlayerBots::PlayerBotArray.begin(),PlayerBots::PlayerBotArray.end(),EmoteBot)!=PlayerBots::PlayerBotArray.end()&&EmoteBot->Valid())
  {
   bool Playing=EmoteBot->Pawn->bIsPlayingEmote;EmoteBot->LastShotCueAt=N;bool Held=EmoteBot->TickCelebration();
   Record(std::format("EMOTE nativePlaying={} cancelledOnShot={}",Playing,!Held&&!EmoteBot->Celebration.Active));
  }
  EmoteBot=nullptr;
 }
 if(Stage==1&&GS->GamePhase==EAthenaGamePhase::SafeZones)
 {
  CheckDevice(N);
  if(N<CensusAt)return;CensusAt=N+20.f;
  int Armed=0,FiveSlots=0,Ground=0,Building=0,Carried=0,Multiple=0;unsigned Crests=0,Transfers=0;float ClimbGain=0.f;
  for(auto B:PlayerBots::PlayerBotArray)if(B&&B->Valid()&&B->bHasJumpedFromBus&&!B->Pawn->bIsSkydiving&&!B->Pawn->bIsParachuteOpen)
  {++Ground;Armed+=B->HasGun();int Primary=FortInventory::PrimaryCount(B->PC);FiveSlots+=Primary==5;Carried+=Primary;Multiple+=Primary>=2;Building+=B->FightBuild.Active();Crests+=B->StairCrests;Transfers+=B->LootTransfers;if(B->CombatHeightBudget.Seeded)ClimbGain=std::max(ClimbGain,B->Pawn->K2_GetActorLocation().Z-B->CombatHeightBudget.Base);}
  if(!ExtraChecked)for(auto B:PlayerBots::PlayerBotArray)
   if(B&&B->Valid()&&B->bHasJumpedFromBus&&!B->Pawn->bIsSkydiving&&!B->Pawn->bIsParachuteOpen&&B->CelebrationSafe())
   {
    auto Wood=UFortKismetLibrary::K2_GetResourceItemDefinition(EFortResourceType::Wood);int Before=FortInventory::CountDefinition(B->PC,Wood);
    auto Ammo=StaticLoadObject<UFortAmmoItemDefinition>("/Game/Athena/Items/Ammo/AthenaAmmoDataShells.AthenaAmmoDataShells");
    int AB=FortInventory::CountDefinition(B->PC,Ammo);B->AmmoIncome.Next=N;B->TickResources();int AA=FortInventory::CountDefinition(B->PC,Ammo);B->TickResources();
    Record(std::format("AMMO {}: before={} after={} repeat={}",AA==AB+1&&FortInventory::CountDefinition(B->PC,Ammo)==AA?"PASS":"FAIL",AB,AA,FortInventory::CountDefinition(B->PC,Ammo)));
    B->MaterialIncome.Next=N;B->TickResources();int After=FortInventory::CountDefinition(B->PC,Wood);B->TickResources();
    Record(std::format("MATERIAL {}: before={} after={} repeat={}",After==std::min(999,Before+10)&&FortInventory::CountDefinition(B->PC,Wood)==After?"PASS":"FAIL",Before,After,FortInventory::CountDefinition(B->PC,Wood)));
    B->Celebration.Offer(N,0,true);bool Started=B->TickCelebration();EmoteBot=B;EmoteAt=N;
    Record(std::format("EMOTE start requested={}",Started));
    ExtraChecked=true;
    break;
   }
  if(!HealBot)for(auto B:PlayerBots::PlayerBotArray)
   if(B&&B->Valid()&&B->bHasJumpedFromBus&&!B->Pawn->bIsSkydiving&&!B->Pawn->bIsParachuteOpen&&B->CelebrationSafe())
   {
    if(B->MaterialCount()<60)continue;
    bool HasHealth=false;if(auto I=FortInventory::Get(B->PC))for(auto E:I->Inventory.ReplicatedEntries)HasHealth|=E.Count>0&&B->KitSlot(E.ItemDefinition)==4;if(!HasHealth)continue;
    TArray<AActor*> Ignored;Ignored.Add(B->Pawn);FHitResult H{};
    auto Here=B->Pawn->K2_GetActorLocation();bool Roof=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),Here+FVector(0,0,100),Here+FVector(0,0,1200),BotPhysics::PawnProfile(),false,Ignored,EDrawDebugTrace::None,&H,true,{},{},0);Ignored.Free();
    if(!Roof){HealBot=B;HealWatchAt=N;B->Pawn->SetHealth(30.f);B->LastHealth=30.f;B->Pawn->bCanBeDamaged=false;B->LastDamageTime=B->LastShotCueAt=-100.f;Record("NATIVE HEAL episode started outdoors.");break;}
   }
  if(HealBot&&!HealReported&&std::find(PlayerBots::PlayerBotArray.begin(),PlayerBots::PlayerBotArray.end(),HealBot)!=PlayerBots::PlayerBotArray.end()&&HealBot->Valid())
  {
    int Pieces=0;for(auto Ref:HealBot->BoxWalls)if(auto B=Ref.Get();B&&!B->bDestroyed)++Pieces;
    if(Pieces==6)HealBoxSeen=true;
    if(HealBot->Pawn->GetHealth()>30.f||N-HealWatchAt>35.f)
    {Record(std::format("NATIVE HEAL {}: sixPieceBox={} health={} shield={} pieces={} healingStarted={} boxActive={} held={} suppressUntil={} damageAgo={}",HealBoxSeen&&HealBot->Pawn->GetHealth()>30.f?"PASS":"INCONCLUSIVE",HealBoxSeen,HealBot->Pawn->GetHealth(),HealBot->Pawn->GetShield(),Pieces,HealBot->HealingStarted,HealBot->BoxActive,HealBot->Pawn->CurrentWeapon?HealBot->Pawn->CurrentWeapon->GetName():"none",HealBot->SuppressHealUntil,N-HealBot->LastDamageTime));HealBot->Pawn->bCanBeDamaged=true;HealReported=true;}
  }
  unsigned Pushers=0;for(auto B:PlayerBots::PlayerBotArray)if(B&&B->Valid()&&B->PressureHits>=2&&N<B->PressureUntil)++Pushers;
  Record("NATIVE CONFIRMED HIT PUSHERS="+std::to_string(Pushers));
  Record(std::format("NATIVE MATCH time={} bots={} grounded={} armed={} fiveSlotKits={} building={} crests={} realLootTransfers={} maxEncounterClimb={} carriedItems={} multiItemBots={}",N,GM->AliveBots.Num(),Ground,Armed,FiveSlots,Building,Crests,Transfers,ClimbGain,Carried,Multiple));
 }
}
}

