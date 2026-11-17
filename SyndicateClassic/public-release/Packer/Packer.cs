// Syndicate public-release packer (.NET only; no Python).
// Collects a clean copy of the installed runtime (no accounts, .env, tokens, logs or
// source), writes an AES-256-GCM encrypted payload in the exact format the standalone
// bootstrapper (Program.cs) expects: file = nonce(12) + tag(16) + ciphertext.
// Mirrors the include/exclude and privacy rules of the validated pack.py.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

static class Packer
{
    static string _install, _outDir, _version, _signKey, _backendOverride;

    static int Main(string[] args)
    {
        try
        {
            // --scan <folder>: only run the secret scan on a folder (e.g. a release unpacked with
            // Project-Syndicate-vX.exe --verify-only <folder>). Exit 0 = clean, 2 = secrets found.
            if (args.Length == 2 && args[0] == "--scan")
            {
                string dir = Path.GetFullPath(args[1]);
                var all = Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories).Select(f => Rel(dir, f)).ToList();
                // Only what a release would ship: files the name rules keep out are listed, not read.
                var named = all.Where(r => !Include(r)).ToList();
                var hits = SecretScan(dir, all.Where(Include));
                foreach (var n in named) Console.WriteLine("SHOULD NOT SHIP: " + n);
                foreach (var h in hits) Console.WriteLine("SECRET: " + h);
                Console.WriteLine(hits.Count + named.Count == 0 ? $"CLEAN: {all.Count} files, no secrets found." : $"FOUND {hits.Count} secret(s), {named.Count} private file(s) in {all.Count} files.");
                return hits.Count + named.Count == 0 ? 0 : 2;
            }
            for (int i = 0; i < args.Length - 1; i++)
            {
                if (args[i] == "--install") _install = Path.GetFullPath(args[++i]);
                else if (args[i] == "--out") _outDir = Path.GetFullPath(args[++i]);
                else if (args[i] == "--version") _version = args[++i];
                else if (args[i] == "--sign-key") _signKey = Path.GetFullPath(args[++i]);
                // A pre-minified backend tree to ship instead of the readable source in the install.
                // build-public-release.ps1 produces this; CollectBackend falls back to the install if it is absent.
                else if (args[i] == "--backend") _backendOverride = Path.GetFullPath(args[++i]);
            }
            if (_install == null || _outDir == null || string.IsNullOrWhiteSpace(_version))
                throw new ArgumentException("Usage: --install <SyndicateClassic folder> --out <bundle dir> --version <x.y.z>");
            if (!File.Exists(Path.Combine(_install, "Syndicate-backend", "src", "index.js")) && !File.Exists(Path.Combine(_install, "Helix-backend-main", "src", "index.js")))
                throw new DirectoryNotFoundException("That does not look like a Syndicate install: " + _install);
            Directory.CreateDirectory(_outDir);

            string stage = Path.Combine(Path.GetTempPath(), "SyndicatePayload-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(stage);
            try
            {
                Console.WriteLine("Collecting a clean copy of the runtime (no accounts, .env, tokens, logs or source)...");
                CollectLauncher(stage);
                CollectBackend(stage);
                CollectDependencies(stage);
                CollectGameServer(stage);
                if (_signKey != null) SignProvenance(stage); // write a signed build-provenance.json so the launcher can prove it is genuine

                var files = Directory.EnumerateFiles(stage, "*", SearchOption.AllDirectories)
                    .Select(f => Rel(stage, f)).OrderBy(r => r, StringComparer.Ordinal).ToList();
                if (files.Count == 0) throw new Exception("Nothing was collected.");
                PrivacyScan(stage, files);
                RequireFiles(stage);
                Console.WriteLine($"Collected {files.Count} files.");

                string zip = Path.Combine(stage + ".zip");
                WriteZip(stage, files, zip);
                Encrypt(zip, out byte[] key);
                File.Delete(zip);
                WriteManifest(stage, files);
                Console.WriteLine($"PASS: {files.Count} files encrypted with AES-256-GCM into {Path.Combine(_outDir, "payload.enc")}.");
                return 0;
            }
            finally { TryDelete(stage); }
        }
        catch (Exception e)
        {
            Console.Error.WriteLine("PACK FAILED: " + e.Message);
            return 1;
        }
    }

