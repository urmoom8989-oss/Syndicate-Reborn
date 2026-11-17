#pragma once
// Match telemetry: one JSONL file per match, written by the game server (the authoritative source:
// it sees every landing, elimination and bot decision; the launcher can close without losing data).
//
// File:   <Runtime/Server>/telemetry/<matchId>.jsonl  (folder next to TILTED_SERVER_READY_FILE)
// Line:   {"v":1,"match":"...","seq":N,"id":"<match>:<seq>","t":<server seconds>,"ts":"<UTC ISO>","type":"...",...}
// Schema: docs in backend/src/Utils/telemetry/SCHEMA.md. Bump SchemaVersion when a field changes meaning.
//
// Reliability: lines are queued on the game thread and appended by a background thread (opening or
// growing files on the game thread stalled the server when antivirus scanned them). Each batch is
// flushed, so a server crash loses at most the last second. A match with no "match_end" line is
// recovered by the backend as "terminated_unexpectedly". "seq" increases by one per line, so gaps
// and duplicates are detectable; "id" is stable, so re-reading or re-uploading never double counts.
//
// Privacy: players are pseudonyms ("h" + hash of name with a per-install random salt kept beside
// the telemetry); names, account ids, IPs and chat are never written. Bots are "b<index>".
//
// This header is engine independent (pure C++/Win32) so it can be tested standalone
// (game-server/Validation/telemetry-test.cpp).
#include <string>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <cstdio>
#include <share.h>
#include <cstdint>
#include <cmath>
#include <atomic>
#include <windows.h>
#include <combaseapi.h>

namespace SyndicateTelemetry {
    inline constexpr int SchemaVersion = 1;
    // Bump when bot behaviour changes, so reports and benchmarks can compare versions.
    inline constexpr const char* BotVersion = "spectra-2026.10.27";
    inline constexpr size_t MaxQueuedLines = 20000;      // a stuck disk never grows memory without bound
    inline constexpr size_t MaxMatchBytes = 32u * 1024u * 1024u; // per match file

    inline std::string Escape(const std::string& Text) {
        std::string Out; Out.reserve(Text.size() + 8);
        for (unsigned char C : Text) {
            if (C == '"' || C == '\\') { Out += '\\'; Out += char(C); }
            else if (C == '\n') Out += "\\n";
            else if (C == '\r') Out += "\\r";
            else if (C == '\t') Out += "\\t";
            else if (C < 32) { char B[8]; snprintf(B, sizeof(B), "\\u%04x", C); Out += B; }
            else Out += char(C);
        }
        return Out;
    }

    // Small JSON object builder: Event("landed").Str("poi",x).Num("x",1)...
    class Fields {
        std::string Body;
        void Key(const char* Name) { if (!Body.empty()) Body += ','; Body += '"'; Body += Name; Body += "\":"; }
    public:
        Fields& Str(const char* Name, const std::string& Value) { Key(Name); Body += '"' + Escape(Value) + '"'; return *this; }
        Fields& Num(const char* Name, double Value) {
            Key(Name);
            if (!std::isfinite(Value)) { Body += "null"; return *this; }
            char B[40]; if (Value == std::floor(Value) && std::fabs(Value) < 1e15) snprintf(B, sizeof(B), "%.0f", Value); else snprintf(B, sizeof(B), "%.3f", Value);
            Body += B; return *this;
        }
        Fields& Int(const char* Name, long long Value) { Key(Name); Body += std::to_string(Value); return *this; }
        Fields& Bool(const char* Name, bool Value) { Key(Name); Body += Value ? "true" : "false"; return *this; }
        Fields& Null(const char* Name) { Key(Name); Body += "null"; return *this; }
        Fields& Pos(const char* Name, double X, double Y, double Z) {
            Key(Name); char B[96]; snprintf(B, sizeof(B), "[%.0f,%.0f,%.0f]", X, Y, Z); Body += B; return *this;
        }
        Fields& Obj(const char* Name, const Fields& Inner) { Key(Name); Body += '{' + Inner.Body + '}'; return *this; }
        const std::string& Text() const { return Body; }
        bool Empty() const { return Body.empty(); }
    };

