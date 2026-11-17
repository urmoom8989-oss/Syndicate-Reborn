#include "pch.h"
#include <vector>
#include <atomic>
#include <thread>
#include <set>
#include <unordered_set>
#include <functional>
#include <map>
#include "../Public/FortLootPackage.h"
#include "../Public/BuildingContainer.h"
#include "../Public/FortGameMode.h"
#include "../../Erbium/Public/Configuration.h"
#include "../Public/FortKismetLibrary.h"

struct FFortLootLevelData
{
public:
	USCRIPTSTRUCT_COMMON_MEMBERS(FFortLootLevelData);

	DEFINE_STRUCT_PROP(Category, FName);
	DEFINE_STRUCT_PROP(category, FName);
	DEFINE_STRUCT_PROP(LootLevel, int32);
	DEFINE_STRUCT_PROP(MinItemLevel, int32);
	DEFINE_STRUCT_PROP(MaxItemLevel, int32);
};

int GetLevel(const FDataTableCategoryHandle& CategoryHandle)
{
	auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
	auto GameState = (AFortGameStateAthena*)GameMode->GameState;

	if (!CategoryHandle.DataTable)
		return 0;

	if (!CategoryHandle.ColumnName)
		return 0;

	if (!CategoryHandle.RowContents.ComparisonIndex)
		return 0;

	int Level = 0;
	FFortLootLevelData* LootLevelData = nullptr;
	
	for (auto& LootLevelDataPair : (TMap<FName, FFortLootLevelData*>)CategoryHandle.DataTable->RowMap)
	{
		if (!LootLevelDataPair.Value())
			continue;

		if ((FFortLootLevelData::HasCategory() ? LootLevelDataPair.Value()->Category : LootLevelDataPair.Value()->category) != CategoryHandle.RowContents || LootLevelDataPair.Value()->LootLevel > GameState->WorldLevel || LootLevelDataPair.Value()->LootLevel <= Level)
			continue;

		Level = LootLevelDataPair.Value()->LootLevel;
		LootLevelData = LootLevelDataPair.Value();
	}

	if (LootLevelData)
	{
		auto subbed = LootLevelData->MaxItemLevel - LootLevelData->MinItemLevel;

		if (subbed <= -1)
			subbed = 0;
		else
		{
			auto calc = (int)(((float)rand() / 32767) * (float)(subbed + 1));
			if (calc <= subbed)
				subbed = calc;
		}

		return subbed + LootLevelData->MinItemLevel;
	}

	return 0;
}

struct FWeaponPickupAmmoCountData
{
public:
	USCRIPTSTRUCT_COMMON_MEMBERS(FWeaponPickupAmmoCountData);

	DEFINE_STRUCT_PROP(AmmoItemDefinitionTag, FGameplayTag);
	DEFINE_STRUCT_PROP(SpawnCount, FScalableFloat);
};

struct FHelperGameplayTagToAmmoCountMultiplier
{
public:
	USCRIPTSTRUCT_COMMON_MEMBERS(FHelperGameplayTagToAmmoCountMultiplier);

	DEFINE_STRUCT_PROP(Tag, FGameplayTag);
	DEFINE_STRUCT_PROP(CountMultiplier, FScalableFloat);
};

class UFortWeaponPickupSpawnAmmoData : public UObject
{
public:
	UCLASS_COMMON_MEMBERS(UFortWeaponPickupSpawnAmmoData);

	DEFINE_PROP(WeaponPickupAmmoCountArray, TArray<FWeaponPickupAmmoCountData>);
	DEFINE_PROP(DefaultWeaponAmmoMultiplier, FScalableFloat);
	DEFINE_PROP(WeaponPickupAmmoMultiplierOverrideArray, TArray<FHelperGameplayTagToAmmoCountMultiplier>);
	DEFINE_PROP(SourceToAmmoMultiplierOverrideArray, TArray<FHelperGameplayTagToAmmoCountMultiplier>);
};


// Syndicate: Season X Batman items (Grapnel Gun, Explosive Batarang). Their exact item names are inside the
// encrypted Batman pack (code name "Badger"), so once the island has loaded the server reads the pack's
// file list from memory (in the background), keeps the item definitions, and writes every Badger file it
// saw to Runtime\\Server\\batman-assets.txt.
static std::vector<std::wstring> SyndicateBatmanPaths;
static std::atomic<bool> SyndicateBatmanScanned{ false };
void SyndicateFindBatmanItems()
{
    static bool Started = false; if (Started) return; Started = true;
    wchar_t Ready[32768]{}; std::wstring Folder;
    if (GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", Ready, 32768)) { Folder = Ready; auto Slash = Folder.find_last_of(L"\\/"); Folder.resize(Slash == std::wstring::npos ? 0 : Slash + 1); }
    std::thread([Folder]() {
        std::set<std::string> Files;
        std::vector<uint8_t> Buffer(4 << 20);
        auto IsChar = [](unsigned c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '.' || c == '/' || c == '-'; };
        const char* Word = "Badger"; const size_t Len = 6;
        MEMORY_BASIC_INFORMATION Info{};
        for (uintptr_t At = 0x10000; Files.size() < 4000 && VirtualQuery((void*)At, &Info, sizeof(Info)) == sizeof(Info);)
        {
            uintptr_t Next = (uintptr_t)Info.BaseAddress + Info.RegionSize; if (Next <= At) break;
            bool Readable = Info.State == MEM_COMMIT && !(Info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && (Info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY));
            for (uintptr_t Chunk = (uintptr_t)Info.BaseAddress; Readable && Chunk < Next; Chunk += Buffer.size() - 512)
            {
                SIZE_T Want = (SIZE_T)(std::min<uintptr_t>)(Buffer.size(), Next - Chunk), Got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), (void*)Chunk, Buffer.data(), Want, &Got) || Got < 16) break;
                auto Data = Buffer.data();
                for (int Wide = 0; Wide < 2; Wide++)
                {
                    size_t Step = Wide ? 2 : 1;
                    for (size_t i = 0; i + Len * Step < Got; i++)
                    {
                        if (Data[i] != 'B') continue;
                        bool Match = true; for (size_t k = 1; k < Len && Match; k++) Match = Data[i + k * Step] == (uint8_t)Word[k] && (!Wide || Data[i + k * Step - 1] == 0);
                        if (!Match || (Wide && Data[i + 1] != 0)) continue;
                        size_t Start = i; while (Start >= Step && IsChar(Data[Start - Step]) && (!Wide || Data[Start - Step + 1] == 0)) Start -= Step;
                        size_t End = i; while (End + Step <= Got && IsChar(Data[End]) && (!Wide || (End + 1 < Got && Data[End + 1] == 0))) End += Step;
                        std::string Text; for (size_t c = Start; c < End && Text.size() < 240; c += Step) Text += (char)Data[c];
                        if ((Text.size() > 7 && Text.compare(Text.size() - 7, 7, ".uasset") == 0) || (Text.find("/Content/") != std::string::npos && Text.back() == '/')) Files.insert(Text);
                        i = End;
                    }
                }
                if (Want < Buffer.size()) break;
            }
            At = Next;
        }
        // Known item ids (Fortnite wiki): Explosive Batarang WID_Athena_BadgerBangsNew, Grapnel Gun WID_Badger_Grape_VR.
        std::set<std::string> Dirs = { "Athena/Items/Weapons/", "Athena/Items/Weapons/Prototype/Badger_Bangs/", "Athena/Items/Weapons/Prototype/Badger_Grape/", "Athena/Items/Weapons/Prototype/Badger_Grapple/", "Athena/Items/Weapons/Prototype/" };
        std::set<std::string> Names = { "WID_Athena_BadgerBangsNew", "WID_Badger_Grape_VR" };
        std::set<std::string> Full; std::string Log;
        auto IsItem = [](const std::string& Name) { return Name.rfind("WID", 0) == 0 || Name.rfind("AGID", 0) == 0 || Name.find("ItemDef") != std::string::npos; };
        for (auto& File : Files)
        {
            Log += File + "\n";
            auto Content = File.find("/Content/");
            if (File.back() == '/') { if (Content != std::string::npos) Dirs.insert(File.substr(Content + 9)); continue; }
            auto Base = File.substr(0, File.size() - 7); auto Name = Base.substr(Base.find_last_of('/') + 1);
            if (!IsItem(Name)) continue;
            if (Content != std::string::npos) Full.insert(Base.substr(Content + 9)); else Names.insert(Name);
        }
        for (auto& Dir : Dirs) for (auto& Name : Names) Full.insert(Dir + Name);
        std::vector<std::wstring> Paths;
        for (auto& Rel : Full) { auto Name = Rel.substr(Rel.find_last_of('/') + 1); auto Path = "/Game/" + Rel + "." + Name; Paths.emplace_back(Path.begin(), Path.end()); }
        if (!Folder.empty()) { FILE* Out = nullptr; if (_wfopen_s(&Out, (Folder + L"batman-assets.txt").c_str(), L"w") == 0 && Out) { fputs(Log.c_str(), Out); fclose(Out); } }
        SyndicateBatmanPaths = Paths; SyndicateBatmanScanned = true;
        printf("[Syndicate] Batman items: %d Badger files seen, %d item definitions to try\n", (int)Files.size(), (int)Paths.size());
    }).detach();
}
// Loaded on the game thread the first time loot is rolled after the scan finished.
static std::vector<UObject*>& SyndicateBatmanItems()
{
    static std::vector<UObject*> Items; static bool Loaded = false;
    if (!Loaded && SyndicateBatmanScanned)
    {
        Loaded = true;
        for (auto& Path : SyndicateBatmanPaths)
            if (auto Item = (UObject*)StaticLoadObject(Path.c_str(), UFortWorldItemDefinition::StaticClass()))
                if (std::find(Items.begin(), Items.end(), Item) == Items.end())
                { Items.push_back(Item); printf("[Syndicate] Batman item in Season X loot: %s\n", Item->Name.ToString().c_str()); }
        if (Items.empty()) printf("[Syndicate] Batman items: none could be loaded (see batman-assets.txt)\n");
    }
    return Items;
}
// The Grapnel Gun among them (bots carry and use it in Season X).
UObject* SyndicateBatmanGrapnel()
{
    if (VersionInfo.FortniteVersion < 10.30 || VersionInfo.FortniteVersion >= 12) return nullptr; // Season X and Chapter 2 Season 1
    for (auto Item : SyndicateBatmanItems()) if (std::string(Item->Name.ToString().c_str()).find("Grap") != std::string::npos) return Item;
    return nullptr;
}

