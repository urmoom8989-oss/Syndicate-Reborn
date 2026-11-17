#pragma once
#include "PlayerBots.h"
namespace AIController
{
void (*OnActorBumpOG)(UPathFollowingComponent *, AActor *, AActor *, const FVector &, FHitResult &) = nullptr;
void OnActorBump(UPathFollowingComponent *Comp, AActor *Self, AActor *Other, const FVector &Impulse, FHitResult &Hit)
{
    // Keep the original navigation callback and throttle physical jump recovery. Never pause AI or harvest every
    // collision.
    if (Comp && Self && Other)
        for (auto B : PlayerBots::PlayerBotArray)
            if (B && B->Valid() && B->Pawn == Self && B->BotState >= PlayerBots::EBotState::Landed)
            {
                float N = Syndicate::Now();
                // A pawn bump is not an obstacle worth jumping. Let native
                // collision/avoidance resolve it; real combat jumps share a cooldown.
                if (N > B->NextJump && !Cast<APawn>(Other) && B->HasGoal &&
                    std::hypot(B->Pawn->GetVelocity().X, B->Pawn->GetVelocity().Y) < 30.f)
                    B->RequestJump();
                break;
            }
    if (OnActorBumpOG)
        OnActorBumpOG(Comp, Self, Other, Impulse, Hit);
}
void HookAll()
{
    HookVTable<UPathFollowingComponent>(0x8D, OnActorBump, (LPVOID *)&OnActorBumpOG);
    Log("AIController hooked.");
}
} // namespace AIController
