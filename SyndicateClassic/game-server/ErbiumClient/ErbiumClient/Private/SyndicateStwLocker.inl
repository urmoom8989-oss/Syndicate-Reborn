    // ---- Save the World heroes in the locker (Pre-season 1.7.2 to Chapter 2 Season 1 11.00) --------------------------------
    // Included inside SyndicateClient.cpp's anonymous namespace (it uses Log and runs on the game thread from OnMessage).
    //
    // The backend lists each STW hero as a locker outfit while Unlock all is on: profile items with template ids such as
    // AthenaCharacter:HID_Commando_008_FoundersF_SR_T05 (syndicateLegacy111Profile.js). Fortnite only shows a profile item
    // whose template id the asset manager knows, so for each hero this makes an outfit (an AthenaCharacterItemDefinition
    // copied from a real one, with the hero as its HeroDefinition and the hero's name, description, rarity and icons)
    // and registers it with UAssetManager::AddDynamicAsset under that id. The lobby preview then shows the hero, and
    // equipping it saves the pick; the match server puts the hero on the player (Erbium RestoreSyndicateOutfit).
    // Every object made here is kept in the root set: it lives for the whole game, and the copied fields share memory
    // with the template outfit and the hero, which are rooted too, so nothing is ever freed twice.
    struct StwAsset { std::wstring Path; std::string Name; };
    struct StwEntry { std::string Id; std::wstring HeroPath; UObject* Item = nullptr; bool Filled = false; };
    std::vector<StwEntry> StwItems;
    int StwStage = 0; // 0 waiting for the engine, 1 filling in heroes, 2 done, -1 off or failed
    ULONGLONG StwNextTry = 0;
    int StwTries = 0;
    int32 StwItemSize = 0; // property size of the outfits made here (UObject's own 0x28 bytes excluded when cleared)

    // 1.72, 1.8x, 1.11, 3.5 (Season 3, UE 4.20: AddDynamicAsset's offsets are read from its code, as on 1.11) and
    // Season 4 (4.5) to Chapter 2 Season 1 (11.00), found the same way. The backend only lists the heroes while the
    // launcher's "Save the World skins" switch is on; registering them costs nothing when it is off.
    // Season X 10.40 included: Syndicate's own match server puts the hero on the player (ServerAndConsole/PlayerLocker.h).
    bool StwBuild() { const float V = VersionInfo.FortniteVersion; return (V > 1.f && V < 2.f) || (V > 3.4f && V < 12.f); }
    bool StwWanted() { return StwStage == 0 || StwStage == 1; }
    std::string StwLower(std::string S) { for (auto& C : S) C = (char)tolower((unsigned char)C); return S; }
    bool StwHeroName(const std::string& Name) {
        auto L = StwLower(Name);
        return L.rfind("hid_", 0) == 0 && L.find("test") == std::string::npos && L.find("npc") == std::string::npos && L.find("_tbd") == std::string::npos && L.find("_dev_") == std::string::npos;
    }
    // Same choice as the backend (syndicateStwHeroes.js): one hero per look, the highest rarity of it.
    std::string StwLook(const std::string& Id) {
        auto L = StwLower(Id);
        for (auto R : { "_c_t0", "_uc_t0", "_r_t0", "_vr_t0", "_sr_t0" }) { auto At = L.rfind(R); if (At != std::string::npos && At + strlen(R) + 1 == L.size()) return L.substr(0, At); }
        return L;
    }
    int StwRank(const std::string& Id) {
        auto L = StwLower(Id); int Rank = -1, I = 0;
        for (auto R : { "_c_t0", "_uc_t0", "_r_t0", "_vr_t0", "_sr_t0" }) { auto At = L.rfind(R); if (At != std::string::npos && At + strlen(R) + 1 == L.size()) Rank = I; ++I; }
        return Rank;
    }
    // Assets of a class from the asset registry (the same query the match server uses for its exports).
    std::vector<StwAsset> StwAssetsOfClass(const wchar_t* ClassName) {
        std::vector<StwAsset> Out;
        auto Helpers = FindClass("AssetRegistryHelpers"); auto Interface = FindClass("AssetRegistry"); auto Data = FindStruct("AssetData");
        if (!Helpers || !Interface || !Data) return Out;
        auto Obj = Helpers->GetDefaultObj(); auto Get = Obj->GetFunction("GetAssetRegistry"); if (!Get) return Out;
        uint8 Return[32]{}; Obj->ProcessEvent(Get, Return); auto Registry = *(UObject**)Return; if (!Registry) return Out;
        UFunction* Query = nullptr;
        for (auto F = Interface->GetChildren(); F; F = F->GetNext()) if (F->Name.ToString() == "GetAssetsByClass") Query = (UFunction*)F;
        if (!Query) return Out;
        auto Meta = Query->GetParamsNamed(); std::vector<uint8> Params(Meta.Size + 16, 0); FName Name(ClassName); uint32 ArrayOffset = uint32(-1);
        for (auto& P : Meta.NameOffsetMap) {
            if (P.Name == "ClassName" && P.ElementSize == sizeof(FName) && P.Offset + sizeof(FName) <= Meta.Size) memcpy(Params.data() + P.Offset, &Name, sizeof(FName));
            else if (P.Name == "OutAssetData") ArrayOffset = P.Offset;
            else if (P.Name == "bSearchSubClasses" && P.Offset < Meta.Size) Params[P.Offset] = 1;
        }
        if (ArrayOffset == uint32(-1)) return Out;
        Registry->ProcessEvent(Query, Params.data());
        auto& Found = *(TArray<uint8>*)(Params.data() + ArrayOffset);
        auto Size = Data->GetPropertiesSize(); auto PathOffset = Data->GetOffset("ObjectPath"); auto NameOffset = Data->GetOffset("AssetName");
        if (PathOffset == uint32(-1) || Size <= 0 || Found.Num() < 0 || Found.Num() > 200000) return Out;
        for (int i = 0; i < Found.Num(); i++) {
            auto Element = &Found.Get(i, Size);
            auto Path = GetFromOffset<FName>(Element, PathOffset); if (Path.ComparisonIndex == 0) continue;
            StwAsset A; A.Path = std::wstring(Path.ToWString().c_str());
            A.Name = NameOffset != uint32(-1) ? std::string(GetFromOffset<FName>(Element, NameOffset).ToString().c_str()) : "";
            if (A.Name.empty()) { std::string P(A.Path.begin(), A.Path.end()); auto Dot = P.find_last_of('.'); A.Name = Dot == std::string::npos ? P : P.substr(Dot + 1); }
            Out.push_back(A);
        }
        return Out;
    }
    // Start of the function that contains an address, from the exe's unwind table (follows chained entries).
    void* StwFunctionStart(uint64_t At) {
        DWORD64 Base = 0; auto Entry = RtlLookupFunctionEntry(At, &Base, nullptr);
        for (int Hops = 0; Entry && Hops < 8; ++Hops) {
            auto Info = (const uint8_t*)(Base + Entry->UnwindData);
            if (!((Info[0] >> 3) & UNW_FLAG_CHAININFO)) break;
            const uint8_t Codes = Info[2];
            Entry = (PRUNTIME_FUNCTION)(Info + 4 + ((Codes + 1) & ~1) * 2);
        }
        return Entry ? (void*)(Base + Entry->BeginAddress) : nullptr;
    }
    // Copies a property's bytes from one object to another when both have it with the same size.
    bool StwCopyField(UObject* To, const UObject* From, const char* Name) {
        auto ToOff = To->GetOffset(Name); auto FromOff = From->GetOffset(Name);
        auto ToProp = To->GetProperty(Name); auto FromProp = From->GetProperty(Name);
        if (ToOff == uint32(-1) || FromOff == uint32(-1) || !ToProp || !FromProp) return false;
        auto Size = GetFromOffset<uint32>(ToProp, Offsets::ElementSize);
        if (!Size || Size != GetFromOffset<uint32>(FromProp, Offsets::ElementSize) || Size > 64) return false;
        memcpy((uint8*)To + ToOff, (const uint8*)From + FromOff, Size); return true;
    }

    // The 68 hero looks in the 1.8.2 game files (same list as the backend's local/Cosmetics/stw-heroes.json), for builds
    // whose asset registry query returns nothing.
    const wchar_t* const StwKnownHeroes[] = {
        L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_007_SR_T03.HID_Commando_007_SR_T03", L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_007HW_SR_T05.HID_Commando_007HW_SR_T05",
        L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_008_FoundersF_SR_T05.HID_Commando_008_FoundersF_SR_T05", L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_008_FoundersM_SR_T05.HID_Commando_008_FoundersM_SR_T05",
        L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_008_SR_T05.HID_Commando_008_SR_T05", L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_009_M_SR_T05.HID_Commando_009_M_SR_T05",
        L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_009_SR_T05.HID_Commando_009_SR_T05", L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_010_SR_T05.HID_Commando_010_SR_T05",
        L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_011_SR_T05.HID_Commando_011_SR_T05", L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_GCGrenade_SR_T03.HID_Commando_GCGrenade_SR_T03",
        L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_GrenadeGun_SR_T03.HID_Commando_GrenadeGun_SR_T03", L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_GrenadeMaster_SR_T05.HID_Commando_GrenadeMaster_SR_T05",
        L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_GunHeadshot_SR_T03.HID_Commando_GunHeadshot_SR_T03", L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_GunHeadshotHW_SR_T03.HID_Commando_GunHeadshotHW_SR_T03",
        L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_GunTough_SR_T03.HID_Commando_GunTough_SR_T03", L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_ShockDamage_SR_T05.HID_Commando_ShockDamage_SR_T05",
        L"/Game/Heroes/Commando/ItemDefinition/HID_Commando_Sony_R_T04.HID_Commando_Sony_R_T04", L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_007_SR_T05.HID_Constructor_007_SR_T05",
        L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_007HW_SR_T03.HID_Constructor_007HW_SR_T03", L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_008_FoundersF_SR_T05.HID_Constructor_008_FoundersF_SR_T05",
        L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_008_FoundersM_SR_T04.HID_Constructor_008_FoundersM_SR_T04", L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_008_SR_T03.HID_Constructor_008_SR_T03",
        L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_009_SR_T03.HID_Constructor_009_SR_T03", L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_010_SR_T03.HID_Constructor_010_SR_T03",
        L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_011_SR_T03.HID_Constructor_011_SR_T03", L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_BASEBig_SR_T03.HID_Constructor_BASEBig_SR_T03",
        L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_BaseHyper_SR_T05.HID_Constructor_BaseHyper_SR_T05", L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_BaseHyperHW_SR_T05.HID_Constructor_BaseHyperHW_SR_T05",
        L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_HammerPlasma_SR_T05.HID_Constructor_HammerPlasma_SR_T05", L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_HammerTank_SR_T05.HID_Constructor_HammerTank_SR_T05",
        L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_PlasmaDamage_SR_T03.HID_Constructor_PlasmaDamage_SR_T03", L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_RushBASE_F_SR_T03.HID_Constructor_RushBASE_F_SR_T03",
        L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_RushBASE_SR_T04.HID_Constructor_RushBASE_SR_T04", L"/Game/Heroes/Constructor/ItemDefinition/HID_Constructor_Sony_R_T02.HID_Constructor_Sony_R_T02",
        L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_007_SR_T05.HID_Ninja_007_SR_T05", L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_008_SR_T03.HID_Ninja_008_SR_T03",
        L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_009_SR_T03.HID_Ninja_009_SR_T03", L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_010_F_SR_T03.HID_Ninja_010_F_SR_T03",
        L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_010_SR_T03.HID_Ninja_010_SR_T03", L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_011_SR_T04.HID_Ninja_011_SR_T04",
        L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_SlashBreath_SR_T03.HID_Ninja_SlashBreath_SR_T03", L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_SlashBreathHW_SR_T05.HID_Ninja_SlashBreathHW_SR_T05",
        L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_SlashTail_SR_T03.HID_Ninja_SlashTail_SR_T03", L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_SmokeDimMak_SR_T05.HID_Ninja_SmokeDimMak_SR_T05",
        L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_Sony_R_T03.HID_Ninja_Sony_R_T03", L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_StarsAssassin_FoundersF_SR_T04.HID_Ninja_StarsAssassin_FoundersF_SR_T04",
        L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_StarsAssassin_FoundersM_SR_T05.HID_Ninja_StarsAssassin_FoundersM_SR_T05", L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_StarsAssassin_SR_T05.HID_Ninja_StarsAssassin_SR_T05",
        L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_StarsRain_SR_T03.HID_Ninja_StarsRain_SR_T03", L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_StarsRainHW_SR_T05.HID_Ninja_StarsRainHW_SR_T05",
        L"/Game/Heroes/Ninja/ItemDefinition/HID_Ninja_Swordmaster_SR_T05.HID_Ninja_Swordmaster_SR_T05", L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_007_SR_T05.HID_Outlander_007_SR_T05",
        L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_008_FoundersF_SR_T05.HID_Outlander_008_FoundersF_SR_T05", L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_008_FoundersM_SR_T04.HID_Outlander_008_FoundersM_SR_T04",
        L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_008_SR_T03.HID_Outlander_008_SR_T03", L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_009_SR_T03.HID_Outlander_009_SR_T03",
        L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_010_M_SR_T03.HID_Outlander_010_M_SR_T03", L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_010_SR_T03.HID_Outlander_010_SR_T03",
        L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_011_SR_T05.HID_Outlander_011_SR_T05", L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_PunchDamage_SR_T03.HID_Outlander_PunchDamage_SR_T03",
        L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_PunchPhase_SR_T04.HID_Outlander_PunchPhase_SR_T04", L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_Sony_R_T04.HID_Outlander_Sony_R_T04",
        L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_SphereFragment_SR_T05.HID_Outlander_SphereFragment_SR_T05", L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_ZoneFragment_SR_T05.HID_Outlander_ZoneFragment_SR_T05",
        L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_ZoneHarvest_SR_T03.HID_Outlander_ZoneHarvest_SR_T03", L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_ZoneHarvestHW_SR_T04.HID_Outlander_ZoneHarvestHW_SR_T04",
        L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_ZonePistol_SR_T04.HID_Outlander_ZonePistol_SR_T04", L"/Game/Heroes/Outlander/ItemDefinition/HID_Outlander_ZonePistolHW_SR_T03.HID_Outlander_ZonePistolHW_SR_T03",
    };
    // Stage 0: make and register one outfit per hero (cheap, so the ids are known before the profile is read).
    void StwRegister() {
        if (++StwTries > 120) { StwStage = -1; Log("stw locker: the engine never became ready; heroes are not in the locker"); return; }
        auto Engine = UEngine::GetEngine(); if (!Engine) return;
        auto AmOff = Engine->GetOffset("AssetManager"); if (AmOff == uint32(-1)) { StwStage = -1; Log("stw locker: no AssetManager property on the engine"); return; }
        auto AssetManager = GetFromOffset<UObject*>(Engine, AmOff); if (!AssetManager) return; // not made yet: try again
        static void* AddDynamicAsset = nullptr;
        if (!AddDynamicAsset) {
            auto Ref = Memcury::Scanner::FindStringRef(L"AddDynamicAsset on %s called with conflicting path. Path %s is replacing path %s", false).Get();
            AddDynamicAsset = Ref ? StwFunctionStart(Ref) : nullptr;
            if (!AddDynamicAsset) { StwStage = -1; Log("stw locker: UAssetManager::AddDynamicAsset not found"); return; }
        }
        auto Package = FindObject(L"/Engine/Transient", FindClass("Package"));
        auto HeroClass = FindClass("FortHeroType");
        if (!Package || !HeroClass) { StwStage = -1; Log("stw locker: transient package or FortHeroType class missing"); return; }
        // A real outfit to copy every other field from (the default Commando when it is there).
        UObject* Template = nullptr;
        auto Outfits = StwAssetsOfClass(L"AthenaCharacterItemDefinition");
        for (auto& A : Outfits) if (StwLower(A.Name).find("cid_001_athena_commando_f_default") != std::string::npos) { Template = (UObject*)FindObject(A.Path.c_str(), FindClass("AthenaCharacterItemDefinition")); break; }
        for (size_t i = 0; !Template && i < Outfits.size(); ++i) if (StwLower(Outfits[i].Name).rfind("cid_0", 0) == 0) Template = (UObject*)FindObject(Outfits[i].Path.c_str(), FindClass("AthenaCharacterItemDefinition"));
        // 1.8.2's asset registry query returns nothing (its match server can't export cosmetics either): load a
        // default outfit by its path instead.
        for (int N = 1; !Template && N <= 8; ++N) {
            wchar_t Path[160]; swprintf_s(Path, L"/Game/Athena/Items/Cosmetics/Characters/CID_%03d_Athena_Commando_%s_Default.CID_%03d_Athena_Commando_%s_Default", N, N <= 4 ? L"F" : L"M", N, N <= 4 ? L"F" : L"M");
            Template = (UObject*)FindObject(Path, FindClass("AthenaCharacterItemDefinition"));
        }
        // Pre-season (1.7.2): neither the registry nor the default paths answer. Any outfit already loaded (the lobby
        // loads the player's own) has every field we need; only its hero and texts are replaced below.
        if (!Template) if (auto CharacterClass = FindClass("AthenaCharacterItemDefinition"))
            for (int i = 0; i < TUObjectArray::Num() && !Template; ++i) {
                auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
                if (O && O->Class == CharacterClass && !O->IsDefaultObject() && StwLower(O->Name.ToString().c_str()).rfind("cid_", 0) == 0) Template = O;
            }
        if (Template || StwTries >= 60) { char Line[160]; snprintf(Line, sizeof(Line), "stw locker: asset registry lists %d outfits; outfit to copy %s", int(Outfits.size()), Template ? "found" : "missing"); Log(Line); }
        if (!Template) { if (StwTries < 60) { StwNextTry = GetTickCount64() + 2000; return; } StwStage = -1; Log("stw locker: no outfit to copy"); return; }
        Template->AddToRoot();
        auto ItemClass = (UClass*)Template->Class; const int32 Size = ItemClass->GetPropertiesSize(); StwItemSize = Size;
        // The heroes: one per look, from the asset registry.
        std::unordered_map<std::string, StwAsset> Best;
        for (auto& A : StwAssetsOfClass(L"FortHeroType")) {
            std::string P(A.Path.begin(), A.Path.end());
            if (P.rfind("/Game/Heroes/", 0) != 0 || !StwHeroName(A.Name)) continue;
            auto Look = StwLook(A.Name); auto It = Best.find(Look); // one per look, the same pick the backend makes from this build's export
            if (It == Best.end() || StwRank(A.Name) > StwRank(It->second.Name) || (StwRank(A.Name) == StwRank(It->second.Name) && A.Name > It->second.Name)) Best[Look] = A;
        }
        // No registry answer: the hero looks found in the 1.8.2 game files (the backend's bundled list, stw-heroes.json).
        if (Best.empty()) for (auto Path : StwKnownHeroes) {
            std::wstring W = Path; auto Dot = W.find_last_of(L'.'); if (Dot == std::wstring::npos) continue;
            StwAsset A; A.Path = W; A.Name = std::string(W.begin() + Dot + 1, W.end()); Best[StwLook(A.Name)] = A;
        }
        { char Line[120]; snprintf(Line, sizeof(Line), "stw locker: %d hero looks to register", int(Best.size())); Log(Line); }
        if (Best.empty()) { StwStage = -1; Log("stw locker: this build lists no Save the World heroes"); return; }
        // AddDynamicAsset only adds to a type marked dynamic, and AthenaCharacter is a scanned type, so 1.8.2 refused
        // every hero. As 1.8.2's own code reads it: the asset manager's AssetTypeMap is a TMap at +0x28 (0x20-byte
        // elements, FName key at +0, the FPrimaryAssetTypeData pointer at +8) and the type data's "dynamic" flag is the
        // byte at +0x78. The type is marked dynamic while the heroes are added, then put back as it was.
        // 1.11 (UE 4.19) has the same map, but its soft class pointers are 8 bytes longer, which moves the flag: there
        // it is the first candidate after +0x78 holding 0 or 1 with the type's asset count (a few hundred) right after it.
        uint8* TypeData = nullptr; uint32 DynamicOff = 0x78;
        {
            auto Readable = [](const void* P, size_t Bytes) { MEMORY_BASIC_INFORMATION M{}; if (!P || !VirtualQuery(P, &M, sizeof(M)) || M.State != MEM_COMMIT || (M.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
                return (const uint8*)P + Bytes <= (const uint8*)M.BaseAddress + M.RegionSize; };
            FName TypeName(L"AthenaCharacter");
            // 1.8.2: the map's elements at +0x28, 0x20 bytes each. Other builds: the first array in the asset manager
            // (+0x28 to +0x200) whose elements (0x18-0x30 bytes) hold an AthenaCharacter key with a readable pointer after it.
            uint32 MapOff = 0, Stride = 0;
            // 1.11 (UE 4.19), read from AddDynamicAsset's own code (logged once from 1.11): "lea rsi,[rcx+disp32]" is the
            // type map (+0x370 there) and "mov byte ptr [rax+disp32],1" marks a type it creates as dynamic (+0x80 there).
            uint32 CodeMapOff = 0, CodeFlagOff = 0;
            // Other places the map can be read from: 7.40 (UE 4.23) addresses it through r15, "lea rcx,[r15+0x3c8]"
            // (49 8D 8F), so every "lea/mov/add reg,[reg+disp32]" in the first 0x100 bytes is a candidate; each one is
            // only used if it really holds an AthenaCharacter entry.
            std::vector<uint32> MapCandidates;
            if (VersionInfo.EngineVersion > 4.16 && Readable(AddDynamicAsset, 0x340)) {
                auto Code = (const uint8*)AddDynamicAsset;
                for (int i = 0; i + 7 <= 0x100 && !CodeMapOff; ++i) if (Code[i] == 0x48 && Code[i + 1] == 0x8D && Code[i + 2] == 0xB1) CodeMapOff = *(const uint32*)(Code + i + 3);
                for (int i = 0; i + 7 <= 0x100; ++i) {
                    const uint8 Rex = Code[i], Op = Code[i + 1], ModRm = Code[i + 2];
                    if ((Rex & 0xF0) != 0x40 || (Op != 0x8D && Op != 0x8B && Op != 0x03) || (ModRm & 0xC0) != 0x80 || (ModRm & 7) == 4) continue;
                    const uint32 Disp = *(const uint32*)(Code + i + 3);
                    if (Disp >= 0x28 && Disp <= 0x1000 && !(Disp & 7) && std::find(MapCandidates.begin(), MapCandidates.end(), Disp) == MapCandidates.end()) MapCandidates.push_back(Disp);
                }
                for (int i = 0; i + 7 <= 0x340 && !CodeFlagOff; ++i) if (Code[i] == 0xC6 && Code[i + 1] == 0x80 && Code[i + 6] == 0x01) CodeFlagOff = *(const uint32*)(Code + i + 2);
                if (CodeMapOff > 0x1000) CodeMapOff = 0;
                if (CodeFlagOff > 0x200) CodeFlagOff = 0;
                char L[120]; snprintf(L, sizeof(L), "stw locker: from AddDynamicAsset: type map +0x%x, dynamic flag +0x%x", CodeMapOff, CodeFlagOff); Log(L);
            }
            MapCandidates.insert(MapCandidates.begin(), CodeMapOff);
            for (uint32 Off : MapCandidates) {
                if (TypeData) break;
                if (!Off || !Readable((uint8*)AssetManager + Off, 16)) continue;
                auto Elements = *(uint8**)((uint8*)AssetManager + Off); const int32 Count = *(int32*)((uint8*)AssetManager + Off + 8);
                if (!Elements || Count <= 0 || Count > 4096 || !Readable(Elements, size_t(Count) * 0x20)) continue;
                for (int32 i = 0; i < Count && !TypeData; ++i) {
                    auto Element = Elements + size_t(i) * 0x20;
                    if (memcmp(Element, &TypeName, sizeof(FName)) != 0) continue;
                    auto Data = *(uint8**)(Element + 8); if (Readable(Data, 0xC0)) { TypeData = Data; MapOff = Off; Stride = 0x20; }
                }
            }
            for (uint32 Off = 0x28; Off < (VersionInfo.EngineVersion > 4.20 ? 0x600u : 0x200u) && !TypeData; Off += 8) {
                if (VersionInfo.EngineVersion <= 4.16 && Off != 0x28) break;
                if (!Readable((uint8*)AssetManager + Off, 16)) break;
                auto Elements = *(uint8**)((uint8*)AssetManager + Off); const int32 Count = *(int32*)((uint8*)AssetManager + Off + 8);
                if (!Elements || Count <= 0 || Count > 4096) continue;
                for (uint32 Step : { 0x20u, 0x18u, 0x28u, 0x30u }) {
                    if (VersionInfo.EngineVersion <= 4.16 && Step != 0x20) continue;
                    if (!Readable(Elements, size_t(Count) * Step)) continue;
                    for (int32 i = 0; i < Count && !TypeData; ++i) {
                        auto Element = Elements + size_t(i) * Step;
                        if (memcmp(Element, &TypeName, sizeof(FName)) != 0) continue;
                        auto Data = *(uint8**)(Element + 8); if (Readable(Data, 0xC0)) { TypeData = Data; MapOff = Off; Stride = Step; }
                    }
                    if (TypeData) break;
                }
            }
            if (TypeData && VersionInfo.EngineVersion > 4.16) {
                // What the type data looks like, for checking the flag's place in this build.
                std::string Hex; char B[4];
                for (int i = 0x60; i < 0xC0; ++i) { snprintf(B, sizeof(B), "%02x", TypeData[i]); Hex += B; if ((i & 7) == 7) Hex += ' '; }
                char Line[400]; snprintf(Line, sizeof(Line), "stw locker: type map at +0x%x (elements 0x%x bytes); type data +0x60..: %s", MapOff, Stride, Hex.c_str()); Log(Line);
                DynamicOff = CodeFlagOff;
                if (!DynamicOff) for (uint32 Off : { 0x80u, 0x88u, 0x78u, 0x90u }) {
                    const int32 Assets = *(int32*)(TypeData + Off + 4);
                    if (TypeData[Off] <= 1 && Assets > 0 && Assets < 20000) { DynamicOff = Off; break; }
                }
                if (!DynamicOff) TypeData = nullptr;
            }
            char Line[160]; snprintf(Line, sizeof(Line), TypeData ? "stw locker: found the AthenaCharacter asset type (dynamic flag at +0x%x)" : "stw locker: AthenaCharacter asset type not found in the asset manager", DynamicOff); Log(Line);
            // Not found: write the asset manager's first bytes and AddDynamicAsset's code to the log once, so the layout
            // of this build can be read from them.
            static bool Dumped = false;
            if (!TypeData && !Dumped) {
                Dumped = true;
                auto Dump = [&](const char* What, const uint8* From, size_t Bytes) {
                    if (!Readable(From, Bytes)) { char L[96]; snprintf(L, sizeof(L), "stw dump %s: not readable", What); Log(L); return; }
                    for (size_t At = 0; At < Bytes; At += 64) {
                        std::string Hex; char B[4];
                        for (size_t i = At; i < At + 64 && i < Bytes; ++i) { snprintf(B, sizeof(B), "%02x", From[i]); Hex += B; }
                        char L[220]; snprintf(L, sizeof(L), "stw dump %s +0x%03zx %s", What, At, Hex.c_str()); Log(L);
                    }
                };
                char L[120]; snprintf(L, sizeof(L), "stw dump: AthenaCharacter name %08x %08x, AddDynamicAsset at exe+0x%llx", ((uint32*)&TypeName)[0], ((uint32*)&TypeName)[1], (unsigned long long)((uint8*)AddDynamicAsset - (uint8*)GetModuleHandleW(nullptr))); Log(L);
                Dump("am", (const uint8*)AssetManager, 0x100);
                Dump("fn", (const uint8*)AddDynamicAsset, 0x340);
            }
        }
        const uint8 WasDynamic = TypeData ? TypeData[DynamicOff] : 0;
        if (TypeData) TypeData[DynamicOff] = 1;
        int Registered = 0, Refused = 0;
        for (auto& [Look, A] : Best) {
            auto Item = UGameplayStatics::SpawnObject(ItemClass, Package); if (!Item) continue;
            if (Size > 0x28) memcpy((uint8*)Item + 0x28, (const uint8*)Template + 0x28, size_t(Size) - 0x28); // every field after UObject's own
            Item->AddToRoot();
            std::wstring Path = L"/Engine/Transient." + std::wstring(Item->Name.ToWString().c_str());
            std::wstring WideId = L"SyndicateStw_" + std::wstring(A.Name.begin(), A.Name.end()); // the backend's stwTemplateName: "HID_..." alone resolves to the STW hero itself
            struct { FName Type; FName Name; } Id{ FName(L"AthenaCharacter"), FName(WideId.c_str()) };
            uint8 Bundle[32]{};
            bool Ok = false;
            if (VersionInfo.EngineVersion < 4.18) { FString Soft(Path.c_str()); Ok = ((bool(*)(UObject*, const void*, const void*, const void*))AddDynamicAsset)(AssetManager, &Id, &Soft, Bundle); }
            else { struct { FName AssetPathName; FString SubPathString; } Soft{ FName(Path.c_str()), FString() }; Ok = ((bool(*)(UObject*, const void*, const void*, const void*))AddDynamicAsset)(AssetManager, &Id, &Soft, Bundle); }
            if (!Ok) { if (++Refused == 1) { char L1[200]; snprintf(L1, sizeof(L1), "stw locker: the asset manager refused %s (AthenaCharacter type unknown?)", A.Name.c_str()); Log(L1); } continue; }
            StwItems.push_back({ A.Name, A.Path, Item, false }); ++Registered;
        }
        if (TypeData) TypeData[DynamicOff] = WasDynamic;
        char Line[200]; snprintf(Line, sizeof(Line), "stw locker: registered %d Save the World heroes as outfits (copied from %s)", Registered, Template->Name.ToString().c_str()); Log(Line);
        StwStage = Registered ? 1 : -1;
    }
    // Stage 1: load the heroes a few per frame and give each outfit its hero, name and icons.
    void StwFill() {
        auto HeroClass = FindClass("FortHeroType"); int Done = 0, Left = 0;
        for (auto& E : StwItems) {
            if (E.Filled) continue;
            if (Done >= 2) { ++Left; continue; }
            E.Filled = true; ++Done;
            auto Hero = (UObject*)FindObject(E.HeroPath.c_str(), HeroClass); if (!Hero) continue;
            Hero->AddToRoot();
            auto HeroOff = E.Item->GetOffset("HeroDefinition"); if (HeroOff != uint32(-1)) GetFromOffset<UObject*>(E.Item, HeroOff) = Hero;
            for (auto Field : { "DisplayName", "ShortDescription", "Description", "Rarity", "SmallPreviewImage", "LargePreviewImage" }) StwCopyField(E.Item, Hero, Field);
        }
        if (!Left) { StwStage = 2; Log("stw locker: every hero outfit is ready"); }
    }
    // The game is closing (its window is gone): empty the fields of the outfits made here before the engine destroys
    // every object. They share their arrays, strings and texts with the template outfit and the heroes, so the
    // engine freed that memory twice and Season 2 crashed on exit (write access violation after "Game engine shut down").
    void StwReleaseForExit() {
        static bool Done = false; if (Done || StwItems.empty() || StwItemSize <= 0x28) return; Done = true;
        for (auto& E : StwItems) if (E.Item) memset((uint8*)E.Item + 0x28, 0, size_t(StwItemSize) - 0x28);
        Log("stw locker: game closing, hero outfits emptied");
    }
    void StwReleaseForExitSafe() { __try { StwReleaseForExit(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    // A crash inside the tick (caught by SyndicateClient.cpp) used to switch the heroes off for the whole game. It
    // happens when the lobby is still loading (4.5 and 5.10 hit it on most starts), so try again a little later.
    // While filling (stage 1) the hero that failed is already marked done, so the next tick carries on with the rest.
    int StwFailures = 0;
    void StwLockerFailed() {
        if (++StwFailures > 20) { StwStage = -1; Log("stw locker: stopped after repeated errors"); return; }
        if (StwStage == 0 && !StwItems.empty()) StwStage = 1; // some heroes were registered before the error: keep them
        StwNextTry = GetTickCount64() + 3000;
        if (StwFailures == 1) Log("stw locker: error while the game was still loading; trying again");
    }
    void StwLockerTick() {
        if (!StwBuild()) { StwStage = -1; return; }
        if (StwStage == 0) { if (GetTickCount64() < StwNextTry) return; StwNextTry = GetTickCount64() + 500; StwRegister(); }
        else if (StwStage == 1) { if (GetTickCount64() < StwNextTry) return; StwFill(); }
    }