// ---- Chapter 2 Season 1 loot pool (Fortnite 11.00+) --------------------------------------------------------------
// 11.00's loot tables still list the vaulted Chapter 1 weapons, with weight 0. Floor loot and chests skip those, but
// the bots took their guns from every row, so they carried (and dropped) weapons Chapter 2 Season 1 never had.
// The pool is every item that the season's loot can actually roll: tier groups used by chests, floor loot, supply
// drops, llamas and ammo boxes, following only rows and package calls with a weight above 0.
static std::unordered_set<const UObject*> SyndicateChapter2Pool;
static void SyndicateBuildChapter2Pool()
{
    // Soft item references only resolve once their items are loaded, so the walk is repeated every 10 seconds
    // until three walks in a row add nothing.
    static ULONGLONG NextWalk = 0; static int Stable = 0;
    if (VersionInfo.FortniteVersion < 11 || LootPackageMap.empty() || TierDataMap.empty() || Stable >= 3 || GetTickCount64() < NextWalk) return;
    NextWalk = GetTickCount64() + 10000;
    const size_t Before = SyndicateChapter2Pool.size();
    std::unordered_set<int32> Visited; std::string Groups;
    std::function<void(FName)> Visit = [&](FName Package) {
        if (!Visited.insert(Package.ComparisonIndex).second) return;
        auto Found = LootPackageMap.find(Package.ComparisonIndex); if (Found == LootPackageMap.end()) return;
        for (auto Row : Found->second)
        {
            if (!Row || Row->Weight <= 0) continue;
            if (Row->LootPackageCall.Num() > 1) { Visit(UKismetStringLibrary::Conv_StringToName(Row->LootPackageCall)); continue; }
            if (auto Item = Row->ItemDefinition.Get()) SyndicateChapter2Pool.insert(Item);
        }
    };
    auto Used = [](const std::string& G) {
        return G.rfind("Loot_AthenaTreasure", 0) == 0 || G.rfind("Loot_AthenaFloorLoot", 0) == 0 || G.rfind("Loot_AthenaSupplyDrop", 0) == 0
            || G.rfind("Loot_AthenaLlama", 0) == 0 || G.rfind("Loot_AthenaAmmo", 0) == 0;
    };
    for (int Pass = 0; Pass < 2 && SyndicateChapter2Pool.size() < 5; ++Pass) // second pass: every Loot_Athena group, if the names differ
        for (auto& [_, Tiers] : TierDataMap)
            for (auto Tier : Tiers)
            {
                if (!Tier || Tier->Weight <= 0 || Tier->NumLootPackageDrops <= 0) continue;
                auto Group = std::string(Tier->TierGroup.ToString().c_str());
                if (Pass == 0 ? !Used(Group) : Group.rfind("Loot_Athena", 0) != 0) continue;
                if (Groups.find(Group + " ") == std::string::npos) Groups += Group + " ";
                Visit(Tier->LootPackage);
            }
    if (SyndicateChapter2Pool.size() == Before) { ++Stable; return; }
    Stable = 0;
    // The list goes next to the other server logs, so the season's pool can be checked after a match.
    std::string Weapons; int Count = 0;
    for (auto Item : SyndicateChapter2Pool) if (Item && ((UObject*)Item)->IsA(UFortWeaponItemDefinition::StaticClass())) { Weapons += std::string(Item->Name.ToString().c_str()) + "\n"; ++Count; }
    wchar_t Ready[32768]{};
    if (GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", Ready, 32768))
    {
        std::wstring Path = Ready; auto Slash = Path.find_last_of(L"\\/"); Path = (Slash == std::wstring::npos ? L"" : Path.substr(0, Slash + 1)) + L"loot-pool-chapter2.txt";
        FILE* Out = nullptr; if (_wfopen_s(&Out, Path.c_str(), L"w") == 0 && Out) { fprintf(Out, "tier groups: %s\nitems: %d, weapons: %d\n%s", Groups.c_str(), (int)SyndicateChapter2Pool.size(), Count, Weapons.c_str()); fclose(Out); }
    }
    printf("[Syndicate] Chapter 2 loot pool: %d items, %d weapons\n", (int)SyndicateChapter2Pool.size(), Count);
}
static bool SyndicateClassicArPumpOn();
static bool SyndicateIsClassicArPump(const std::string& Name)
{
    return Name.rfind("WID_Assault_Auto_Athena_", 0) == 0 || Name.rfind("WID_Assault_AutoHigh_Athena_", 0) == 0
        || (Name.rfind("WID_Shotgun_Standard_Athena_", 0) == 0 && Name.find("_Ore_") != std::string::npos);
}
// Syndicate: weapons taken out of a season on purpose (not in chests, floor loot, supply drops or bot loadouts).
// Season X: the Heavy Shotgun (WID_Shotgun_SlugFire_Athena_*) is vaulted.
bool SyndicateVaultedThisSeason(const UObject* Item)
{
    if (!Item) return false;
    if (VersionInfo.FortniteVersion >= 10 && VersionInfo.FortniteVersion < 11)
        return std::string(Item->Name.ToString().c_str()).find("SlugFire") != std::string::npos;
    return false;
}
bool SyndicateInChapter2LootPool(const UObject* Item)
{
    if (VersionInfo.FortniteVersion < 11 || !Item) return true;
    // With the classic switch on, the Chapter 1 AR and pump are part of the season (they replace the Chapter 2 ones).
    if (SyndicateClassicArPumpOn() && SyndicateIsClassicArPump(std::string(Item->Name.ToString().c_str()))) return true;
    SyndicateBuildChapter2Pool();
    return SyndicateChapter2Pool.empty() || SyndicateChapter2Pool.count(Item) > 0;
}

// ---- Classic AR and pump (Settings > Modded, Chapter 2 only) ----------------------------------------------------
// classic-ar-pump.txt in %LOCALAPPDATA%\ProjectTilted (written by the launcher) swaps the Chapter 2 Assault Rifle
// and Pump Shotgun for the Chapter 1 ones at the same rarity, wherever loot is rolled (floor, chests, supply drops,
// llamas) and in the bots' weapons. Chapter 1 AR: WID_Assault_Auto_Athena_* (grey to blue) and the SCAR
// WID_Assault_AutoHigh_Athena_* (purple, gold); Chapter 1 pump: WID_Shotgun_Standard_Athena_*_Ore_*. The Chapter 2
// ones are the AR/pump rows of this season's pool that are not those (burst ARs, WID_Assault_SemiAuto_*, stay).
static bool SyndicateClassicArPumpOn()
{
    static ULONGLONG Next = 0; static bool On = false;
    if (GetTickCount64() >= Next)
    {
        Next = GetTickCount64() + 5000;
        wchar_t Base[MAX_PATH]{};
        On = GetEnvironmentVariableW(L"LOCALAPPDATA", Base, MAX_PATH) && GetFileAttributesW((std::wstring(Base) + L"\\ProjectTilted\\classic-ar-pump.txt").c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    return On;
}
static int SyndicateItemRarity(const UObject* Item)
{
    auto Off = ((UObject*)Item)->GetOffset("Rarity");
    return Off == uint32(-1) ? -1 : (int)GetFromOffset<uint8>((UObject*)Item, Off);
}
UObject* SyndicateClassicArPumpSwap(UObject* Item)
{
    if (!Item || VersionInfo.FortniteVersion < 11 || !SyndicateClassicArPumpOn()) return Item;
    auto Name = std::string(Item->Name.ToString().c_str());
    if (SyndicateIsClassicArPump(Name)) return Item;
    bool Ar = Name.rfind("WID_Assault_", 0) == 0;
    for (auto Other : { "SemiAuto", "Burst", "Heavy", "LMG", "Surgical", "Suppressed", "Thermal", "PistolCaliber", "Minigun", "Scope", "Sniper" })
        if (Name.find(Other) != std::string::npos) Ar = false; // burst AR and other rifles stay as they are
    Ar = Ar && SyndicateInChapter2LootPool(Item);
    const bool Pump = Name.rfind("WID_Shotgun_Standard_", 0) == 0 && SyndicateInChapter2LootPool(Item);
    if (!Ar && !Pump) return Item;
    // Chapter 1 versions by rarity, found once among every weapon the loot tables name (any weight).
    static std::map<int, UObject*> ClassicArs, ClassicPumps; static bool Scanned = false;
    if (!Scanned)
    {
        Scanned = true;
        for (auto& [_, Rows] : LootPackageMap) for (auto Row : Rows)
        {
            auto Def = Row ? Row->ItemDefinition.Get() : nullptr; if (!Def || !((UObject*)Def)->IsA(UFortWeaponItemDefinition::StaticClass())) continue;
            auto N = std::string(Def->Name.ToString().c_str()); int R = SyndicateItemRarity(Def); if (R < 0) continue;
            if (N.rfind("WID_Assault_Auto_Athena_", 0) == 0 || N.rfind("WID_Assault_AutoHigh_Athena_", 0) == 0) { if (!ClassicArs.count(R) || N.find("Auto_Athena") != std::string::npos) ClassicArs[R] = (UObject*)Def; }
            else if (N.rfind("WID_Shotgun_Standard_Athena_", 0) == 0 && N.find("_Ore_") != std::string::npos) ClassicPumps[R] = (UObject*)Def;
        }
        printf("[Syndicate] Classic AR/pump: %d Chapter 1 ARs, %d Chapter 1 pumps found\n", (int)ClassicArs.size(), (int)ClassicPumps.size());
    }
    auto& Pool = Ar ? ClassicArs : ClassicPumps; if (Pool.empty()) return Item;
    int Want = SyndicateItemRarity(Item); if (Want < 0) Want = 0;
    UObject* Best = nullptr; int BestGap = 99;
    for (auto& [R, Def] : Pool) { int Gap = std::abs(R - Want) * 2 + (R > Want ? 1 : 0); if (Gap < BestGap) { BestGap = Gap; Best = Def; } } // nearest rarity, lower first
    return Best ? Best : Item;
}

// Syndicate PARADOX (Season XI) Save the World loot: when Runtime\Server\stw-loot.on exists, weapon drops reroll
// into this build's Save the World weapons. The pool is resolved once from a curated name list, so only the STW
// weapons the build actually ships are used. Toggled by the file's presence - no rebuild needed to turn on/off.
static bool SyndicateStwLootOn()
{
	static int Cached = -1;
	if (Cached >= 0) return Cached != 0;
	Cached = 0;
	// PARADOX is Season XI, which runs on the Season X (10.x) build. Restrict STW loot to that build so the
	// manual stw-loot.on toggle can never bleed into other seasons (e.g. Chapter 2 Season 2 / 12.50, which must
	// keep its own regular weapons and no traps).
	if (VersionInfo.FortniteVersion < 10 || VersionInfo.FortniteVersion >= 11) return false;
	wchar_t Ready[32768]{};
	if (GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", Ready, 32768))
		if (auto Slash = wcsrchr(Ready, L'\\'))
		{
			wcscpy_s(Slash + 1, 32768 - (size_t)(Slash + 1 - Ready), L"stw-loot.on");
			if (GetFileAttributesW(Ready) != INVALID_FILE_ATTRIBUTES) Cached = 1;
		}
	return Cached != 0;
}
// STW weapons bucketed by class, so a BR weapon is swapped for an STW weapon of the SAME class. That keeps the
// ammo type matching (the ammo that drops is still right) and keeps launchers/explosives as BR (no STW rocket
// weirdness / infinite ammo). Only snipers/shotguns/assault/pistol/SMG are rerolled; everything else is left.
struct SyndicateStwBuckets { std::vector<const UFortWeaponItemDefinition*> Sniper, Shotgun, Assault, Pistol, SMG; };
static UObject* SyndicateParadoxStwWeapon(UObject* Original)
{
	if (!SyndicateStwLootOn() || !Original) return nullptr;
	static SyndicateStwBuckets B;
	static std::vector<const UFortItemDefinition*> Traps;
	static bool Built = false;
	if (!Built)
	{
		Built = true;
		auto add = [](std::vector<const UFortWeaponItemDefinition*>& v, std::initializer_list<const char*> names) {
			for (auto n : names) if (auto w = TUObjectArray::FindObject<UFortWeaponItemDefinition>(n)) v.push_back(w);
		};
		add(B.Sniper, { "WID_Sniper_Standard_Winter_SR_Ore_T05", "WID_Sniper_VacuumTube_SR_Ore_T05", "WID_Sniper_Steampunk_SR_Ore_T05", "WID_Sniper_TripleShot_SR_Ore_T05", "WID_Sniper_VacuumTube_VR_Ore_T05" });
		add(B.Shotgun, { "WID_Shotgun_Winter_VR_Ore_T05", "WID_Shotgun_Standard_VR_Ore_T05", "WID_Shotgun_Standard_Winter_VR_Ore_T05", "WID_Shotgun_SemiAuto_VR_Ore_T05" });
		add(B.Assault, { "WID_Assault_AutoHigh_VR_Ore_T05", "WID_Assault_Standard_VR_Ore_T05", "WID_Assault_Surgical_VR_Ore_T05", "WID_Assault_Auto_VR_Ore_T05" });
		add(B.Pistol, { "WID_Pistol_SemiAuto_VR_Ore_T05", "WID_Pistol_AutoHeavy_VR_Ore_T05", "WID_Pistol_Standard_VR_Ore_T05" });
		add(B.SMG, { "WID_Assault_SubMachineGun_VR_Ore_T05", "WID_Pistol_AutoHeavy_SMG_VR_Ore_T05" });
		for (auto n : { "TID_Rail_Turret", "TID_Wall_Shield_WIP", "TID_Wall_HeavyMetal", "TID_Wall_Wood_Spikes_VR_T05", "TID_Wall_Speaker_VR_T05", "TID_Ceiling_ZapperTrap_VR_T05" })
			if (auto t = TUObjectArray::FindObject<UFortItemDefinition>(n)) Traps.push_back(t);
		printf("[SyndicateLoot] PARADOX STW buckets: snipers=%d shotguns=%d assault=%d pistol=%d smg=%d traps=%d\n", (int)B.Sniper.size(), (int)B.Shotgun.size(), (int)B.Assault.size(), (int)B.Pistol.size(), (int)B.SMG.size(), (int)Traps.size());
	}
	// ~12% of weapon drops become a Save the World trap instead (turrets, shields, spikes).
	if (!Traps.empty() && rand() % 100 < 12) return (UObject*)Traps[rand() % Traps.size()];
	std::string n = Original->Name.ToString().c_str();
	auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
	const std::vector<const UFortWeaponItemDefinition*>* pick = nullptr;
	if (has("Sniper")) pick = &B.Sniper;
	else if (has("Shotgun")) pick = &B.Shotgun;
	else if (has("SMG") || has("SubMachine")) pick = &B.SMG;
	else if (has("Pistol") || has("Revolver")) pick = &B.Pistol;
	else if (has("Assault") || has("Rifle") || has("Burst") || has("Scoped") || has("Auto")) pick = &B.Assault;
	// Launchers, explosives, consumables and anything unmatched: keep the BR item (prevents STW explosive / infinite-ammo bugs).
	if (!pick || pick->empty()) return nullptr;
	return (UObject*)(*pick)[rand() % pick->size()];
}

void UFortLootPackage::SetupLDSForPackage(TArray<FFortItemEntry*>& LootDrops, SDK::FName Package, int i, FName TierGroup, int WorldLevel, ABuildingContainer* Container)
{
	TArray<FFortLootPackageData*> LPGroups{};

	for (auto const& Val : LootPackageMap[Package.ComparisonIndex])
	{
		if (!Val)
			continue;

		if (i != -1 && Val->LootPackageCategory != i)
			continue;

		if (WorldLevel >= 0)
		{
			if (Val->MaxWorldLevel >= 0 && WorldLevel > Val->MaxWorldLevel)
				continue;
			
			if (Val->MinWorldLevel >= 0 && WorldLevel < Val->MinWorldLevel)
				continue;
		}

        // Remove LMG leaf rows and dedicated LMG packages before weighted selection.
        if (VersionInfo.FortniteVersion >= 4 && VersionInfo.FortniteVersion < 5) {
            auto Item = Val->ItemDefinition.Get();
            if ((Item && Item->Name.ToString().find("LMG") != std::string::npos) ||
                Val->LootPackageCall.ToString().find("LMG") != std::string::npos) continue;
        }
        // Season X: vaulted weapons (Heavy Shotgun) never roll.
        if (SyndicateVaultedThisSeason(Val->ItemDefinition.Get()) || (VersionInfo.FortniteVersion >= 10 && VersionInfo.FortniteVersion < 11 && Val->LootPackageCall.ToString().find("SlugFire") != std::string::npos)) continue;
		LPGroups.Add(Val);
	}

	if (LPGroups.Num() == 0)
		return;

	auto LootPackage = PickWeighted(LPGroups, [](float Total)
		{ return ((float)rand() / 32767.f) * Total; });
	if (!LootPackage)
		return;

	if (LootPackage->LootPackageCall.Num() > 1)
	{
		for (int i = 0; i < LootPackage->Count; i++)
			SetupLDSForPackage(LootDrops, UKismetStringLibrary::Conv_StringToName(LootPackage->LootPackageCall), 0, TierGroup, WorldLevel);

		return;
	}


	auto ItemDefinition = LootPackage->ItemDefinition.Get();

	if (!ItemDefinition)
		return;

	// Syndicate: Chapter 2 with the classic AR/pump switch on rolls the Chapter 1 versions instead.
	ItemDefinition = (decltype(ItemDefinition))SyndicateClassicArPumpSwap((UObject*)ItemDefinition);

	// Syndicate PARADOX (Season XI): when STW loot is on, reroll weapon drops into Save the World weapons.
	if (ItemDefinition->IsA(UFortWeaponItemDefinition::StaticClass()))
		if (auto Stw = SyndicateParadoxStwWeapon((UObject*)ItemDefinition))
			ItemDefinition = (decltype(ItemDefinition))Stw;

	// Syndicate: Seasons 6-X also roll the Guided Missile into weapon drops (about 1 in 30 weapons).
	if (VersionInfo.FortniteVersion >= 6 && VersionInfo.FortniteVersion < 11 && ItemDefinition->IsA(UFortWeaponItemDefinition::StaticClass()) && rand() % 30 == 0)
	{
		static auto Guided = []() -> UObject* {
			for (auto Path : { L"/Game/Athena/Items/Weapons/WID_RC_Rocket_Athena_SR_T03.WID_RC_Rocket_Athena_SR_T03", L"/Game/Athena/Items/Weapons/WID_RC_Rocket_Athena_VR_T03.WID_RC_Rocket_Athena_VR_T03" })
				if (auto Found = (UObject*)StaticLoadObject(Path, UFortItemDefinition::StaticClass()))
					return Found;
			printf("[Syndicate] Guided Missile is not in this build; loot unchanged\n");
			return nullptr;
		}();
		if (Guided)
			ItemDefinition = (decltype(ItemDefinition))Guided;
	}

	// Syndicate: Season X also rolls the Batman items: the Grapnel Gun in about 1 in 9 weapon drops, the Explosive
	// Batarang in about 1 in 40. Chapter 2 Season 1 gets the Grapnel Gun only, when its game files still have it.
	if (VersionInfo.FortniteVersion >= 10.30 && VersionInfo.FortniteVersion < 12 && ItemDefinition->IsA(UFortWeaponItemDefinition::StaticClass()))
	{
		auto& Batman = SyndicateBatmanItems();
		const int Roll = rand() % 360;
		bool WantGrapnel = Roll < 40, WantBatarang = Roll >= 40 && Roll < 49 && VersionInfo.FortniteVersion < 11;
		for (auto Item : Batman)
		{
			bool Grapnel = std::string(Item->Name.ToString().c_str()).find("Grap") != std::string::npos;
			if ((Grapnel && WantGrapnel) || (!Grapnel && WantBatarang)) { ItemDefinition = (decltype(ItemDefinition))Item; break; }
		}
	}

	FFortItemEntry* AmmoEntry = nullptr;

	// Normally Chapter 2+ attaches each weapon's own ammo to its drop. On Season X (10.31) ammo comes from the
	// loot tables for BR ammo types, so swapped-in STW weapons get none. With PARADOX STW loot on, run the
	// attach on 10.31 too, so each STW weapon drops with its own correct ammo.
	if ((VersionInfo.FortniteVersion >= 11 || SyndicateStwLootOn()) && VersionInfo.FortniteVersion != 19.40)
	{
		if (auto WorldItemDefinition = ItemDefinition->Cast<UFortWorldItemDefinition>())
		{
			auto AmmoDef = WorldItemDefinition->GetAmmoWorldItemDefinition_BP();

			if (auto AmmoDefinition = AmmoDef ? AmmoDef->Cast<UFortAmmoItemDefinition>() : nullptr)
			{
				auto SpawnAmmoData = FindObject<UFortWeaponPickupSpawnAmmoData>(L"/Game/Athena/Balance/Pickups/FortWeaponPickupSpawnAmmoData.FortWeaponPickupSpawnAmmoData");
				if (SpawnAmmoData) // null during 10.31 world-init; skip the ammo attach rather than crash the match
				{

				FGameplayTagContainer AmmoTags{};

				auto Interface = (IGameplayTagAssetInterface*)AmmoDefinition->GetInterface(IGameplayTagAssetInterface::StaticClass());
				if (Interface)
				{
					auto GetOwnedGameplayTags = (void(*)(IGameplayTagAssetInterface*, FGameplayTagContainer*))Interface->Vft[0x2];
					GetOwnedGameplayTags(Interface, &AmmoTags);
					//Interface->GetOwnedGameplayTags(&TargetTags);
				}

				float AmmoCount = 0.f;
				for (int i = 0; i < SpawnAmmoData->WeaponPickupAmmoCountArray.Num(); i++)
				{
					auto& AmmoCountData = SpawnAmmoData->WeaponPickupAmmoCountArray.Get(i, FWeaponPickupAmmoCountData::Size());

					if (AmmoTags.HasTag(AmmoCountData.AmmoItemDefinitionTag))
					{
						AmmoCount = AmmoCountData.SpawnCount.Evaluate((float)WorldLevel);
						break;
					}
				}

				auto Multiplier = SpawnAmmoData->DefaultWeaponAmmoMultiplier.Evaluate((float)WorldLevel);

				FGameplayTagContainer WeaponTags{};

				auto Interface2 = (IGameplayTagAssetInterface*)ItemDefinition->GetInterface(IGameplayTagAssetInterface::StaticClass());
				if (Interface2)
				{
					auto GetOwnedGameplayTags = (void(*)(IGameplayTagAssetInterface*, FGameplayTagContainer*))Interface2->Vft[0x2];
					GetOwnedGameplayTags(Interface2, &WeaponTags);
					//Interface->GetOwnedGameplayTags(&TargetTags);
				}

				for (int i = 0; i < SpawnAmmoData->WeaponPickupAmmoMultiplierOverrideArray.Num(); i++)
				{
					auto& MultiplierData = SpawnAmmoData->WeaponPickupAmmoMultiplierOverrideArray.Get(i, FHelperGameplayTagToAmmoCountMultiplier::Size());

					if (WeaponTags.HasTag(MultiplierData.Tag))
					{
						Multiplier = MultiplierData.CountMultiplier.Evaluate((float)WorldLevel);
						break;
					}
				}

				float	SourceMultiplier = 1.f;

				if (Container)
				{
					FGameplayTagContainer SourceTags{};

					auto Interface3 = (IGameplayTagAssetInterface*)Container->GetInterface(IGameplayTagAssetInterface::StaticClass());
					if (Interface3)
					{
						auto GetOwnedGameplayTags = (void(*)(IGameplayTagAssetInterface*, FGameplayTagContainer*))Interface3->Vft[0x2];
						GetOwnedGameplayTags(Interface3, &WeaponTags);
						//Interface->GetOwnedGameplayTags(&TargetTags);
					}

					for (int i = 0; i < SpawnAmmoData->SourceToAmmoMultiplierOverrideArray.Num(); i++)
					{
						auto& MultiplierData = SpawnAmmoData->SourceToAmmoMultiplierOverrideArray.Get(i, FHelperGameplayTagToAmmoCountMultiplier::Size());

						if (SourceTags.HasTag(MultiplierData.Tag))
						{
							SourceMultiplier = MultiplierData.CountMultiplier.Evaluate((float)WorldLevel);
							break;
						}
					}
					SourceTags.GameplayTags.Free();
					SourceTags.ParentTags.Free();
				}

				auto FinalCount = AmmoCount * Multiplier * SourceMultiplier;

				if (FinalCount > 0.f)
					AmmoEntry = AFortInventory::MakeItemEntry(AmmoDefinition, (int)FinalCount, AmmoDefinition->IsA(UFortWorldItemDefinition::StaticClass()) ? std::clamp(GetLevel(AmmoDefinition->LootLevelData), AmmoDefinition->MinLevel, AmmoDefinition->MaxLevel > 0 ? AmmoDefinition->MaxLevel : 99999) : 0);

				AmmoTags.GameplayTags.Free();
				AmmoTags.ParentTags.Free();
				WeaponTags.GameplayTags.Free();
				WeaponTags.ParentTags.Free();
				}
				else // 10.31 has no SpawnAmmoData: give the swapped STW weapon a solid starting ammo count directly
				{
					AmmoEntry = AFortInventory::MakeItemEntry(AmmoDefinition, 200, AmmoDefinition->IsA(UFortWorldItemDefinition::StaticClass()) ? std::clamp(GetLevel(AmmoDefinition->LootLevelData), AmmoDefinition->MinLevel, AmmoDefinition->MaxLevel > 0 ? AmmoDefinition->MaxLevel : 99999) : 0);
				}
			}
		}
	}

	bool found = false;
	bool foundAmmo = false;
	for (auto& LootDrop : LootDrops)
	{
		if (/*(!AmmoDef || AmmoDef->DropCount) && */LootDrop->ItemDefinition == ItemDefinition && LootDrop->Count < ItemDefinition->GetMaxStackSize())
		{
			LootDrop->Count += LootPackage->Count;

			if (LootDrop->Count > ItemDefinition->GetMaxStackSize())
			{
				auto OGCount = LootDrop->Count;
				LootDrop->Count = ItemDefinition->GetMaxStackSize();

				//if (Inventory::GetQuickbar(LootDrop.ItemDefinition) == EFortQuickBars::Secondary)
				LootDrops.Add(AFortInventory::MakeItemEntry(ItemDefinition, OGCount - (int32)ItemDefinition->GetMaxStackSize(), ItemDefinition->IsA(UFortWorldItemDefinition::StaticClass()) ? std::clamp(GetLevel(ItemDefinition->LootLevelData), ItemDefinition->MinLevel, ItemDefinition->MaxLevel > 0 ? ItemDefinition->MaxLevel : 99999) : 0));
			}

			//if (Inventory::GetQuickbar(LootDrop.ItemDefinition) == EFortQuickBars::Secondary)
			found = true;
			break;
		}

		if (AmmoEntry && AmmoEntry->ItemDefinition && LootDrop->ItemDefinition == AmmoEntry->ItemDefinition)
		{
			LootDrop->Count += AmmoEntry->Count;

			if (LootDrop->Count > AmmoEntry->ItemDefinition->GetMaxStackSize())
	        {
				auto OGCount = LootDrop->Count;
				LootDrop->Count = AmmoEntry->ItemDefinition->GetMaxStackSize();

				//if (!AFortInventory::IsPrimaryQuickbar(LootDrop->ItemDefinition))
				LootDrops.Add(AFortInventory::MakeItemEntry(AmmoEntry->ItemDefinition, OGCount - AmmoEntry->ItemDefinition->GetMaxStackSize(), AmmoEntry->ItemDefinition->IsA(UFortWorldItemDefinition::StaticClass()) ? std::clamp(GetLevel(AmmoEntry->ItemDefinition->LootLevelData), AmmoEntry->ItemDefinition->MinLevel, AmmoEntry->ItemDefinition->MaxLevel > 0 ? AmmoEntry->ItemDefinition->MaxLevel : 99999) : 0));
			}

			//if (Inventory::GetQuickbar(LootDrop.ItemDefinition) == EFortQuickBars::Secondary)
			free(AmmoEntry);
			foundAmmo = true;
		}
	}

	if (!found && LootPackage->Count > 0)
		LootDrops.Add(AFortInventory::MakeItemEntry(ItemDefinition, LootPackage->Count, ItemDefinition->IsA(UFortWorldItemDefinition::StaticClass()) ? std::clamp(GetLevel(ItemDefinition->LootLevelData), ItemDefinition->MinLevel, ItemDefinition->MaxLevel > 0 ? ItemDefinition->MaxLevel : 99999) : 0));

	if (!foundAmmo && AmmoEntry && LootPackage->Count > 0)
		LootDrops.Add(AmmoEntry);
	LPGroups.Free();
}

void UFortLootPackage::ChooseLootForContainer(TArray<FFortItemEntry*>& LootDrops, FName TierGroup, int LootTier, int WorldLevel, ABuildingContainer* Container)
{
	TArray<FFortLootTierData*> TierDataGroups{};
	
	for (auto const& Val : TierDataMap[TierGroup.ComparisonIndex])
		if (LootTier == -1 ? true : LootTier == Val->LootTier)
			TierDataGroups.Add(Val);

	auto LootTierData = PickWeighted(TierDataGroups, [](float Total)
		{ return ((float)rand() / 32767.f) * Total; });

	if (!LootTierData)
		return;
	
	//printf("Picked LootTierData %s\n", LootTierData->LootPackage.ToString().c_str());

	if (LootTierData->NumLootPackageDrops <= 0)
		return;

	//printf("Selecting %f loot drops from <unk>\n", LootTierData->NumLootPackageDrops);
	if (VersionInfo.FortniteVersion == 19.40)
	{
		auto& MinArr = LootTierData->LootPackageCategoryMinArray;

		if (MinArr.Num() > 1 && MinArr[1] == 0 && LootTierData->LootPackage.ToString().starts_with("WorldPKG.AthenaLoot.Weapon."))
		{
			MinArr[1] = 1;
			LootTierData->NumLootPackageDrops++;
		}
	}

	int DropCount;
	if (LootTierData->NumLootPackageDrops < 1.f)
		DropCount = 1;
	else
	{
		DropCount = (int)((LootTierData->NumLootPackageDrops * 2.f) - .5f) >> 1;

		float RemainderSomething = LootTierData->NumLootPackageDrops - (float)DropCount;

		if (RemainderSomething > 0.0000099999997f)
			DropCount += RemainderSomething >= ((float)rand() / 32767.f);
	}

	float AmountOfLootDrops = 0;
	float MinLootDrops = 0;

	std::unordered_map<int, int> NumMap;

	for (int i = 0; i < LootTierData->LootPackageCategoryMinArray.Num(); i++)
	{
		NumMap[i] = LootTierData->LootPackageCategoryMinArray[i];
		AmountOfLootDrops += LootTierData->LootPackageCategoryMinArray[i];
	}

	int SumWeights = 0;
	std::unordered_map<int, int> WeightMap;

	for (int i = 0; i < LootTierData->LootPackageCategoryWeightArray.Num(); i++)
	{
		if (LootTierData->LootPackageCategoryWeightArray[i] > 0 && (LootTierData->LootPackageCategoryMaxArray[i] < 0 || NumMap[i] < LootTierData->LootPackageCategoryMaxArray[i]))
		{
			WeightMap[i] = LootTierData->LootPackageCategoryWeightArray[i];
			SumWeights += LootTierData->LootPackageCategoryWeightArray[i];
		}
	}
	
	if (AmountOfLootDrops < DropCount)
		while (SumWeights > 0)
		{
			auto RandomValue = (float)rand() / 32767.f;
			auto RandomWeight = (int)std::floor(RandomValue * SumWeights);

			int Category = -1;
			for (auto& [DropCategory, Weight] : WeightMap)
			{
				if (RandomWeight <= Weight && RandomWeight <= LootTierData->NumLootPackageDrops)
				{
					Category = DropCategory;
					break;
				}

				RandomWeight -= Weight;
			}

			if (Category != -1)
			{
				AmountOfLootDrops++;
				NumMap[Category]++;

				if (LootTierData->LootPackageCategoryMaxArray[Category] >= 0 && NumMap[Category] >= LootTierData->LootPackageCategoryMaxArray[Category])
				{
					SumWeights -= LootTierData->LootPackageCategoryWeightArray[Category];
					WeightMap.erase(Category);
				}

				if (AmountOfLootDrops >= DropCount)
					break;
			}
		}

	/*if (AmountOfLootDrops < DropCount)
		while (SumWeights > 0)
		{
			AmountOfLootDrops++;

			if (AmountOfLootDrops >= DropCount)
			{
				//AmountOfLootDrops = AmountOfLootDrops;
				break;
			}

			SumWeights--;
		}

	if (!AmountOfLootDrops)
		return {};*/

	LootDrops.Reserve((int)DropCount);


	int SpawnedItems = 0;
	int CurrentCategory = 0;
	while (SpawnedItems < DropCount && CurrentCategory < NumMap.size())
	{
		for (int j = 0; j < NumMap[CurrentCategory]; j++)
			SetupLDSForPackage(LootDrops, LootTierData->LootPackage, CurrentCategory, TierGroup, WorldLevel);

		SpawnedItems += NumMap[CurrentCategory];
		CurrentCategory++;
	}
	TierDataGroups.Free();
}


// Loot picked by blueprints through UFortKismetLibrary::PickLootDrops (supply drops,
// llamas): apply the playlist's tier-group redirect like containers do, and fall back to
// the standard supply drop / chest tables when this build has no rows for the tier.
void SyndicatePickLootDrops(TArray<FFortItemEntry*>& LootDrops, FName TierGroup, int ForcedLootTier, int WorldLevel)
{
	auto RealTierGroup = TierGroup;
	auto GameMode = ((AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode);
	if (GameMode && GameMode->HasRedirectAthenaLootTierGroups() && VersionInfo.FortniteVersion < 20)
	{
		static auto Off = GameMode->GetOffset("RedirectAthenaLootTierGroups");
		auto& Redirects = *(TMap<FName, FName>*)(__int64(GameMode) + Off);
		for (const auto& [Old, New] : Redirects)
			if (Old == TierGroup) { RealTierGroup = New; break; }
	}
	UFortLootPackage::ChooseLootForContainer(LootDrops, RealTierGroup, ForcedLootTier, WorldLevel);
	std::string Tier = RealTierGroup.ToString().c_str();
	if (LootDrops.Num() == 0)
	{
		static auto SupplyDrop = FName(L"Loot_AthenaSupplyDrop");
		static auto Treasure = FName(L"Loot_AthenaTreasure");
		bool Supply = Tier.find("Supply") != std::string::npos || Tier.find("Llama") != std::string::npos;
		if (Supply && !(RealTierGroup == SupplyDrop)) UFortLootPackage::ChooseLootForContainer(LootDrops, SupplyDrop, -1, WorldLevel);
		if (LootDrops.Num() == 0 && Supply) UFortLootPackage::ChooseLootForContainer(LootDrops, Treasure, -1, WorldLevel);
	}
	static std::unordered_set<std::string> Reported;
	if (Reported.insert(Tier + (LootDrops.Num() ? "|ok" : "|empty")).second)
		printf("[SyndicateLoot] PickLootDrops tier=%s (asked %s) items=%d\n", Tier.c_str(), TierGroup.ToString().c_str(), LootDrops.Num());
}

bool UFortLootPackage::SpawnLootHook(ABuildingContainer* Container)
{
	if (Container->bAlreadySearched)
		return false;

	auto RealTierGroup = Container->SearchLootTierGroup;
	auto GameMode = ((AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode);
	if (GameMode->HasRedirectAthenaLootTierGroups())
	{
		static auto RedirectAthenaLootTierGroupsOff = GameMode->GetOffset("RedirectAthenaLootTierGroups");

		if (VersionInfo.FortniteVersion >= 20)
		{
			auto& RedirectAthenaLootTierGroups = *(TMap<int32, int32>*)(__int64(GameMode) + RedirectAthenaLootTierGroupsOff);

			for (const auto& [OldTierGroup, RedirectedTierGroup] : RedirectAthenaLootTierGroups)
			{
				if (OldTierGroup == Container->SearchLootTierGroup.ComparisonIndex)
				{
					RealTierGroup.ComparisonIndex = RedirectedTierGroup;
					break;
				}
			}
		}
		else
		{
			auto& RedirectAthenaLootTierGroups = *(TMap<FName, FName>*)(__int64(GameMode) + RedirectAthenaLootTierGroupsOff);

			for (const auto& [OldTierGroup, RedirectedTierGroup] : RedirectAthenaLootTierGroups)
			{
				if (OldTierGroup == Container->SearchLootTierGroup)
				{
					RealTierGroup = RedirectedTierGroup;
					break;
				}
			}
		}
	}
	else
	{
		static auto Loot_Treasure = FName(L"Loot_Treasure");
		static auto Loot_Ammo = FName(L"Loot_Ammo");
		static auto Loot_AthenaTreasure = FName(L"Loot_AthenaTreasure");
		static auto Loot_AthenaAmmoLarge = FName(L"Loot_AthenaAmmoLarge");

		if (Container->SearchLootTierGroup == Loot_Treasure)
			RealTierGroup = Loot_AthenaTreasure;
		else if (Container->SearchLootTierGroup == Loot_Ammo)
			RealTierGroup = Loot_AthenaAmmoLarge;
	}

	TArray<FFortItemEntry*> LootDrops{};

	UFortLootPackage::ChooseLootForContainer(LootDrops, RealTierGroup, -1, GameMode->GameState->WorldLevel, Container);
	// Some themed containers (Season X Tilted Town, event chests) use tier groups
	// with no loot rows in this build. Fall back to the standard chest/ammo tables
	// instead of opening empty.
	if (LootDrops.Num() == 0)
	{
		static std::unordered_set<std::string> Reported;
		std::string Tier = RealTierGroup.ToString().c_str();
		std::string Kind = Container->Class->Name.ToString().c_str();
		bool Ammo = Tier.find("Ammo") != std::string::npos || Kind.find("Ammo") != std::string::npos;
		bool Chest = !Ammo && (Tier.find("Treasure") != std::string::npos || Tier.find("Chest") != std::string::npos || Kind.find("Chest") != std::string::npos || Kind.find("Treasure") != std::string::npos);
		// Floor loot: some builds (notably the 12.50 Dev Kit) ship no rows for Loot_AthenaFloorLoot, so
		// the floor spawners (Tiered_Athena_FloorLoot_*_C) came up empty and the map had nothing on the
		// ground. Fall back to the standard chest table so the floor at least has weapons/consumables
		// rather than nothing; only ever runs when the floor tier is genuinely empty.
		bool Floor = !Ammo && !Chest && (Tier.find("FloorLoot") != std::string::npos || Kind.find("FloorLoot") != std::string::npos);
		if (Reported.insert(Tier + "|" + Kind).second)
			printf("[SyndicateLoot] no loot rows for tier=%s container=%s; %s\n", Tier.c_str(), Kind.c_str(), Ammo ? "using ammo box loot" : Chest ? "using chest loot" : Floor ? "using chest loot (floor fallback)" : "left empty");
		static auto FallbackChest = FName(L"Loot_AthenaTreasure");
		static auto FallbackAmmo = FName(L"Loot_AthenaAmmoLarge");
		if (Ammo || Chest || Floor)
			UFortLootPackage::ChooseLootForContainer(LootDrops, Ammo ? FallbackAmmo : FallbackChest, -1, GameMode->GameState->WorldLevel, Container);
	}

	for (auto& LootDrop : LootDrops)
	{
		AFortInventory::SpawnPickup(Container, *LootDrop);
		free(LootDrop);
	}

	Container->bAlreadySearched = true;
	Container->OnRep_bAlreadySearched();
	Container->SearchBounceData.SearchAnimationCount++;
	Container->BounceContainer();
	//if (Container->bDestroyContainerOnSearch)
	//	Container->K2_DestroyActor();

	return true;
}


void UFortLootPackage::SpawnLoot(FName& TierGroup, FVector Loc)
{
	auto& RealTierGroup = TierGroup;

	auto GameMode = ((AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode);
	if (GameMode->HasRedirectAthenaLootTierGroups())
	{
		for (const auto& [OldTierGroup, RedirectedTierGroup] : GameMode->RedirectAthenaLootTierGroups)
		{
			if (OldTierGroup == TierGroup)
			{
				RealTierGroup = RedirectedTierGroup;
				break;
			}
		}
	}
	else
	{
		static auto Loot_Treasure = FName(L"Loot_Treasure");
		static auto Loot_Ammo = FName(L"Loot_Ammo");
		static auto Loot_AthenaTreasure = FName(L"Loot_AthenaTreasure");
		static auto Loot_AthenaAmmoLarge = FName(L"Loot_AthenaAmmoLarge");

		if (TierGroup == Loot_Treasure)
			RealTierGroup = Loot_AthenaTreasure;
		else if (TierGroup == Loot_Ammo)
			RealTierGroup = Loot_AthenaAmmoLarge;
	}

	TArray<FFortItemEntry*> LootDrops{};

	UFortLootPackage::ChooseLootForContainer(LootDrops, RealTierGroup);

	for (auto& LootDrop : LootDrops)
	{
		AFortInventory::SpawnPickup(Loc, *LootDrop);
		free(LootDrop);
	}
}


bool ServerOnAttemptInteract(ABuildingContainer* BuildingContainer, AFortPlayerPawnAthena*)
{

	if (!BuildingContainer)
		return false;

	if (BuildingContainer->bAlreadySearched)
		return true;

	//SpawnLoot(BuildingContainer->SearchLootTierGroup, BuildingContainer->K2_GetActorLocation() + BuildingContainer->GetActorRightVector() * 70.f + FVector{ 0, 0, 50 });
	UFortLootPackage::SpawnLootHook(BuildingContainer);

	/*BuildingContainer->bAlreadySearched = true;
	BuildingContainer->OnRep_bAlreadySearched();
	BuildingContainer->SearchBounceData.SearchAnimationCount++;
	BuildingContainer->BounceContainer();*/

	return true;
}

void UFortLootPackage::SpawnFloorLootForContainer(const UClass* ContainerType)
{
	TArray<ABuildingContainer*> Containers;
	Utils::GetAll<ABuildingContainer>(ContainerType, Containers);

	for (auto& BuildingContainer : Containers)
	{
		if (VersionInfo.FortniteVersion >= 11.00)
		{
			BuildingContainer->K2_DestroyActor();
		}
		//SpawnLootHook(BuildingContainer);
		else
		{
			SpawnLootHook(BuildingContainer);
			//SpawnLoot(BuildingContainer->SearchLootTierGroup, BuildingContainer->K2_GetActorLocation() + BuildingContainer->GetActorForwardVector() * BuildingContainer->LootSpawnLocation_Athena.X + BuildingContainer->GetActorRightVector() * BuildingContainer->LootSpawnLocation_Athena.Y + BuildingContainer->GetActorUpVector() * BuildingContainer->LootSpawnLocation_Athena.Z);

			if (VersionInfo.FortniteVersion > 3.3)
				BuildingContainer->K2_DestroyActor();
			else
			{
				BuildingContainer->bAlreadySearched = true;
				BuildingContainer->OnRep_bAlreadySearched();
			}
		}
	}

	Containers.Free();
}

void UFortLootPackage::SpawnConsumableActor(ABGAConsumableSpawner* Spawner)
{
    // Season 4 world consumables (hop rocks around the meteor craters). This was 4.00 only, so 4.5, the build
    // the launcher runs for Season 4, never got hop rocks. Seasons 6-X also need it: Dusty Divot mushrooms, apples
    // and other world consumables.
    if(((VersionInfo.FortniteVersion<4 || VersionInfo.FortniteVersion>=5) && (VersionInfo.FortniteVersion<6 || VersionInfo.FortniteVersion>=11)) || !Spawner || !Spawner->HasSpawnLootTierGroup())return;
    // Season 6 shadow stones (Spooky Mist) are disabled for now: the shadow form does not work yet.
    {std::string Tier=Spawner->SpawnLootTierGroup.ToString().c_str();if(Tier.find("SpookyMist")!=std::string::npos)return;}
    TArray<FFortItemEntry*> Drops{};
    ChooseLootForContainer(Drops,Spawner->SpawnLootTierGroup);
    int Spawned=0;std::string Kinds;
    for(auto Entry:Drops) {
        auto Item=Entry?(UBGAConsumableWrapperItemDefinition*)Entry->ItemDefinition:nullptr;
        if(Spawned<8 && Item && Item->HasConsumableClass())if(auto Class=Item->ConsumableClass.Get()) {
            auto Location=Spawner->K2_GetActorLocation();
            float Angle=Spawned*2.399963f;float Spread=Spawned?300.f+90.f*Spawned:0.f;
            Location.X+=cos(Angle)*Spread;Location.Y+=sin(Angle)*Spread;
            // Resolve the ground using this build's reflected trace layout.
            // The legacy ground helper returns the airborne spawner position in 4.00.
            auto Library=UKismetSystemLibrary::GetDefaultObj();
            auto Trace=Library?Library->GetFunction("SphereTraceSingle"):nullptr;
            if(Trace) {
                auto Params=Trace->GetParamsNamed();std::vector<uint8> Data(Params.Size,0);
                auto Top=Location,Bottom=Location;Top.Z+=1000;Bottom.Z-=2500;
                TArray<AActor*> Ignore;Ignore.Add(Spawner);
                auto Put=[&](const char* Name,const auto& Value) {
                    size_t Size=sizeof(Value);
                    if constexpr(std::is_same_v<std::decay_t<decltype(Value)>,FVector>)Size=FVector::Size();
                    for(auto& P:Params.NameOffsetMap)if(P.Name==Name && P.ElementSize==Size && P.Offset+Size<=Data.size()){memcpy(Data.data()+P.Offset,&Value,Size);return true;}return false;
                };
                if(Put("WorldContextObject",UWorld::GetWorld()) && Put("Start",Top) && Put("End",Bottom)
                    && Put("Radius",2.f) && Put("TraceChannel",uint8(0)) && Put("ActorsToIgnore",Ignore)) {
                    Put("bIgnoreSelf",true);Library->ProcessEvent(Trace,Data.data());
                    bool Hit=false;for(auto& P:Params.NameOffsetMap)if(P.Name=="ReturnValue")Hit=Data[P.Offset]!=0;
                    auto HS=FindStruct("HitResult");auto Offset=HS?HS->GetOffset("ImpactPoint"):uint32(-1);
                    if(Hit && Offset!=uint32(-1))for(auto& P:Params.NameOffsetMap)if(P.Name=="OutHit" && Offset+FVector::Size()<=P.ElementSize){memcpy(&Location,Data.data()+P.Offset+Offset,FVector::Size());Location.Z+=35;}
                }
                Ignore.Free();
            }
            if(auto Actor=UWorld::SpawnActor(Class,FTransform(Location,Spawner->K2_GetActorRotation()))) {
                if(auto Rep=Actor->GetFunction("SetReplicates"))Actor->Call<void>(Rep,true);
                Actor->bReplicates=true;Actor->bAlwaysRelevant=true;
                  // Dynamic consumables must enter the active replication list before clients join.
                  Actor->SetNetDormancy(1);
                  Actor->FlushNetDormancy();
                if(auto Show=Actor->GetFunction("SetActorHiddenInGame"))Actor->Call<void>(Show,false);
                auto Off=Actor->GetOffset("StaticMesh");auto Mesh=Off!=uint32(-1)?GetFromOffset<UObject*>(Actor,Off):nullptr;
                if(Mesh) {
                    if(auto Cull=Mesh->GetFunction("SetCullDistance"))Mesh->Call<void>(Cull,0.f);
                    if(auto Visible=Mesh->GetFunction("SetVisibility"))Mesh->Call<void>(Visible,true,true);
                }
                Actor->ForceNetUpdate();++Spawned;if(Kinds.find(Class->Name.ToString().c_str())==std::string::npos)Kinds+=std::string(" ")+Class->Name.ToString().c_str();
            }
        }
        if(Entry)free(Entry);
    }
    wchar_t Ready[32768]{};
    if(GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE",Ready,32768)) {
        FILE* File=nullptr;auto Path=std::wstring(Ready)+L".consumables.log";
        if(!_wfopen_s(&File,Path.c_str(),L"a") && File){fprintf(File,"spawner=%s tier=%s drops=%d spawned=%d classes=%s\n",Spawner->Name.ToString().c_str(),Spawner->SpawnLootTierGroup.ToString().c_str(),Drops.Num(),Spawned,Kinds.c_str());fclose(File);}
    }
    Drops.Free();
}

void (*OnAuthorityRandomUpgradeAppliedOG)(ABuildingContainer*, FName&);
void OnAuthorityRandomUpgradeApplied(ABuildingContainer* Container, FName& UpgradeTierGroup)
{
	if (!Container->HasChosenRandomUpgrade()) // 15.10 what
		return OnAuthorityRandomUpgradeAppliedOG(Container, UpgradeTierGroup);

	auto ChosenRandomUpgrade = Container->ChosenRandomUpgrade;

	if (Container->HasAlternateMeshes())
	{
		if (ChosenRandomUpgrade < 0 || ChosenRandomUpgrade >= Container->AlternateMeshes.Num())
			return OnAuthorityRandomUpgradeAppliedOG(Container, UpgradeTierGroup);

		auto& AlternateMeshSet = Container->AlternateMeshes[ChosenRandomUpgrade];

		Container->ReplicatedLootTier = AlternateMeshSet.Tier;
		Container->OnRep_LootTier();
	}
	else
	{
		auto ClassData = Container->GetClassData();
		if (ChosenRandomUpgrade < 0 || ChosenRandomUpgrade >= ClassData->AlternateMeshes.Num())
			return OnAuthorityRandomUpgradeAppliedOG(Container, UpgradeTierGroup);

		auto& AlternateMeshSet = ClassData->AlternateMeshes[ChosenRandomUpgrade];

		Container->ReplicatedLootTier = AlternateMeshSet.Tier;
		Container->OnRep_LootTier();
	}

	return OnAuthorityRandomUpgradeAppliedOG(Container, UpgradeTierGroup);
}

void PostUpdate(ABuildingSMActor* BuildingSMActor)
{
	static auto Chest = FindObject<UClass>(L"/Game/Building/ActorBlueprints/Containers/Tiered_Chest_6_Parent.Tiered_Chest_6_Parent_C");
	static auto AmmoCrate = FindObject<UClass>(L"/Game/Building/ActorBlueprints/Containers/Tiered_Short_Ammo_3_Parent.Tiered_Short_Ammo_3_Parent_C");

	if (auto Container = BuildingSMActor->Cast<ABuildingContainer>())
	{
		if (Container->IsA(Chest) || Container->IsA(AmmoCrate))
			return;

		if (!Container->bStartAlreadySearched_Athena)
		{
			Container->bAlreadySearched = true;
			Container->OnRep_bAlreadySearched();
		}
	}
}


bool bDidntFind = false;
void UFortLootPackage::Hook()
{
	if (VersionInfo.FortniteVersion < 3)
	{
		auto PostUpdate_ = Memcury::Scanner::FindStringRef(L"ABuildingSMActor::PostUpdate() Building: %s, AltMeshIdx: %d", false, 0, VersionInfo.FortniteVersion >= 19).ScanFor({ 0x40, 0x53 }, false).Get();

		Utils::Hook(PostUpdate_, PostUpdate);
	}

	if (VersionInfo.FortniteVersion >= 11.00)
	{
		Utils::Hook(FindSpawnLoot(), SpawnLootHook);

		auto OnAuthorityRandomUpgradeAppliedAddr = FindFunctionCall(L"OnAuthorityRandomUpgradeApplied", std::vector<uint8_t>{ 0x48, 0x89, 0x5C });
		Utils::Hook(OnAuthorityRandomUpgradeAppliedAddr, OnAuthorityRandomUpgradeApplied, OnAuthorityRandomUpgradeAppliedOG);
		return;
	}
	else
	{
		auto ServerOnAttemptInteractRef = Memcury::Scanner::FindStringRef(L"ABuildingContainer::ServerOnAttemptInteract %s failed for %s");

		if (ServerOnAttemptInteractRef.Get())
		{
			auto UnderlyingCall = ServerOnAttemptInteractRef;
			UnderlyingCall.ScanFor({ 0x41, 0xFF }, false);

			if (UnderlyingCall.Get() != ServerOnAttemptInteractRef.Get())
			{
				auto VFTIndex = *(uint32*)(UnderlyingCall.Get() + 3);

				if (VFTIndex != 0)
				{
					Utils::Hook<ABuildingContainer>(VFTIndex / 8, ServerOnAttemptInteract);

					return;
				}
			}
		}
	}

	// if we cant find serveronattemptinteract
	bDidntFind = true;
	return;
}
