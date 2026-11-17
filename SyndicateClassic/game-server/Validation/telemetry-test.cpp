// Standalone test for SyndicateTelemetry.h (the match event writer). No engine needed.
// cl /EHsc /std:c++17 telemetry-test.cpp ole32.lib /Fetelemetry-test.exe && telemetry-test.exe
#include "../Erbium/Erbium/Private/SpectraBots/SyndicateTelemetry.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <vector>
#include <filesystem>

static std::vector<std::string> ReadLines(const std::wstring& Path) {
    std::ifstream In(std::filesystem::path(Path), std::ios::binary); std::vector<std::string> Lines; std::string L;
    while (std::getline(In, L)) if (!L.empty()) Lines.push_back(L);
    return Lines;
}
static bool Has(const std::string& Line, const std::string& Part) { return Line.find(Part) != std::string::npos; }

int main() {
    auto Dir = std::filesystem::temp_directory_path() / L"syndicate-telemetry-test";
    std::filesystem::remove_all(Dir); std::filesystem::create_directories(Dir);
    auto Ready = (Dir / L"ready.pid").wstring();
    SetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", Ready.c_str());

    using namespace SyndicateTelemetry;
    // Nothing is written before a match starts.
    Emit("landed", 1.0, Fields().Str("poi", "x"));
    assert(!Active());

    assert(Begin(10.0, Fields().Str("fortniteVersion", "4.50").Int("bots", 99)));
    assert(Active());
    std::string MatchId = Current().Id;
    Emit("landed", 12.5, Fields().Obj("actor", Fields().Str("kind", "bot").Str("id", "b3")).Pos("pos", 1, 2, 3).Str("poi", "City_001"));
    Emit("bot_state", 13.0, Fields().Str("from", "looting").Str("to", "fighting").Num("enemyDist", 1234.5));
    // Names with quotes, backslashes and control characters stay valid JSON.
    Emit("note", 14.0, Fields().Str("text", "a\"b\\c\n\x01"));
    Emit("nan", 14.5, Fields().Num("bad", std::nan("")));
    assert(CheckpointDue(40.0) && !CheckpointDue(50.0) && CheckpointDue(71.0));
    End(90.0, Fields().Str("reason", "winner"));
    assert(!Active());
    Emit("late", 95.0); // ignored after the end

    // Pseudonyms: stable for a name, different for different names, never the name itself.
    auto A = HumanId("Edwin"), B = HumanId("Edwin"), C = HumanId("Syndicate");
    assert(A == B && A != C && A.find("Edwin") == std::string::npos && A[0] == 'h');

    Sleep(500); // the background writer flushes asynchronously
    auto Lines = ReadLines((Dir / L"telemetry" / std::filesystem::path(std::wstring(MatchId.begin(), MatchId.end()) + L".jsonl")).wstring());
    assert(Lines.size() == 6);
    for (size_t I = 0; I < Lines.size(); ++I) {
        assert(Has(Lines[I], "\"v\":1,"));
        assert(Has(Lines[I], "\"seq\":" + std::to_string(I + 1) + ","));
        assert(Has(Lines[I], "\"id\":\"" + MatchId + ":" + std::to_string(I + 1) + "\""));
    }
    assert(Has(Lines[0], "\"type\":\"match_start\"") && Has(Lines[0], "\"botVersion\":") && Has(Lines[0], "\"schema\":1"));
    assert(Has(Lines[1], "\"pos\":[1,2,3]") && Has(Lines[1], "\"actor\":{\"kind\":\"bot\",\"id\":\"b3\"}"));
    assert(Has(Lines[2], "\"enemyDist\":1234.500"));
    assert(Has(Lines[3], "\"text\":\"a\\\"b\\\\c\\n\\u0001\""));
    assert(Has(Lines[4], "\"bad\":null"));
    assert(Has(Lines[5], "\"type\":\"match_end\"") && Has(Lines[5], "\"reason\":\"winner\""));

    // A second match gets a new file and starts again at seq 1.
    assert(Begin(100.0, Fields()));
    auto Second = Current().Id; assert(Second != MatchId);
    End(101.0, Fields().Str("reason", "endgame"));
    Sleep(300);
    auto Lines2 = ReadLines((Dir / L"telemetry" / std::filesystem::path(std::wstring(Second.begin(), Second.end()) + L".jsonl")).wstring());
    assert(Lines2.size() == 2 && Has(Lines2[0], "\"seq\":1,"));

    printf("telemetry-test: all checks passed (%zu + %zu lines)\n", Lines.size(), Lines2.size());
    return 0;
}
