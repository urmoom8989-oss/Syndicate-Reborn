#pragma once
#include <string>
#include <vector>
#include <cstdint>
// Season-accurate squad Reboot Card bookkeeping. Pure logic - NO engine types - so it cannot affect the
// game-server build and its rules can be reasoned about in isolation. The caller (SpectraBotsIntegration)
// supplies primitives from the live match and performs the actual van activation / respawn separately.
//
// The one intentional difference from retail Fortnite: a dead squadmate's card is AUTO-collected for the
// squad (no physical pickup). Everything else follows the normal rules - the squad must still take the card
// to a Reboot Van to actually bring the teammate back (that step lives in the engine-side code, not here).
namespace SyndicateReboot {
struct Card {
    std::string playerId;   // stable per-match id: account id for humans, "bot:<name>" for bots
    std::wstring name;      // display name, for logs / HUD
    int team = -1;          // squad (team index)
    double x = 0, y = 0, z = 0; // elimination location (where the card dropped)
    bool isBot = false;
    bool collected = false; // auto-collected by a surviving squadmate (no manual pickup)
    bool consumed = false;  // spent at a Reboot Van -> teammate was rebooted
    double createdAt = 0;
};
class Squads {
    std::vector<Card> cards;
    static constexpr size_t Limit = 256; // a match has <= ~25 squads * 4; cap guards against leaks
public:
    // Reboot Vans/Cards exist only Season 8 onward (8.50 / 9.40 / 10.31 / 11.00) and only in team modes.
    // Chapter 1 Pre-Season..Season 7 and all solo playlists use normal elimination (no reboot).
    static bool Supported(double version, bool teamMode) { return teamMode && version >= 8.0; }

    // A squadmate (human or bot) was eliminated. Records one card (deduped by playerId while unconsumed),
    // not yet collected. Returns the card index, or -1 if a live card for that player already exists or the
    // store is full. Does NOT reboot anyone - only the card is created here.
    int OnEliminated(const std::string& id, const std::wstring& name, int team,
                     double x, double y, double z, bool isBot, double now) {
        if (id.empty()) return -1;
        for (auto& c : cards) if (c.playerId == id && !c.consumed) return -1; // one live card per player
        if (cards.size() >= Limit) return -1;
        cards.push_back(Card{ id, name, team, x, y, z, isBot, false, false, now });
        return int(cards.size()) - 1;
    }

    // Auto-collect every uncollected, unconsumed card for a team, but ONLY if the squad still has a living
    // member to carry it (aliveOnTeam). Returns how many cards were collected. No physical interaction.
    int AutoCollect(int team, bool aliveOnTeam) {
        if (!aliveOnTeam) return 0;
        int n = 0;
        for (auto& c : cards) if (c.team == team && !c.collected && !c.consumed) { c.collected = true; ++n; }
        return n;
    }

    // Is a reboot attempt at a van valid? Guards the rules the spec lists: card must be collected, not
    // already spent, belong to the activator's own squad, the target not already alive, and the match live.
    bool CanReboot(int index, int activatorTeam, bool matchEnded, bool targetAlreadyAlive) const {
        if (index < 0 || index >= int(cards.size())) return false; // invalid card
        const Card& c = cards[index];
        return c.collected && !c.consumed && !matchEnded && !targetAlreadyAlive && c.team == activatorTeam;
    }

    // Mark a card spent after a van successfully brought the teammate back. Prevents a second reboot.
    void Consume(int index) { if (index >= 0 && index < int(cards.size())) cards[index].consumed = true; }

    // Collected, unspent cards a squad is currently carrying (for van activation and the dev overlay).
    std::vector<int> Carried(int team) const {
        std::vector<int> r;
        for (size_t i = 0; i < cards.size(); ++i)
            if (cards[i].team == team && cards[i].collected && !cards[i].consumed) r.push_back(int(i));
        return r;
    }
    int CarriedCount(int team) const {
        int n = 0; for (auto& c : cards) if (c.team == team && c.collected && !c.consumed) ++n; return n;
    }

    const Card* Get(int index) const { return (index >= 0 && index < int(cards.size())) ? &cards[index] : nullptr; }
    void Reset() { cards.clear(); }
    size_t Size() const { return cards.size(); }
};
}