    static string Rel(string root, string full) =>
        Path.GetRelativePath(root, full).Replace('\\', '/');

    // ---- collection ------------------------------------------------------

    static void CollectLauncher(string stage)
    {
        string src = Path.Combine(_install, "Launcher");
        if (!Directory.Exists(src)) throw new DirectoryNotFoundException("Launcher folder not found. Run BUILD LAUNCHER first.");
        CopyTree(src, Path.Combine(stage, "Launcher"), "Launcher");
    }

    // Sign the launcher DLL's SHA-256 with the release private key so the launcher can verify at startup that it is a
    // genuine, unmodified Syndicate build (BuildProvenance.cs). The private key is passed in and never shipped.
    static void SignProvenance(string stage)
    {
        string dll = Path.Combine(stage, "Launcher", "SyndicateLauncher.dll");
        if (!File.Exists(dll)) throw new FileNotFoundException("Cannot sign the build: the launcher DLL is missing from the staged runtime.");
        string hash;
        using (var sha = System.Security.Cryptography.SHA256.Create())
            hash = Convert.ToHexString(sha.ComputeHash(File.ReadAllBytes(dll))).ToLowerInvariant();
        string payload = "tilted-build-v1|" + _version + "|" + hash;
        using var rsa = System.Security.Cryptography.RSA.Create();
        rsa.ImportFromPem(File.ReadAllText(_signKey));
        byte[] sig = rsa.SignData(System.Text.Encoding.UTF8.GetBytes(payload),
            System.Security.Cryptography.HashAlgorithmName.SHA256, System.Security.Cryptography.RSASignaturePadding.Pkcs1);
        string json = "{\"v\":\"" + _version + "\",\"dll\":\"" + hash + "\",\"sig\":\"" + Convert.ToBase64String(sig) + "\"}";
        File.WriteAllText(Path.Combine(stage, "Launcher", "build-provenance.json"), json);
        Console.WriteLine("Signed build provenance over Launcher/SyndicateLauncher.dll.");
    }

    static void CollectBackend(string stage)
    {
        // Releases ship the backend as Syndicate-backend, whichever name the source install still has.
        // When build-public-release.ps1 hands us a pre-minified tree (--backend), ship that instead of the
        // readable source: the extracted copy on a user's disk is then a scrambled blob, not editable source.
        // If anything is off with the override we fall back to the real install so a release never ships empty.
        string src;
        if (_backendOverride != null && File.Exists(Path.Combine(_backendOverride, "src", "index.js")))
        {
            src = _backendOverride;
            Console.WriteLine("Shipping minified backend from " + src);
        }
        else
        {
            if (_backendOverride != null)
                Console.WriteLine("WARNING: --backend " + _backendOverride + " has no src/index.js; shipping readable backend source.");
            src = Path.Combine(_install, "Syndicate-backend");
            if (!Directory.Exists(src)) src = Path.Combine(_install, "Helix-backend-main");
        }
        CopyTree(src, Path.Combine(stage, "Syndicate-backend"), "Syndicate-backend");
    }

    static void CollectDependencies(string stage)
    {
        foreach (var dep in new[] { "Node", "MongoDB" })
        {
            string src = Path.Combine(_install, "Dependencies", dep);
            if (Directory.Exists(src))
                CopyTree(src, Path.Combine(stage, "Dependencies", dep), "Dependencies/" + dep);
        }
    }

    static void CollectGameServer(string stage)
    {
        string dir = Path.Combine(_install, "Erbium-Quality-of-life-changes-main", "Erbium", "x64", "Release");
        string dll = Path.Combine(dir, "Erbium.dll"), pending = dll + ".pending";
        string pick = null;
        if (File.Exists(dll)) pick = dll;
        if (File.Exists(pending) && (pick == null || File.GetLastWriteTimeUtc(pending) > File.GetLastWriteTimeUtc(dll)))
            pick = pending; // a freshly built game server lands here before the next host start
        if (pick == null) throw new FileNotFoundException("Erbium.dll (the game server) was not found. Run BUILD GAME SERVER first: " + dir);
        string dest = Path.Combine(stage, "Erbium-Quality-of-life-changes-main", "Erbium", "x64", "Release", "Erbium.dll");
        Directory.CreateDirectory(Path.GetDirectoryName(dest));
        File.Copy(pick, dest, true);
    }

