// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using System.Threading.Tasks;

namespace SyndicateClassic;

internal sealed class AccountClient
{
	private readonly HttpClient _http;
    internal AccountClient(HttpClient? http = null)
    {
        _http = http ?? new HttpClient { BaseAddress = new Uri("http://127.0.0.1:3551/"), Timeout = TimeSpan.FromSeconds(90) /* account transfer to a Radmin host can be slow */ };
    }

	public string? Token { get; private set; }

	public string? Username { get; private set; }

	public bool SignedIn => Token != null;

	// Syndicate Arena HUD: live Hype for the signed-in player's current Arena match (short timeout,
	// polled once a second, so it never holds up the launcher). Null when unavailable.
	private static readonly HttpClient ArenaHttp = new HttpClient { BaseAddress = new Uri("http://127.0.0.1:3551/"), Timeout = TimeSpan.FromSeconds(2) };
	public async Task<JsonElement?> ArenaLive()
	{
		var token = Token;
		if (token == null) return null;
		using var request = new HttpRequestMessage(HttpMethod.Get, "syndicate/arena/live");
		request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
		using var response = await ArenaHttp.SendAsync(request);
		if (!response.IsSuccessStatusCode) return null;
		return await response.Content.ReadFromJsonAsync<JsonElement>();
	}

	private string? _rememberToken;

