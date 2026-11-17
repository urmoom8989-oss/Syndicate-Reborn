#include "../headers/config.h"
#include <utility>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#include <imgui_internal.h>
#include "../settings/colors.h"

extern HMODULE g_hModule;

void c_config::init_config()
{
    static bool init = false;

    if (!std::exchange(init, true))
    {
        add_option<checkbox_t>("Enable aim assist", true);
        add_option<checkbox_t>("Silent aim", false);
        add_option<checkbox_t>("Magic Bullet", false);
        add_option<checkbox_t>("Always headshots", false);
        add_option<checkbox_t>("Sniper pump", false);
        add_option<checkbox_t>("Sniper pump uses aim FOV", false);
        add_option<checkbox_t>("No spread", false);
        add_option<checkbox_t>("No bloom", false);
        add_option<slider_float_t>("Aim FOV", 10.f, 1.f, 18.4f, "%.1f");
        add_option<slider_float_t>("Smoothness", 8.f, 1.f, 20.f, "%.1f");
        add_option<dropdown_t>("Aim key", 0, string_t{ "Right Mouse", "Left Mouse", "Middle Mouse", "Shift", "Alt", "X", "C", "V" });
        add_option<dropdown_t>("Aim bone", 0, string_t{ "Head", "Neck", "Chest", "Pelvis" });
        add_option<checkbox_t>("Draw FOV circle", true);
        add_option<checkbox_t>("Draw target highlight", false);
        add_option<slider_float_t>("FOV thickness", 1.5f, 0.5f, 5.f, "%.1f");
        add_option<color_edit_t>("FOV color", col_t{ 0.44f, 0.47f, 1.f, 1.f }, true);
        add_option<slider_float_t>("Spread amount", 1.f, 0.f, 1.f, "%.2f");

        add_option<checkbox_t>("Enable ESP", true);
        add_option<checkbox_t>("Player box", true);
        add_option<color_edit_t>("Player box##color", col_t{ 1.f, 0.24f, 0.24f, 1.f }, true);
        add_option<checkbox_t>("Corner box", false);
        add_option<checkbox_t>("Box outline", true);
        add_option<checkbox_t>("Box fill", false);
        add_option<color_edit_t>("Box fill##color", col_t{ 1.f, 0.24f, 0.24f, 0.15f }, true);
        add_option<checkbox_t>("Player skeleton", true);
        add_option<color_edit_t>("Player skeleton##color", col_t{ 1.f, 1.f, 1.f, 1.f }, true);
        add_option<checkbox_t>("Curved skeleton", false);
        add_option<checkbox_t>("Spaghetti skeleton", false);
        add_option<checkbox_t>("Skeleton outline", true);
        add_option<checkbox_t>("Chams", false);
        add_option<checkbox_t>("Wireframe chams", false);
        add_option<color_edit_t>("Chams color", col_t{ 0.12f, 0.76f, 0.29f, 1.f }, true);
        add_option<checkbox_t>("Player name", true);
        add_option<color_edit_t>("Name color", col_t{ 1.f, 1.f, 1.f, 1.f }, true);
        add_option<checkbox_t>("Distance", true);
        add_option<color_edit_t>("Distance color", col_t{ 0.78f, 0.78f, 0.78f, 1.f }, true);
        add_option<checkbox_t>("Player health bar", false);
        add_option<color_edit_t>("Player health bar##color", col_t{ 0.12f, 0.86f, 0.32f, 1.f }, true);
        add_option<dropdown_t>("Health bar side", 0, string_t{ "Left", "Right" });
        add_option<checkbox_t>("Snapline", false);
        add_option<color_edit_t>("Snapline color", col_t{ 1.f, 0.24f, 0.24f, 1.f }, true);
        add_option<dropdown_t>("Snapline from", 0, string_t{ "Bottom", "Center", "Top" });
        add_option<checkbox_t>("China hat", false);
        add_option<color_edit_t>("China hat##color", col_t{ 0.9f, 0.9f, 0.9f, 1.f }, true);
        add_option<checkbox_t>("Self china hat", false);
        add_option<slider_float_t>("Box thickness", 1.6f, 0.5f, 5.f, "%.1f");
        add_option<slider_float_t>("Box width", 0.45f, 0.25f, 0.80f, "%.2f");
        add_option<slider_float_t>("Skeleton thickness", 1.4f, 0.5f, 5.f, "%.1f");
        add_option<slider_float_t>("China thickness", 1.5f, 0.5f, 5.f, "%.1f");
        add_option<slider_int_t>("Render distance", 0, 0, 500, "%d");
        add_option<checkbox_t>("Show on teammates", false);
        add_option<checkbox_t>("Show on enemies", true);
        add_option<checkbox_t>("Only show on screen", true);
        add_option<checkbox_t>("No self ESP", true);
        add_option<checkbox_t>("Team check", true);
        add_option<checkbox_t>("Visible check", false);

        add_option<checkbox_t>("Floor loot", false);
        add_option<checkbox_t>("Chests", false);
        add_option<checkbox_t>("Loot distance", true);
        add_option<slider_float_t>("Loot range", 0.f, 0.f, 500.f, "%.0f");
        add_option<checkbox_t>("Exclude ammo", false);
        add_option<checkbox_t>("Exclude materials", false);
        add_option<checkbox_t>("Common", true);
        add_option<checkbox_t>("Uncommon", true);
        add_option<checkbox_t>("Rare", true);
        add_option<checkbox_t>("Epic", true);
        add_option<checkbox_t>("Legendary", true);
        add_option<checkbox_t>("Mythic", true);

        add_option<checkbox_t>("Vehicle fly", false);
        add_option<slider_float_t>("Vehicle speed", 4.f, 1.f, 15.f, "%.0f");
        add_option<checkbox_t>("Spinbot", false);
        add_option<checkbox_t>("Debug overlay", false);
        add_option<dropdown_t>("Menu key", 0, string_t{ "Insert only", "Home", "Page Up", "Page Down", "Pause", "F6", "F7", "F8", "F9", "F10", "F12" });
        add_option<checkbox_t>("Stream proof", false);
        add_option<slider_float_t>("Spin speed", 15.f, 1.f, 30.f, "%.0f");
        add_option<checkbox_t>("Time of day", false);
        add_option<slider_float_t>("Hour of day", 12.f, 0.f, 24.f, "%.1f");
        add_option<checkbox_t>("Zipline fly", false);
        add_option<checkbox_t>("FOV changer", false);
        add_option<slider_float_t>("FOV value", 45.f, 0.f, 180.f, "%.0f");
        add_option<checkbox_t>("Auto revive", false);
        add_option<checkbox_t>("Instant revive", false);
        add_option<checkbox_t>("ADS in air", false);
        add_option<checkbox_t>("Fly", false);
        add_option<slider_float_t>("Fly speed", 2.f, 0.5f, 10.f, "%.1fx");
        add_option<dropdown_t>("Fly key", 1, string_t{ "None", "F", "G", "H", "J", "K", "L", "Z", "X", "V", "Caps Lock", "Mouse 4", "Mouse 5" });
        add_option<checkbox_t>("Speed boost", false);
        add_option<slider_float_t>("Speed multiplier", 2.5f, 1.f, 3.f, "%.1fx");
        add_option<checkbox_t>("God mode", false);
        add_option<checkbox_t>("Heal while moving", false);
        add_option<dropdown_t>("Heal key", 3, string_t{ "None", "F", "G", "H", "J", "K", "L", "Z", "X", "V", "Caps Lock", "Mouse 4", "Mouse 5" });
        add_option<dropdown_t>("Teleport key", 0, string_t{ "None", "F", "G", "H", "J", "K", "L", "Z", "X", "V", "Caps Lock", "Mouse 4", "Mouse 5" });
        add_option<dropdown_t>("Spawn item", 0, string_t{ "Assault rifle", "Pump shotgun", "Tactical shotgun", "SMG", "Bolt sniper", "Rocket launcher", "LMG", "Guided missile", "Big shield", "Small shields", "Med kit", "Bandages", "Chug jug / Lean", "Boogie bomb", "Port-a-fort" });

        add_option<checkbox_t>("Projectile TP", false);
        add_option<checkbox_t>("No recoil", false);
        add_option<slider_float_t>("Recoil amount", 1.f, 0.f, 1.f, "%.2f");
        add_option<checkbox_t>("No reload", false);
        add_option<slider_float_t>("Reload amount", 1.f, 0.f, 1.f, "%.2f");
        add_option<checkbox_t>("No charge", false);
        add_option<slider_float_t>("Charge amount", 1.f, 0.f, 1.f, "%.2f");
        add_option<checkbox_t>("Rapid fire", false);
        add_option<slider_float_t>("Fire speed", 2.f, 1.f, 3.f, "%.1f");
        add_option<checkbox_t>("Damage multiplier", false);
        add_option<slider_float_t>("Damage scale", 2.f, 1.f, 10.f, "%.1f");
        add_option<checkbox_t>("Fast pickaxe", false);
        add_option<slider_float_t>("Pickaxe speed", 2.f, 1.f, 50.f, "%.1f");
        add_option<checkbox_t>("Automatic weapons", false);
        add_option<checkbox_t>("Double pump", false);
        add_option<checkbox_t>("Infinite ammo", false);
        add_option<checkbox_t>("Infinite materials", false);

        add_option<checkbox_t>("Custom color", false);
        add_option<color_edit_t>("Custom color##color", col_t{ clr->accent.Value.x, clr->accent.Value.y, clr->accent.Value.z, 1.f });
        add_option<dropdown_t>("Menu theme", 0, string_t{ "Default", "Christmas", "Halloween", "Easter", "Valentine's", "Midnight", "Crimson", "Ocean", "Emerald", "Sunset", "Gold", "Mono", "Neon" });
        add_option<dropdown_t>("Menu scale", 1, string_t{ "0.75x", "1x", "1.5x", "2x" });
        add_option<slider_float_t>("Animation speed", 1.f, 0.1f, 2.f, "%.1fx");
    }
}