    static void CopyTree(string srcDir, string dstDir, string relPrefix)
    {
        foreach (var file in Directory.EnumerateFiles(srcDir, "*", SearchOption.AllDirectories))
        {
            string rel = relPrefix + "/" + Rel(srcDir, file);
            if (!Include(rel)) continue;
            string dest = Path.Combine(dstDir, Rel(srcDir, file).Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(dest));
            File.Copy(file, dest, true);
        }
    }

    // ---- include / exclude (mirrors pack.py) -----------------------------

    static readonly string[] PrivateParts =
        { ".env", "jwt.secret", "tokens.json", "launcher.config.json", "remember-login.dat",
          "runtime", "database", "userdata", "source", "accountbackups", "clientsettings", "updater.json" };
    static readonly string[] SourceExt = { ".cs", ".cpp", ".h", ".csproj", ".vcxproj" };

    static bool Include(string rel)
    {
        string[] parts = rel.Split('/');
        string name = parts[^1];
        string nameL = name.ToLowerInvariant();
        string ext = Path.GetExtension(nameL);
        bool node = parts.Any(p => p.Equals("node_modules", StringComparison.OrdinalIgnoreCase));

        if (name.Contains(".previous-") || name.Contains(".before-") || nameL.EndsWith(".update")) return false;
        if (ext == ".pdb") return false;

        if (parts[0] == "Launcher")
        {
            if (parts.Length > 1 && (parts[1] == "components" || parts[1].StartsWith("Backup-", StringComparison.Ordinal))) return false;
            if (nameL is "repair.pending" or "components.applied" or "repair-payload.json"
                or "updater.json" or "launcher.config.json" or "radmin-host.txt" or "console-diagnostics.log" or "pad-assist.log") return false;
        }
        if (nameL is ".env" or "jwt.secret" or "tokens.json" or "hwid.json") return false;
        // Every .env variant (.env.local, .env.production, prod.env...) except the documented template, developer
        // tokens, and key/certificate stores never ship.
        if ((nameL.StartsWith(".env") && nameL != ".env.example") || ext == ".env") return false;
        if (nameL is "dev-token" or "discord-release-webhook.txt" or "id_rsa" or "id_ed25519") return false;
        if (ext is ".pfx" or ".p12" or ".key" or ".snk" or ".keystore") return false;
        if (parts.Any(p => p.Equals(".keys", StringComparison.OrdinalIgnoreCase) || p.Equals(".git", StringComparison.OrdinalIgnoreCase))) return false;
        if (ext == ".sav" && parts.Any(p => p.Equals("ClientSettings", StringComparison.OrdinalIgnoreCase))) return false;

        foreach (var part in parts)
        {
            string p = part.ToLowerInvariant();
            if (node && p == "source") continue; // some npm packages ship a 'source' folder
            if (Array.IndexOf(PrivateParts, p) >= 0) return false;
        }
        if (!node && Array.IndexOf(SourceExt, ext) >= 0) return false;
        return true;
    }

    static void PrivacyScan(string stage, List<string> files)
    {
        foreach (var rel in files)
            if (!Include(rel))
                throw new Exception("Privacy scan rejected a file that should not ship: " + rel);
        var found = SecretScan(stage, files);
        if (found.Count > 0)
            throw new Exception("Secret scan found " + found.Count + " secret(s) in files that would ship (nothing was built):\n  " + string.Join("\n  ", found));
    }

