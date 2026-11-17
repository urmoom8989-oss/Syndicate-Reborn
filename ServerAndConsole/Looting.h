#pragma once
#include "SyndicateRuntime.h"
namespace Looting
{
inline const FFortBaseWeaponStats *GetWeaponStats(UFortItemDefinition *D)
{
    auto W = Cast<UFortWeaponItemDefinition>(D);
    if (!W || !W->WeaponStatHandle.DataTable)
        return nullptr;
    auto &H = W->WeaponStatHandle;
    for (auto &R : H.DataTable->RowMap)
        if (R.Key() == H.RowName)
            return reinterpret_cast<FFortBaseWeaponStats *>(R.Value());
    return nullptr;
}
inline int GetClipSize(UFortItemDefinition *D)
{
    auto S = GetWeaponStats(D);
    return S ? std::max(0, S->ClipSize) : 0;
}
template <class T, class F> inline T Weighted(const std::vector<T> &Rows, F Weight)
{
    double Sum = 0;
    for (auto R : Rows)
    {
        float W = Weight(R);
        if (std::isfinite(W) && W > 0)
            Sum += W;
    }
    if (!(Sum > 0) || !std::isfinite(Sum))
        return T{};
    double Draw = Syndicate::Between(0.f, static_cast<float>(Sum));
    T Last{};
    for (auto R : Rows)
    {
        float W = Weight(R);
        if (!std::isfinite(W) || W <= 0)
            continue;
        Last = R;
        Draw -= W;
        if (Draw <= 0)
            return R;
    }
    return Last;
}
// Cache successful and failed native lookups per world/playlist. A missing table
// must not cause a full registry/string scan for every floor-loot marker.
inline Syndicate::Ref<UWorld> TableWorld;
inline Syndicate::Ref<UFortPlaylistAthena> TablePlaylist;
inline Syndicate::Ref<UDataTable> TierTable, PackageTable;
inline float NextTableRetry = 0.f;
inline bool TableAttempted = false;
inline int TableLookupIndex = 0;
inline bool MissingTablesLogged = false;
inline bool Tables(UDataTable *&Tier, UDataTable *&Package)
{
    Tier = Package = nullptr;
    auto W = UWorld::GetWorld();
    auto G = W ? Cast<AFortGameStateAthena>(W->GameState) : nullptr;
    if (!G)
        return false;
    auto P = G->CurrentPlaylistInfo.BasePlaylist;
    if (TableWorld.Get() != W || TablePlaylist.Get() != P)
    {
        TableWorld.Set(W);
        TablePlaylist.Set(P);
        TierTable.Clear();
        PackageTable.Clear();
        TableAttempted = MissingTablesLogged = false;
        NextTableRetry = 0.f;
        TableLookupIndex = 0;
    }
    Tier = TierTable.Get();
    Package = PackageTable.Get();
    if (Tier && Package)
        return true;
    float Now = Syndicate::Now();
    bool Retry = !TableAttempted || Now >= NextTableRetry;
    if (Retry)
    {
        TableAttempted = true;
        NextTableRetry = Now + 10.f;
        TableLookupIndex = 0;
    }
    if (Retry && P)
    {
        if (!Tier)
            Tier = Cast<UDataTable>(P->LootTierData.Get());
        if (!Package)
            Package = Cast<UDataTable>(P->LootPackages.Get());
        auto TierPath = P->LootTierData.ObjectID.AssetPathName.GetRawString();
        auto PackagePath = P->LootPackages.ObjectID.AssetPathName.GetRawString();
        if (!Tier && !TierPath.empty() && TierPath != "None")
            Tier = StaticLoadObject<UDataTable>(TierPath);
        if (!Package && !PackagePath.empty() && PackagePath != "None")
            Package = StaticLoadObject<UDataTable>(PackagePath);
    }
    // Native typed hash lookups for the legacy defaults. These do not walk GObjects.
    if (Retry && !Tier)
        Tier = StaticFindObject<UDataTable>(L"/Game/Items/Datatables/AthenaLootTierData_Client.AthenaLootTierData_Client");
    if (Retry && !Package)
        Package = StaticFindObject<UDataTable>(L"/Game/Items/Datatables/AthenaLootPackages_Client.AthenaLootPackages_Client");
    // Old default playlists can leave their soft paths empty. Resolve already
    // loaded legacy tables by FName, at most 512 registry slots per call. Never
    // build every object's full name and never restart the scan for each marker.
    static FName TierName = UKismetStringLibrary::Conv_StringToName(L"AthenaLootTierData_Client");
    static FName PackageName = UKismetStringLibrary::Conv_StringToName(L"AthenaLootPackages_Client");
    for (int Checked = 0; (!Tier || !Package) && Checked < 512 &&
                          TableLookupIndex < UObject::GObjects->Num(); ++Checked)
    {
        auto Object = UObject::GObjects->GetByIndex(TableLookupIndex++);
        if (!Object)
            continue;
        if (!Tier && Object->Name == TierName)
            Tier = Cast<UDataTable>(Object);
        if (!Package && Object->Name == PackageName)
            Package = Cast<UDataTable>(Object);
    }
    TierTable.Set(Tier);
    PackageTable.Set(Package);
    if (!Tier || !Package)
    {
        if (!MissingTablesLogged)
        {
            MissingTablesLogged = true;
            Log("Loot tables are unavailable; background floor-loot setup will retry without blocking player startup.");
        }
        return false;
    }
    Log("Native loot tables resolved and cached.");
    return true;
}
inline void Expand(UDataTable *Table, FName ID, int Category, int Depth, int &Budget, std::vector<FFortItemEntry> &Out)
{
    if (!Table || Depth >= 10 || Budget-- <= 0 || Out.size() >= 128)
        return;
    std::vector<FFortLootPackageData *> Rows;
    for (auto &P : Table->RowMap)
    {
        auto R = reinterpret_cast<FFortLootPackageData *>(P.Value());
        if (R && R->LootPackageID == ID && (Category < 0 || R->LootPackageCategory == Category) && R->Weight > 0)
            Rows.push_back(R);
    }
    auto R = Weighted(Rows, [](auto P) { return P ? P->Weight : 0.f; });
    if (!R)
        return;
    auto D = Cast<UFortItemDefinition>(R->ItemDefinition.Get());
    if (!D)
    {
        auto Path = R->ItemDefinition.ObjectID.AssetPathName.GetRawString();
        if (!Path.empty() && Path != "None")
            D = StaticLoadObject<UFortItemDefinition>(Path);
    }
    if (D && R->Count > 0)
    {
        FFortItemEntry E{};
        E.ItemDefinition = D;
        E.Count = std::min(R->Count, 999);
        E.LoadedAmmo = GetClipSize(D);
        Out.push_back(E);
        return;
    }
    auto Call = R->LootPackageCall.ToString();
    if (Call.empty() || Call == "None" || Call.contains(".Empty"))
        return;
    FName Next = UKismetStringLibrary::Conv_StringToName(FString(std::wstring(Call.begin(), Call.end()).c_str()));
    // Call rows can request multiple independent draws. A shared budget bounds cyclic tables.
    for (int i = 0; i < std::clamp(R->Count, 1, 16) && Budget > 0; ++i)
        Expand(Table, Next, -1, Depth + 1, Budget, Out);
}
inline std::vector<FFortItemEntry> PickLootDrops(FName Group, int Recursive = 0)
{
    std::vector<FFortItemEntry> Out;
    UDataTable *T = nullptr, *P = nullptr;
    if (!Tables(T, P))
        return Out;
    std::vector<FFortLootTierData *> Rows;
    for (auto &Pair : T->RowMap)
    {
        auto R = reinterpret_cast<FFortLootTierData *>(Pair.Value());
        if (R && R->TierGroup == Group && R->Weight > 0)
            Rows.push_back(R);
    }
    auto R = Weighted(Rows, [](auto Row) { return Row ? Row->Weight : 0.f; });
    if (!R || !std::isfinite(R->NumLootPackageDrops))
        return Out;
    float Requested = std::clamp(R->NumLootPackageDrops, 0.f, 64.f);
    int Drops = static_cast<int>(std::floor(Requested));
    if (Syndicate::Between(0, 1) < Requested - Drops)
        ++Drops;
    int N = R->LootPackageCategoryWeightArray.Num();
    if (N < 0 || N > 64 || N != R->LootPackageCategoryMinArray.Num() || N != R->LootPackageCategoryMaxArray.Num())
        return Out;
    int Budget = 128, Used = 0;
    std::vector<int> Counts(N, 0);
    if (!N)
    {
        for (int i = 0; i < Drops && Budget > 0; ++i)
            Expand(P, R->LootPackage, -1, 0, Budget, Out);
        return Out;
    }
    for (int C = 0; C < N; ++C)
    {
        int Min = std::clamp(R->LootPackageCategoryMinArray[C], 0, 64);
        int Max = R->LootPackageCategoryMaxArray[C];
        if (Max >= 0)
            Min = std::min(Min, Max);
        for (int i = 0; i < Min && Budget > 0; ++i)
        {
            Expand(P, R->LootPackage, C, 0, Budget, Out);
            ++Counts[C];
            ++Used;
        }
    }
    while (Used < Drops && Budget > 0)
    {
        std::vector<int> Candidates;
        for (int C = 0; C < N; ++C)
        {
            int Max = R->LootPackageCategoryMaxArray[C];
            if (Max < 0 || Counts[C] < Max)
                Candidates.push_back(C);
        }
        // Store category+1 so zero can indicate failure.
        std::vector<int> Keys;
        for (auto C : Candidates)
            Keys.push_back(C + 1);
        auto Key = Weighted(Keys, [R](int K) { return static_cast<float>(R->LootPackageCategoryWeightArray[K - 1]); });
        if (!Key)
            break;
        int C = Key - 1;
        Expand(P, R->LootPackage, C, 0, Budget, Out);
        ++Counts[C];
        ++Used;
    }
    return Out;
}
inline int SpawnFloorLootAt(ABuildingContainer *C)
{
    if (!C || C->bAlreadySearched || !C->GetName().contains("FloorLoot"))
        return 0;
    auto Drops = PickLootDrops(C->SearchLootTierGroup);
    int Spawned = 0;
    for (auto &E : Drops)
        if (SpawnPickup(E, C->K2_GetActorLocation(), EFortPickupSourceTypeFlag::FloorLoot,
                        EFortPickupSpawnSource::Unset))
            ++Spawned;
    if (Spawned)
    {
        C->bAlreadySearched = true;
        C->OnRep_bAlreadySearched();
    }
    return Spawned;
}
} // namespace Looting
