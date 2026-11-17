// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System.IO;

namespace SyndicateClassic;

// The backend folder is "Syndicate-backend". Installs made before 2026-10-31 have it as "Helix-backend-main";
// the launcher renames it on start (Migrate) and everything also still finds the old name until then.
// Component manifests may list either prefix; Map turns either into the folder actually on disk.
public static class BackendFolder
{
	public const string Name = "Syndicate-backend", OldName = "Helix-backend-main";
	public static string For(string root) =>
		Directory.Exists(Path.Combine(root, Name)) || !Directory.Exists(Path.Combine(root, OldName)) ? Name : OldName;
	public static bool Present(string root) =>
		File.Exists(Path.Combine(root, Name, "start.bat")) && File.Exists(Path.Combine(root, Name, "src", "index.js"))
		|| File.Exists(Path.Combine(root, OldName, "start.bat")) && File.Exists(Path.Combine(root, OldName, "src", "index.js"));
	// "Helix-backend-main/..." or "Syndicate-backend/..." -> the same file under the old name (the logical name the
	// update checks were written for).
	public static string Logical(string relative) =>
		relative.StartsWith(Name + "/", System.StringComparison.Ordinal) ? OldName + relative.Substring(Name.Length) : relative;
	// A manifest path -> the path under the backend folder this install really has.
	public static string Map(string root, string relative)
	{
		var logical = Logical(relative);
		return logical.StartsWith(OldName + "/", System.StringComparison.Ordinal) ? For(root) + logical.Substring(OldName.Length) : relative;
	}
	private static bool _tried;
	// Rename an old install's folders once per launcher start, while nothing runs from them. A failed move (the
	// backend still running, a file open) changes nothing: the old name keeps working and the next start tries again.
	public static void Migrate(string root)
	{
		if (_tried) return; _tried = true;
		try
		{
			var oldDir = Path.Combine(root, OldName); var newDir = Path.Combine(root, Name);
			if (Directory.Exists(oldDir) && !Directory.Exists(newDir)) Directory.Move(oldDir, newDir);
			else if (Directory.Exists(oldDir) && Directory.Exists(newDir))
			{
				// A full package was extracted over an old install: the new folder wins, so bring the local
				// settings file across (packages never contain it).
				var oldEnv = Path.Combine(oldDir, ".env"); var newEnv = Path.Combine(newDir, ".env");
				if (File.Exists(oldEnv) && !File.Exists(newEnv)) File.Copy(oldEnv, newEnv);
			}
		}
		catch { }
		try
		{
			// The launcher's own component copies and their manifest use the folder name as a path prefix.
			var components = Path.Combine(System.AppContext.BaseDirectory, "components");
			var oldC = Path.Combine(components, OldName); var newC = Path.Combine(components, Name);
			var manifest = Path.Combine(components, "manifest.json");
			if (!Directory.Exists(oldC) || Directory.Exists(newC) || !File.Exists(manifest)) return;
			var text = File.ReadAllText(manifest);
			var version = Path.Combine(root, "Runtime", "components-version.txt");
			bool upToDate = File.Exists(version) && File.ReadAllText(version).Trim().Equals(System.Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(manifest))), System.StringComparison.OrdinalIgnoreCase);
			Directory.Move(oldC, newC);
			var updated = text.Replace("\"" + OldName + "/", "\"" + Name + "/");
			File.WriteAllText(manifest, updated, new System.Text.UTF8Encoding(false));
			// Same files, new paths: keep the fast start instead of a full re-check.
			if (upToDate) File.WriteAllText(version, System.Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(manifest))));
		}
		catch { }
	}
}

public sealed class LauncherPaths
{
	public string Root { get; init; } = "";

	public string? GameInstallRootOverride { get; init; }

	public string GameInstallRoot => string.IsNullOrWhiteSpace(GameInstallRootOverride) ? Root : GameInstallRootOverride;

	public string GameRoot => Path.Combine(GameInstallRoot, "FortniteGame");

	public string GameExe => Path.Combine(GameRoot, "Binaries", "Win64", "FortniteClient-Win64-Shipping.exe");

	public string GameWorkDir => Path.Combine(GameRoot, "Binaries", "Win64");

	public string BackendDir => Path.Combine(Root, BackendFolder.For(Root));

	public string StartBat => Path.Combine(BackendDir, "start.bat");

	public string RedirectDll { get; init; } = "";
}