    // ---- secret scan -----------------------------------------------------
    // The file-name rules above keep .env & co out; this reads what actually ships, so a token pasted into a
    // .js, .json or .txt file stops the release too. Only the file and the kind of secret are reported, never the
    // value. A Syndicate release reached players with a backend .env inside it once; never again.
    static readonly (string Kind, System.Text.RegularExpressions.Regex Rx, bool AlsoNodeModules)[] SecretRules =
    {
        ("Discord webhook", Rx(@"discord(?:app)?\.com/api/webhooks/\d{17,20}/[A-Za-z0-9_-]{60,}"), true),
        ("Discord bot token", Rx(@"\b[MNO][A-Za-z0-9_-]{23,27}\.[A-Za-z0-9_-]{6}\.[A-Za-z0-9_-]{27,40}\b"), true),
        // Library docs and examples (node_modules) are full of sample GitHub/AWS/database credentials, so these are
        // checked in our own files only; Discord webhooks and bot tokens are checked everywhere.
        ("GitHub token", Rx(@"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36}\b|\bgithub_pat_[A-Za-z0-9_]{50,}"), false),
        ("AWS key", Rx(@"\bAKIA[0-9A-Z]{16}\b"), false),
        ("Database password", Rx(@"mongodb(?:\+srv)?://[^/\s:@""'$`{}]+:[^@\s""'$`{}]+@"), false),
        ("Secret setting", Rx(@"(?im)^\s*(?:export\s+)?(?:BOT_TOKEN|DISCORD_TOKEN|CLIENT_SECRET|API_KEY|JWT_SECRET|TILTED_DISCORD_WEBHOOK|LOG_WEBHOOK|DISCORD_WEBHOOK)\s*=\s*[""']?[^\s""'#]{8,}"), false),
        ("Private key", Rx(@"-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----"), false),
    };
    // Known, deliberate: the Season 1 chat (XMPP) certificate key that every player's own local backend uses.
    static readonly string[] SecretAllow = { "Syndicate-backend/src/Utils/syndicateLegacyXmpp.js" };
    static readonly string[] TextExt = { ".js", ".cjs", ".mjs", ".ts", ".json", ".txt", ".ini", ".cfg", ".conf", ".config", ".env", ".yml", ".yaml", ".md", ".html", ".htm", ".xml", ".ps1", ".bat", ".cmd", ".sh", ".py", ".pem", ".crt", ".log", ".csv", "" };
    static System.Text.RegularExpressions.Regex Rx(string p) => new System.Text.RegularExpressions.Regex(p, System.Text.RegularExpressions.RegexOptions.Compiled | System.Text.RegularExpressions.RegexOptions.CultureInvariant);

    internal static List<string> SecretScan(string root, IEnumerable<string> files)
    {
        var found = new List<string>();
        foreach (var rel in files)
        {
            string ext = Path.GetExtension(rel).ToLowerInvariant();
            if (Array.IndexOf(TextExt, ext) < 0) continue;
            string full = Path.Combine(root, rel.Replace('/', Path.DirectorySeparatorChar));
            var info = new FileInfo(full);
            if (!info.Exists || info.Length > 16 * 1024 * 1024) continue;
            bool node = rel.Split('/').Any(p => p.Equals("node_modules", StringComparison.OrdinalIgnoreCase));
            bool allowed = SecretAllow.Any(a => rel.EndsWith(a, StringComparison.OrdinalIgnoreCase));
            string text = File.ReadAllText(full);
            foreach (var (kind, rx, alsoNode) in SecretRules)
            {
                if (node && !alsoNode) continue;
                if (allowed && kind == "Private key") continue;
                if (rx.IsMatch(text)) found.Add(rel + ": " + kind);
            }
        }
        return found;
    }

    static void RequireFiles(string stage)
    {
        string[] required =
        {
            "Launcher/SyndicateLauncher.exe", "Launcher/SyndicateLauncher.dll",
            "Launcher/clrjit.dll", "Launcher/coreclr.dll", "Launcher/hostfxr.dll", "Launcher/hostpolicy.dll",
            "Dependencies/Node/node.exe",
            "Syndicate-backend/src/local/Storefront/rotation_pool_season5.json",
            "Erbium-Quality-of-life-changes-main/Erbium/x64/Release/Erbium.dll",
        };
        foreach (var rel in required)
            if (!File.Exists(Path.Combine(stage, rel.Replace('/', Path.DirectorySeparatorChar))))
                throw new FileNotFoundException("Required file missing from the release: " + rel);
    }