void process_checkbox_keybind(checkbox_t& widget)
{
    if (!widget.keybind || widget.key_data.key == 0)
        return;

    const bool is_toggle_mode = widget.key_data.mode == 0;

    if (is_toggle_mode)
    {
        if (ImGui::IsKeyPressed((ImGuiKey)widget.key_data.key))
        {
            if (widget.is_active)
            {
                widget.callback = widget.stored_value;
                widget.is_active = false;
            }
            else
            {
                widget.stored_value = widget.callback;
                widget.callback = !widget.callback;
                widget.is_active = true;
            }
        }
    }
    else
    {
        if (ImGui::IsKeyDown((ImGuiKey)widget.key_data.key))
        {
            if (!widget.is_active)
            {
                widget.stored_value = widget.callback;
                widget.callback = !widget.callback;
                widget.is_active = true;
            }
        }
        else if (widget.is_active)
        {
            widget.callback = widget.stored_value;
            widget.is_active = false;
        }
    }
}

void c_config::process_keybinds()
{
    for (auto& [name, variant] : options)
    {
        std::visit([this](auto&& widget)
            {
                using T = std::decay_t<decltype(widget)>;

                if constexpr (has_keybind<T>::value)
                {
                    if (widget.keybind)
                        process_checkbox_keybind(widget);
                }
            }, variant);
    }
}