    // FNV-1a 64: stable pseudonyms without storing names.
    inline uint64_t Hash(const std::string& Text) {
        uint64_t H = 1469598103934665603ull;
        for (unsigned char C : Text) { H ^= C; H *= 1099511628211ull; }
        return H;
    }

    inline std::wstring Folder() {
        static std::wstring Cached; static bool Done = false;
        if (Done) return Cached;
        Done = true;
        wchar_t Ready[32768]{};
        if (!GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", Ready, 32768)) return Cached;
        if (GetEnvironmentVariableW(L"TILTED_TELEMETRY", nullptr, 0)) {
            wchar_t Flag[8]{}; GetEnvironmentVariableW(L"TILTED_TELEMETRY", Flag, 8);
            if (Flag[0] == L'0') return Cached; // turned off in the launcher
        }
        std::wstring Path = Ready; auto Slash = Path.find_last_of(L"\\/");
        if (Slash == std::wstring::npos) return Cached;
        Path.resize(Slash + 1); Path += L"telemetry";
        CreateDirectoryW(Path.c_str(), nullptr);
        Cached = Path;
        return Cached;
    }

    // Per-install random salt (telemetry/salt.txt), so pseudonyms can't be reversed with a name list.
    inline std::string Salt() {
        static std::string Cached; if (!Cached.empty()) return Cached;
        auto Dir = Folder(); if (Dir.empty()) return Cached = "nosalt";
        auto Path = Dir + L"\\salt.txt";
        FILE* F = nullptr;
        if (_wfopen_s(&F, Path.c_str(), L"rb") == 0 && F) { char B[80]{}; size_t N = fread(B, 1, 64, F); fclose(F); Cached.assign(B, N); }
        if (Cached.size() < 16) {
            GUID G{}; CoCreateGuid(&G); char B[64];
            snprintf(B, sizeof(B), "%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x", G.Data1, G.Data2, G.Data3, G.Data4[0], G.Data4[1], G.Data4[2], G.Data4[3], G.Data4[4], G.Data4[5], G.Data4[6], G.Data4[7]);
            Cached = B;
            if (_wfopen_s(&F, Path.c_str(), L"wb") == 0 && F) { fwrite(Cached.data(), 1, Cached.size(), F); fclose(F); }
        }
        return Cached;
    }

    inline std::string HumanId(const std::string& PlayerName) {
        char B[24]; snprintf(B, sizeof(B), "h%012llx", (unsigned long long)(Hash(Salt() + "|" + PlayerName) & 0xffffffffffffull));
        return B;
    }

    inline std::string NowIso() {
        SYSTEMTIME T{}; GetSystemTime(&T); char B[32];
        snprintf(B, sizeof(B), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", T.wYear, T.wMonth, T.wDay, T.wHour, T.wMinute, T.wSecond, T.wMilliseconds);
        return B;
    }

    inline std::string NewMatchId() {
        GUID G{}; CoCreateGuid(&G); SYSTEMTIME T{}; GetSystemTime(&T); char B[64];
        snprintf(B, sizeof(B), "%04u%02u%02u-%02u%02u%02u-%08lx", T.wYear, T.wMonth, T.wDay, T.wHour, T.wMinute, T.wSecond, G.Data1);
        return B;
    }

    // Background appender. Lines are (file, text); the worker keeps the current file open.
    namespace Writer {
        inline std::mutex Lock; inline std::condition_variable Wake;
        inline std::deque<std::pair<std::wstring, std::string>> Queue;
        inline bool Started = false; inline std::atomic<unsigned> Dropped{0};
        inline void Worker() {
            std::wstring OpenPath; FILE* File = nullptr; size_t Written = 0;
            for (;;) {
                std::deque<std::pair<std::wstring, std::string>> Batch;
                { std::unique_lock<std::mutex> Guard(Lock); Wake.wait(Guard, [] { return !Queue.empty(); }); Batch.swap(Queue); }
                for (auto& [Path, Line] : Batch) {
                    if (Path != OpenPath) {
                        if (File) fclose(File);
                        File = nullptr; OpenPath = Path; Written = 0;
                        // Shared for reading: the backend reads the file while the match is still running.
                        File = _wfsopen(Path.c_str(), L"ab", _SH_DENYNO);
                        if (File) { _fseeki64(File, 0, SEEK_END); Written = size_t(_ftelli64(File)); }
                    }
                    if (!File || Written + Line.size() > MaxMatchBytes) { ++Dropped; continue; }
                    fwrite(Line.data(), 1, Line.size(), File); Written += Line.size();
                    if (Line.find("\"type\":\"match_end\"") != std::string::npos) { fclose(File); File = nullptr; OpenPath.clear(); }
                }
                if (File) fflush(File);
            }
        }
        inline void Append(const std::wstring& Path, std::string Line) {
            std::lock_guard<std::mutex> Guard(Lock);
            if (!Started) { Started = true; std::thread(Worker).detach(); }
            if (Queue.size() >= MaxQueuedLines) { ++Dropped; return; }
            Queue.emplace_back(Path, std::move(Line)); Wake.notify_one();
        }
    }

    // One match at a time (the server hosts one). Begin() starts a new file; Emit() appends.
    struct Match {
        std::string Id; std::wstring Path; long long Seq = 0; bool Open = false; bool Ended = false;
        double LastCheckpoint = -1e9;
    };
    inline Match& Current() { static Match M; return M; }

    inline bool Active() { return Current().Open && !Current().Ended; }

    inline void Emit(const char* Type, double ServerSeconds, const Fields& Data = Fields()) {
        auto& M = Current();
        if (!M.Open || M.Path.empty()) return;
        if (M.Ended && std::string(Type) != "match_end") return;
        long long Seq = ++M.Seq;
        std::string Line = "{\"v\":" + std::to_string(SchemaVersion) + ",\"match\":\"" + M.Id + "\",\"seq\":" + std::to_string(Seq)
            + ",\"id\":\"" + M.Id + ":" + std::to_string(Seq) + "\"";
        char T[32]; snprintf(T, sizeof(T), ",\"t\":%.2f", std::isfinite(ServerSeconds) ? ServerSeconds : 0.0); Line += T;
        Line += ",\"ts\":\"" + NowIso() + "\",\"type\":\"" + Escape(Type) + "\"";
        if (!Data.Empty()) Line += "," + Data.Text();
        Line += "}\n";
        Writer::Append(M.Path, std::move(Line));
    }

    // Starts a match file. Returns false when telemetry is off or the folder is unavailable.
    inline bool Begin(double ServerSeconds, const Fields& Info) {
        auto Dir = Folder(); auto& M = Current();
        if (Dir.empty()) return false;
        if (M.Open && !M.Ended) Emit("match_end", ServerSeconds, Fields().Str("reason", "superseded"));
        M = Match{};
        M.Id = NewMatchId();
        M.Path = Dir + L"\\" + std::wstring(M.Id.begin(), M.Id.end()) + L".jsonl";
        M.Open = true;
        Fields Start = Info;
        Start.Int("schema", SchemaVersion).Str("botVersion", BotVersion).Str("serverBuild", __DATE__ " " __TIME__);
        Emit("match_start", ServerSeconds, Start);
        return true;
    }

    inline void End(double ServerSeconds, const Fields& Info) {
        auto& M = Current();
        if (!M.Open || M.Ended) return;
        Fields Data = Info; Data.Int("dropped", Writer::Dropped.load());
        Emit("match_end", ServerSeconds, Data);
        M.Ended = true;
    }

    // Periodic state snapshot, at most every Interval seconds.
    inline bool CheckpointDue(double ServerSeconds, double Interval = 30) {
        auto& M = Current();
        if (!Active() || ServerSeconds - M.LastCheckpoint < Interval) return false;
        M.LastCheckpoint = ServerSeconds; return true;
    }
}