	private async Task<JsonElement> Post(string route, object data, bool authorized = false)
	{
		if(!Launcher.TryAutoLocate(out var owner, out _) || !SessionProcesses.OwnedRunning(owner,"backend"))
            throw new InvalidOperationException("Start this launcher’s local services before signing in.");
        using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Post, route)
		{
			Content = JsonContent.Create(data)
		};
		if (authorized)
		{
			request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", Token);
		}
		using HttpResponseMessage response = await _http.SendAsync(request);
		if (route == "launcher/resume" && ((int)response.StatusCode == 401 || (int)response.StatusCode == 403)) RememberedLogin.Clear();
		JsonElement result;
		try { result = await response.Content.ReadFromJsonAsync<JsonElement>(); }
		catch (Exception) when (!response.IsSuccessStatusCode) { result = default; }
		JsonElement value;
		if (!response.IsSuccessStatusCode)
		{
			if (result.ValueKind == JsonValueKind.Object && result.TryGetProperty("message", out value)) throw new InvalidOperationException(value.GetString());
			if ((int)response.StatusCode == 404) throw new InvalidOperationException("Your local services are older than this launcher. Close Syndicate, run APPLY BACKEND FIXES, then try again.");
			throw new InvalidOperationException(result.ValueKind == JsonValueKind.Object && result.TryGetProperty("errorMessage", out value) ? value.GetString() : "The account request failed.");
		}
		return result;
	}

    public async Task ResetPassword(string email,string password,string confirmation)
    {
        await Post("launcher/reset-password",new {email,password,confirmPassword=confirmation});
        RememberedLogin.Clear();Token=null;Username=null;_rememberToken=null;
    }

	public async Task SignIn(string email, string password, string? username, bool remember = false)
	{
		JsonElement jsonElement = await Post((username == null) ? "launcher/login" : "launcher/register", new { email, password, username, remember });
		Token = jsonElement.GetProperty("token").GetString();
		Username = jsonElement.GetProperty("username").GetString();
        _rememberToken = jsonElement.TryGetProperty("rememberToken", out var saved) ? saved.GetString() : null;
        RememberedLogin.Clear();
        if (remember && _rememberToken != null) RememberedLogin.Save(_rememberToken);
	}

    public async Task<bool> Restore()
    {
        var token = RememberedLogin.Load();
        if (token == null) return false;
        var result = await Post("launcher/resume", new { rememberToken = token });
        Token = result.GetProperty("token").GetString();
        Username = result.GetProperty("username").GetString();
        _rememberToken = token;
        return true;
    }

    // Username-only play: signs in an existing local player or creates a new one.
    public async Task QuickPlay(string username)
    {
        JsonElement result = await Post("launcher/quick", new { username });
        Token = result.GetProperty("token").GetString();
        Username = result.GetProperty("username").GetString();
        _rememberToken = result.TryGetProperty("rememberToken", out var saved) ? saved.GetString() : null;
        RememberedLogin.Clear();
        if (_rememberToken != null) RememberedLogin.Save(_rememberToken);
    }

    public async Task<string[]> Players()
    {
        JsonElement result = await Post("launcher/players", new { });
        return result.GetProperty("players").EnumerateArray().Select(p => p.GetProperty("username").GetString() ?? "").Where(n => n.Length > 0).ToArray();
    }

    // Renames a local player; returns the saved name. Updates the signed-in name if it was this player.
    public async Task<string> RenamePlayer(string username, string newName)
    {
        var result = await Post("launcher/players/rename", new { username, newName });
        var saved = result.GetProperty("username").GetString() ?? newName;
        if (string.Equals(Username, username, StringComparison.OrdinalIgnoreCase)) Username = saved;
        return saved;
    }

    // Deletes a local player (account, locker and progress) from this PC.
    public async Task DeletePlayer(string username)
    {
        await Post("launcher/players/delete", new { username });
        if (string.Equals(Username, username, StringComparison.OrdinalIgnoreCase))
        {
            Token = null; Username = null; _rememberToken = null;
            RememberedLogin.Clear();
        }
    }

    // "Unlock all cosmetics": current state, and turning it on/off. Returns (enabled, changed, available).
    public async Task<(bool Enabled, int Changed, int Available)> UnlockAll(bool? enable)
    {
        var result = await Post(enable == null ? "launcher/unlockall/status" : "launcher/unlockall/set", new { enabled = enable ?? false }, authorized: true);
        int Int(string name) => result.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetInt32() : 0;
        return (result.TryGetProperty("enabled", out var e) && e.ValueKind == JsonValueKind.True, Int("changed"), Int("available"));
    }

    // "Remove default outfits from locker": null reads the setting, true/false turns it on/off.
    public async Task<(bool Enabled, int Changed)> HideDefaults(bool? enable)
    {
        var result = await Post(enable == null ? "launcher/hidedefaults/status" : "launcher/hidedefaults/set", new { enabled = enable ?? false }, authorized: true);
        int changed = result.TryGetProperty("changed", out var c) && c.ValueKind == JsonValueKind.Number ? c.GetInt32() : 0;
        return (result.TryGetProperty("enabled", out var e) && e.ValueKind == JsonValueKind.True, changed);
    }

    // "Remove unreleased win umbrellas": null reads the setting, true/false turns it on/off.
    public async Task<(bool Enabled, int Changed)> HideWinUmbrellas(bool? enable)
    {
        var result = await Post(enable == null ? "launcher/hidewinumbrellas/status" : "launcher/hidewinumbrellas/set", new { enabled = enable ?? false }, authorized: true);
        int changed = result.TryGetProperty("changed", out var c) && c.ValueKind == JsonValueKind.Number ? c.GetInt32() : 0;
        return (result.TryGetProperty("enabled", out var e) && e.ValueKind == JsonValueKind.True, changed);
    }

    // "Save the World skins" (needs Unlock all): null reads the switch, true/false turns it on/off.
    public async Task<bool> StwSkins(bool? enable)
    {
        var result = await Post(enable == null ? "launcher/stwskins/status" : "launcher/stwskins/set", new { enabled = enable ?? false }, authorized: true);
        return result.TryGetProperty("enabled", out var e) && e.ValueKind == JsonValueKind.True;
    }

    // Save the World hero in Battle Royale (needs Unlock all and STW skins): null reads the pick and the hero list, "" clears the pick.
    public Task<JsonElement> StwHero(string? hero)
    {
        return hero == null ? Post("launcher/stwhero/status", new { }, authorized: true) : Post("launcher/stwhero/set", new { hero }, authorized: true);
    }

	public Task<JsonElement> Passes()
	{
		return Post("launcher/passes/catalog", new { }, authorized: true);
	}

	// Hidden item shop (SecretShop.cs): every item-shop cosmetic, and buying one.
	public Task<JsonElement> SecretShop() => Post("launcher/shop/catalog", new { }, authorized: true);
	public Task<JsonElement> BuySecretShop(string templateId) => Post("launcher/shop/purchase", new { templateId }, authorized: true);

	// Settings > Redeem code: a one-time code that gives an exclusive item (backend syndicateRedeem.js).
	public Task<JsonElement> Redeem(string code) => Post("launcher/redeem", new { code }, authorized: true);

	// Career totals for the Stats tab (solo, duo, squad and total).
	public Task<JsonElement> Stats() => Post("launcher/stats", new { }, authorized: true);

	public Task<JsonElement> Level(int season) => Post("launcher/level", new { season }, authorized: true);

	public Task<JsonElement> BuyPass(string passId)
	{
		return Post("launcher/passes/purchase", new { passId }, authorized: true);
	}

	public Task<JsonElement> FinishTravel()
	{
		return Post("launcher/travel/finish", new { }, authorized: true);
	}

	public async Task<string> Exchange()
	{
		return (await Post("launcher/exchange", new { }, authorized: true)).GetProperty("code").GetString() ?? throw new InvalidOperationException("No login code returned.");
	}

	public async Task SignOut()
	{
		try
		{
			if (Token != null)
			{
				await Post("launcher/logout", new { rememberToken = _rememberToken }, authorized: true);
			}
		}
		finally
		{
			Token = null;
			Username = null;
            _rememberToken = null;
            RememberedLogin.Clear();
		}
	}
}