namespace {

// Configs live with the player's other Syndicate data, %APPDATA%\SyndicateLauncher\MenuConfigs,
// so an update or a new dev kit never wipes them. Configs saved next to the DLL by older menus
// (Runtime\dlls\configs) are copied over once.
std::filesystem::path OldPresetDir() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(g_hModule, path, MAX_PATH);
    return std::filesystem::path(path).parent_path() / "configs";
}

std::filesystem::path PresetDir() {
    static std::filesystem::path cached;
    if (!cached.empty())
        return cached;
    wchar_t appData[MAX_PATH] = {};
    std::filesystem::path dir;
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH))
        dir = std::filesystem::path(appData) / L"SyndicateLauncher" / L"MenuConfigs";
    else
        dir = OldPresetDir();
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    const std::filesystem::path old = OldPresetDir();
    if (old != dir && std::filesystem::exists(old, error)) {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(old, error)) {
            if (error || !entry.is_regular_file() || entry.path().extension() != ".cfg")
                continue;
            const std::filesystem::path to = dir / entry.path().filename();
            if (!std::filesystem::exists(to, error))
                std::filesystem::copy_file(entry.path(), to, error);
        }
    }
    printf("[config] configs folder: %ls\n", dir.wstring().c_str());
    cached = dir;
    return cached;
}

