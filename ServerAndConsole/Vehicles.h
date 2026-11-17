#pragma once
#include "SyndicateRuntime.h"
namespace Vehicles
{
inline bool SpawnVehicleAt(AFortAthenaVehicleSpawner *S)
{
    if (!S)
        return false;
    auto C = S->GetVehicleClass();
    if (!C || !Cast<AFortAthenaVehicle>(C->DefaultObject))
        return false;
    return SpawnActor<AActor>(S->K2_GetActorLocation(), S->K2_GetActorRotation(), nullptr, C) != nullptr;
}
} // namespace Vehicles
