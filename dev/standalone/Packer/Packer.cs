// Syndicate standalone packer (.NET).
// Takes an already-assembled, already-cleaned Syndicate folder (the "Syndicate" folder that
// make-public-release.ps1 produces), zips it with a SHA-256 hash manifest inside, and writes an
// AES-256-GCM encrypted payload in the exact format the bootstrapper (Bootstrap.cs) expects:
//     payload.enc = nonce(12) + tag(16) + ciphertext
// It also writes PayloadKey.cs (the XOR-masked key) which is compiled into the bootstrapper so the exe
// can self-decrypt. The key file is a build artifact and is never shipped on its own.
//
// Usage: Packer --install <Syndicate folder> --out <build dir> --version <x.y.z>
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

static class Packer
{
    static string _install, _outDir, _version;

    const string ManifestName = "syndicate-hashes";

    static int Main(string[] args)
    {
        try
        {
            for (int i = 0; i < args.Length - 1; i++)
            {
                if (args[i] == "--install") _install = Path.GetFullPath(args[++i]);
                else if (args[i] == "--out") _outDir = Path.GetFullPath(args[++i]);
                else if (args[i] == "--version") _version = args[++i];
            }
            if (_install == null || _outDir == null || string.IsNullOrWhiteSpace(_version))
                throw new ArgumentException("Usage: --install <Syndicate folder> --out <build dir> --version <x.y.z>");
            if (!File.Exists(Path.Combine(_install, "SyndicateLauncher.exe")))
                throw new FileNotFoundException("That is not an assembled Syndicate folder (no SyndicateLauncher.exe): " + _install);
            Directory.CreateDirectory(_outDir);

            var files = Directory.EnumerateFiles(_install, "*", SearchOption.AllDirectories)
                .Select(f => Rel(_install, f)).OrderBy(r => r, StringComparer.Ordinal).ToList();
            if (files.Count == 0) throw new Exception("Nothing to pack.");

            // Safety net: the folder is already cleaned by make-public-release, but never ship a leaked secret.
            var secrets = SecretScan(_install, files);
            if (secrets.Count > 0)
                throw new Exception("Secret scan found secret(s) in files that would ship (nothing was built):\n  " + string.Join("\n  ", secrets));

            RequireFiles(_install);
            Console.WriteLine($"Packing {files.Count} files from {_install}");

            string zip = Path.Combine(_outDir, "payload.zip");
            WriteZip(_install, files, zip);
            Encrypt(zip);
            File.Delete(zip);
            Console.WriteLine($"PASS: {files.Count} files encrypted with AES-256-GCM into {Path.Combine(_outDir, "payload.enc")}.");
            return 0;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine("PACK FAILED: " + e.Message);
            return 1;
        }
    }

    static string Rel(string root, string full) => Path.GetRelativePath(root, full).Replace('\\', '/');

    static void RequireFiles(string stage)
    {
        string[] required =
        {
            "SyndicateLauncher.exe",
            "Runtime/dlls/manifest.json",
            "Runtime/dlls/Erbium.dll",
            "Runtime/dlls/cobalt.dll",
            "Runtime/dlls/Syndicate1040.dll",
            "Runtime/backend/lawinserver.exe",
            "Dependencies/Node/node.exe",
        };
        foreach (var rel in required)
            if (!File.Exists(Path.Combine(stage, rel.Replace('/', Path.DirectorySeparatorChar))))
                throw new FileNotFoundException("Required file missing from the Syndicate folder: " + rel);
    }

    // ---- package ---------------------------------------------------------

    static void WriteZip(string stage, List<string> files, string zipPath)
    {
        if (File.Exists(zipPath)) File.Delete(zipPath);
        using var zip = ZipFile.Open(zipPath, ZipArchiveMode.Create);
        var hashes = new StringBuilder();
        foreach (var rel in files)
        {
            var full = Path.Combine(stage, rel.Replace('/', Path.DirectorySeparatorChar));
            zip.CreateEntryFromFile(full, rel, CompressionLevel.Optimal);
            string hash = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(full))).ToLowerInvariant();
            hashes.Append(hash).Append(' ').Append(rel).Append('\n');
        }
        // Integrity list INSIDE the encrypted payload (cannot be forged on disk). On every start the
        // bootstrapper re-hashes each file against this and restores any that differ or were removed
        // (e.g. by antivirus). Added last so the entry order of the real files is unchanged.
        var manifest = zip.CreateEntry(ManifestName, CompressionLevel.Optimal);
        using var writer = new StreamWriter(manifest.Open());
        writer.Write(hashes.ToString());
    }

    static void Encrypt(string zipPath)
    {
        byte[] plain = File.ReadAllBytes(zipPath);
        byte[] key = RandomNumberGenerator.GetBytes(32);
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

    // The key must live in the EXE to self-decrypt. Storing it XOR-masked and split across two arrays
    // keeps it out of a plain strings dump; it is not unbreakable. Bootstrap.cs reads it via PayloadKey.Bytes().
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

    // ---- secret scan (safety net) ---------------------------------------
    static readonly (string Kind, System.Text.RegularExpressions.Regex Rx, bool AlsoNodeModules)[] SecretRules =
    {
        ("Discord webhook", Rx(@"discord(?:app)?\.com/api/webhooks/\d{17,20}/[A-Za-z0-9_-]{60,}"), true),
        ("Discord bot token", Rx(@"\b[MNO][A-Za-z0-9_-]{23,27}\.[A-Za-z0-9_-]{6}\.[A-Za-z0-9_-]{27,40}\b"), true),
        ("GitHub token", Rx(@"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36}\b|\bgithub_pat_[A-Za-z0-9_]{50,}"), false),
        ("AWS key", Rx(@"\bAKIA[0-9A-Z]{16}\b"), false),
        ("Database password", Rx(@"mongodb(?:\+srv)?://[^/\s:@""'$`{}]+:[^@\s""'$`{}]+@"), false),
        ("Private key", Rx(@"-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----"), false),
    };
    // Known, deliberate secrets that are MEANT to ship:
    //  - the Season 1 chat (XMPP) certificate key every player's own local backend uses, and
    //  - error-webhook.txt, the error-reporting Discord webhook players' launchers post crash reports to.
    static readonly string[] SecretAllow =
    {
        "src/Utils/syndicateLegacyXmpp.js",
        "src/Utils/tiltedLegacyXmpp.js",
        "error-webhook.txt",
    };
    static readonly string[] TextExt = { ".js", ".cjs", ".mjs", ".ts", ".json", ".txt", ".ini", ".cfg", ".conf", ".config", ".env", ".yml", ".yaml", ".md", ".html", ".htm", ".xml", ".ps1", ".bat", ".cmd", ".sh", ".py", ".pem", ".crt", ".log", ".csv", "" };
    static System.Text.RegularExpressions.Regex Rx(string p) => new System.Text.RegularExpressions.Regex(p, System.Text.RegularExpressions.RegexOptions.Compiled | System.Text.RegularExpressions.RegexOptions.CultureInvariant);

    static List<string> SecretScan(string root, IEnumerable<string> files)
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
            if (allowed) continue; // explicitly trusted file whose "secret" is deliberate and meant to ship
            string text = File.ReadAllText(full);
            foreach (var (kind, rx, alsoNode) in SecretRules)
            {
                if (node && !alsoNode) continue;
                if (rx.IsMatch(text)) found.Add(rel + ": " + kind);
            }
        }
        return found;
    }
}