// The settings you had last time (saved by itself every 2 s when something changed, loaded when
// the menu starts). Not listed with your named configs.
const char kAutosave[] = "_last session";

std::string SafePreset(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_')
            out.push_back((char)c);
    }
    while (!out.empty() && out.front() == ' ')
        out.erase(out.begin());
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    if (out.size() > 24)
        out.resize(24);
    return out;
}

}

// The menu's settings as text, one "kind|name|value" line each. Presets are this text in
// configs\<name>.cfg; a share code is the same text in base64 behind "SYNDICATE-CFG1:" so it
// fits on one line (Discord, a text file) and can be imported from the clipboard.
std::string c_config::export_text() {
    std::ostringstream file;
    file << "syndicate 1\n";
    for (const auto& item : order) {
        const std::string& key = item.first;
        const config_variant& value = options[key];
        if (const checkbox_t* box = std::get_if<checkbox_t>(&value))
            file << "C|" << key << '|' << (box->callback ? 1 : 0) << '\n';
        else if (const slider_int_t* slider = std::get_if<slider_int_t>(&value))
            file << "I|" << key << '|' << slider->callback << '\n';
        else if (const slider_float_t* slider = std::get_if<slider_float_t>(&value))
            file << "F|" << key << '|' << slider->callback << '\n';
        else if (const dropdown_t* drop = std::get_if<dropdown_t>(&value))
            file << "D|" << key << '|' << drop->callback << '\n';
        else if (const color_edit_t* color = std::get_if<color_edit_t>(&value))
            file << "K|" << key << '|' << color->color[0] << '|' << color->color[1] << '|' << color->color[2] << '|' << color->color[3] << '\n';
    }
    return file.str();
}

int c_config::import_text(const std::string& text) {
    std::istringstream file(text);
    std::string line;
    int applied = 0;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("syndicate", 0) == 0 || line.rfind("titanium", 0) == 0) // header line (older presets used the old name)
            continue;
        std::stringstream stream(line);
        std::string kind, key;
        if (!std::getline(stream, kind, '|') || !std::getline(stream, key, '|'))
            continue;
        auto found = options.find(key);
        if (found == options.end())
            continue;
        if (kind == "C") {
            int on = 0;
            stream >> on;
            if (checkbox_t* box = std::get_if<checkbox_t>(&found->second)) {
                box->callback = on != 0;
                ++applied;
            }
        } else if (kind == "I") {
            int value = 0;
            stream >> value;
            if (slider_int_t* slider = std::get_if<slider_int_t>(&found->second)) {
                if (value < slider->min)
                    value = slider->min;
                if (value > slider->max)
                    value = slider->max;
                slider->callback = value;
                ++applied;
            }
        } else if (kind == "F") {
            float value = 0.f;
            stream >> value;
            if (slider_float_t* slider = std::get_if<slider_float_t>(&found->second)) {
                if (!(value >= slider->min))
                    value = slider->min;
                if (value > slider->max)
                    value = slider->max;
                slider->callback = value;
                ++applied;
            }
        } else if (kind == "D") {
            int value = 0;
            stream >> value;
            if (dropdown_t* drop = std::get_if<dropdown_t>(&found->second)) {
                if (value < 0)
                    value = 0;
                if (!drop->items.empty() && value >= (int)drop->items.size())
                    value = (int)drop->items.size() - 1;
                drop->callback = value;
                ++applied;
            }
        } else if (kind == "K") {
            float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
            char bar = 0;
            stream >> r >> bar >> g >> bar >> b >> bar >> a;
            if (color_edit_t* color = std::get_if<color_edit_t>(&found->second)) {
                const float in[4] = { r, g, b, a };
                for (int i = 0; i < 4; ++i)
                    color->color[i] = (in[i] >= 0.f && in[i] <= 1.f) ? in[i] : 1.f;
                ++applied;
            }
        }
    }
    return applied;
}