    // ---- package ---------------------------------------------------------

    static void WriteZip(string stage, List<string> files, string zipPath)
    {
        if (File.Exists(zipPath)) File.Delete(zipPath);
        using var zip = ZipFile.Open(zipPath, ZipArchiveMode.Create);
        var hashes = new StringBuilder();
        foreach (var rel in files)
        {
            zip.CreateEntryFromFile(Path.Combine(stage, rel.Replace('/', Path.DirectorySeparatorChar)), rel, CompressionLevel.Optimal);
            string hash = Convert.ToHexString(SHA256.HashData(
                File.ReadAllBytes(Path.Combine(stage, rel.Replace('/', Path.DirectorySeparatorChar))))).ToLowerInvariant();
            hashes.Append(hash).Append(' ').Append(rel).Append('\n');
        }
        // Integrity list inside the encrypted payload (so it cannot be forged on disk). On every start the
        // launcher re-hashes each file against this and restores any that differ, so only a dev-built
        // bundle's files are ever run. Added last, so the entry order of the real files is unchanged.
        var manifest = zip.CreateEntry("tilted-hashes", CompressionLevel.Optimal);
        using (var writer = new StreamWriter(manifest.Open())) writer.Write(hashes.ToString());
    }

    static void Encrypt(string zipPath, out byte[] key)
    {
        byte[] plain = File.ReadAllBytes(zipPath);
        key = RandomNumberGenerator.GetBytes(32);
        byte[] nonce = RandomNumberGenerator.GetBytes(12);
        byte[] cipher = new byte[plain.Length];
        byte[] tag = new byte[16];
        using (var aes = new AesGcm(key, 16)) aes.Encrypt(nonce, plain, cipher, tag);
        using (var outStream = File.Create(Path.Combine(_outDir, "payload.enc")))
        {
            outStream.Write(nonce, 0, nonce.Length);
            outStream.Write(tag, 0, tag.Length);
            outStream.Write(cipher, 0, cipher.Length);
        }
        WriteKeyFile(key);
    }

    // The key must live in the EXE to self-decrypt. Storing it XOR-masked and split
    // across two arrays keeps it out of a plain strings dump; it is not unbreakable.
    static void WriteKeyFile(byte[] key)
    {
        byte[] mask = RandomNumberGenerator.GetBytes(key.Length);
        byte[] masked = new byte[key.Length];
        for (int i = 0; i < key.Length; i++) masked[i] = (byte)(key[i] ^ mask[i]);
        var sb = new StringBuilder();
        sb.Append("internal static class PayloadKey {\n");
        sb.Append("  static readonly byte[] P = { ").Append(string.Join(",", masked)).Append(" };\n");
        sb.Append("  static readonly byte[] Q = { ").Append(string.Join(",", mask)).Append(" };\n");
        sb.Append("  internal static byte[] Bytes(){ var k=new byte[P.Length]; for(int i=0;i<k.Length;i++) k[i]=(byte)(P[i]^Q[i]); return k; }\n");
        sb.Append("}\n");
        File.WriteAllText(Path.Combine(_outDir, "PayloadKey.cs"), sb.ToString());
    }

    static void WriteManifest(string stage, List<string> files)
    {
        var sb = new StringBuilder();
        sb.Append("{\n  \"version\": \"").Append(_version).Append("\",\n  \"files\": [\n");
        for (int i = 0; i < files.Count; i++)
        {
            string hash = Convert.ToHexString(SHA256.HashData(
                File.ReadAllBytes(Path.Combine(stage, files[i].Replace('/', Path.DirectorySeparatorChar))))).ToLowerInvariant();
            sb.Append("    {\"path\": \"").Append(files[i]).Append("\", \"sha256\": \"").Append(hash).Append("\"}");
            sb.Append(i < files.Count - 1 ? ",\n" : "\n");
        }
        sb.Append("  ]\n}\n");
        File.WriteAllText(Path.Combine(_outDir, "component-manifest.json"), sb.ToString());
    }

    static void TryDelete(string dir) { try { if (Directory.Exists(dir)) Directory.Delete(dir, true); } catch { } }
}