namespace {

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char kSharePrefix[] = "SYNDICATE-CFG1:";

std::string ToBase64(const std::string& in) {
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(kB64[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6)
        out.push_back(kB64[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

std::string FromBase64(const std::string& in) {
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        const char* at = strchr(kB64, c);
        if (!c || !at)
            continue; // skips '=', spaces and line breaks
        val = (val << 6) + (int)(at - kB64);
        bits += 6;
        if (bits >= 0) {
            out.push_back(char((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

bool SetClipboardText(const std::string& text) {
    if (!OpenClipboard(nullptr))
        return false;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    bool ok = false;
    if (mem) {
        if (void* dst = GlobalLock(mem)) {
            memcpy(dst, text.c_str(), text.size() + 1);
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_TEXT, mem) != nullptr;
        }
        if (!ok)
            GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

std::string GetClipboardText() {
    std::string text;
    if (!OpenClipboard(nullptr))
        return text;
    if (HANDLE data = GetClipboardData(CF_TEXT)) {
        if (const char* src = (const char*)GlobalLock(data)) {
            text.assign(src, strnlen(src, 64 * 1024));
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

}

bool c_config::copy_share_code() {
    const bool ok = SetClipboardText(kSharePrefix + ToBase64(export_text()));
    printf("[config] share code %s clipboard\n", ok ? "copied to the" : "could not be put on the");
    return ok;
}

int c_config::import_share_code() {
    std::string text = GetClipboardText();
    const size_t at = text.find(kSharePrefix);
    int applied = 0;
    if (at != std::string::npos)
        applied = import_text(FromBase64(text.substr(at + sizeof(kSharePrefix) - 1)));
    else if (text.find("syndicate 1") != std::string::npos)
        applied = import_text(text); // a .cfg file's text pasted as it is
    printf("[config] import from clipboard: %d settings applied\n", applied);
    return applied;
}

// Puts the config's .cfg file itself on the clipboard (like Ctrl+C on a file in Explorer), so
// it can be pasted into Discord, a chat or a folder and sent to a friend as a file.
bool c_config::share_preset_file(const std::string& name) {
    const std::string safe = SafePreset(name);
    if (safe.empty())
        return false;
    const std::wstring path = (PresetDir() / (safe + ".cfg")).wstring();
    std::error_code error;
    if (!std::filesystem::exists(path, error))
        return false;
    const size_t bytes = sizeof(DROPFILES) + (path.size() + 2) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!mem)
        return false;
    bool ok = false;
    if (auto* drop = (DROPFILES*)GlobalLock(mem)) {
        drop->pFiles = sizeof(DROPFILES);
        drop->fWide = TRUE;
        memcpy((char*)drop + sizeof(DROPFILES), path.c_str(), path.size() * sizeof(wchar_t));
        GlobalUnlock(mem);
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            ok = SetClipboardData(CF_HDROP, mem) != nullptr;
            CloseClipboard();
        }
    }
    if (!ok)
        GlobalFree(mem);
    printf("[config] %s.cfg %s\n", safe.c_str(), ok ? "copied as a file: paste it in Discord or a folder" : "could not be copied");
    return ok;
}

// The other way: a .cfg file someone sent you, copied (Ctrl+C in Explorer or Discord's download
// folder), is added to your configs under its own name and loaded. Text copied from a .cfg file or
// an older share code works too.
std::string c_config::import_copied_file() {
    std::string loaded;
    if (OpenClipboard(nullptr)) {
        if (HANDLE data = GetClipboardData(CF_HDROP)) {
            HDROP drop = (HDROP)data;
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count && loaded.empty(); ++i) {
                wchar_t file[MAX_PATH] = {};
                if (!DragQueryFileW(drop, i, file, MAX_PATH))
                    continue;
                std::filesystem::path from(file);
                if (from.extension() != ".cfg")
                    continue;
                const std::string safe = SafePreset(from.stem().string());
                if (safe.empty())
                    continue;
                std::error_code error;
                std::filesystem::copy_file(from, PresetDir() / (safe + ".cfg"), std::filesystem::copy_options::overwrite_existing, error);
                if (!error)
                    loaded = safe;
            }
        }
        CloseClipboard();
    }
    if (!loaded.empty()) {
        names_dirty = true;
        load_preset(loaded);
        printf("[config] imported the copied file as %s\n", loaded.c_str());
        return loaded;
    }
    if (import_share_code() > 0) {
        const std::string name = "Imported";
        save_preset(name);
        return name;
    }
    printf("[config] nothing to import: copy a .cfg file first\n");
    return loaded;
}

// Every 2 s: when a setting changed, the new settings are written to the last-session file and to
// the config you loaded or saved last (so it remembers your changes without pressing Save).
void c_config::autosave_tick() {
    static DWORD last = 0;
    static std::string saved;
    const DWORD now = GetTickCount();
    if (last && now - last < 2000)
        return;
    last = now;
    const std::string text = export_text();
    if (saved.empty()) {
        saved = text; // first call: whatever was loaded at start
        return;
    }
    if (text == saved)
        return;
    saved = text;
    std::ofstream file(PresetDir() / (std::string(kAutosave) + ".cfg"), std::ios::trunc);
    if (file)
        file << text;
    if (!active_name.empty()) {
        std::ofstream active(PresetDir() / (active_name + ".cfg"), std::ios::trunc);
        if (active)
            active << text;
    }
    static int told = 0;
    if (told++ < 3)
        printf("[config] changes saved (last session%s%s)\n", active_name.empty() ? "" : " and ", active_name.c_str());
}

bool c_config::load_last_session() {
    std::ifstream file(PresetDir() / (std::string(kAutosave) + ".cfg"));
    if (!file)
        return false;
    std::stringstream text;
    text << file.rdbuf();
    const int applied = import_text(text.str());
    printf("[config] loaded your last session (%d settings)\n", applied);
    return applied > 0;
}

void c_config::open_preset_folder() {
    ShellExecuteW(nullptr, L"open", PresetDir().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool c_config::save_preset(const std::string& name) {
    const std::string safe = SafePreset(name);
    if (safe.empty())
        return false;
    std::ofstream file(PresetDir() / (safe + ".cfg"), std::ios::trunc);
    if (!file)
        return false;
    file << export_text();
    names_dirty = true;
    active_name = safe;
    printf("[config] saved config %s\n", safe.c_str());
    return true;
}

bool c_config::load_preset(const std::string& name) {
    const std::string safe = SafePreset(name);
    if (safe.empty())
        return false;
    std::ifstream file(PresetDir() / (safe + ".cfg"));
    if (!file)
        return false;
    std::stringstream text;
    text << file.rdbuf();
    const int applied = import_text(text.str());
    active_name = safe;
    printf("[config] loaded config %s (%d settings); changes now save to it\n", safe.c_str(), applied);
    return true;
}

bool c_config::delete_preset(const std::string& name) {
    const std::string safe = SafePreset(name);
    if (safe.empty())
        return false;
    std::error_code error;
    names_dirty = true;
    if (active_name == safe)
        active_name.clear();
    return std::filesystem::remove(PresetDir() / (safe + ".cfg"), error);
}

// Listed from disk at most every 2 s (and right after a save or delete): this is called every
// frame the Presets page is open, and a folder scan per frame made the menu stutter.
std::vector<std::string> c_config::preset_names() {
    static std::vector<std::string> cached;
    static DWORD last = 0;
    const DWORD now = GetTickCount();
    if (!names_dirty && last && now - last < 2000)
        return cached;
    names_dirty = false;
    last = now;
    std::vector<std::string>& names = cached;
    names.clear();
    std::error_code error;
    const std::filesystem::path dir = PresetDir();
    if (!std::filesystem::exists(dir, error))
        return names;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, error)) {
        if (error || !entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".cfg" || entry.path().stem().string().rfind('_', 0) == 0)
            continue; // "_last session" is the autosave
        names.push_back(entry.path().stem().string());
    }
    return names;
}
