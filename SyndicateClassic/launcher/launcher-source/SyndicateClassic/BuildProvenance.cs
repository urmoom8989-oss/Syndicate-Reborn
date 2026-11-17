// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace SyndicateClassic;

// Build tamper-detection. A public release is signed at build time (public-release\Packer, with a private key
// that is kept off GitHub and never ships) over the launcher DLL's SHA-256. Here the launcher verifies that
// signature with the built-in public key and confirms its own DLL still matches the one that was signed.
//
// This does NOT stop decompilation — nothing can, since the code runs on the user's PC. What it does is detect a
// modified or rebranded build (someone who recompiles cannot produce a valid signature without the private key),
// so the launcher can show "Unverified build" and you have cryptographic proof of your official releases for a
// takedown. It never blocks a genuine user: any problem reading or verifying leaves the state Unknown and the
// launcher runs exactly as before.
internal static class BuildProvenance
{
    internal enum Result { Unknown, Official, Tampered }
    internal static Result State { get; private set; } = Result.Unknown;

    // Syndicate's build-signing public key. The matching private key lives only on the release machine.
    private const string PublicKeyPem =
        "-----BEGIN PUBLIC KEY-----\n" +
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAzxVIOWjexFPvCTfG+IHb\n" +
        "+v4LYZsGb1KRdrU692/yhH2aKrjG1tIy5XGs3yfG8dJZtyXPbY2/ZPF3gTcWu/pn\n" +
        "8no8gQ9qfi1iLEWu9bBOoTiw5itL43ZzB77L6RjrCRXFeIE2n6agGYMbt90W8Y4C\n" +
        "vwx29M426od3fBv853LBm34WBPCZzDShHxSczsnhQ8PrBf/x5QoC+UfQx/iCbqmw\n" +
        "rr/1WG/XGSfF4elQ+KZPmL92qzjjv1chxo0gUAuQjDzd2kK85O412oT9fpfePAyW\n" +
        "ziVnTKXdQ8e9vML3QTlcswc8Tzaynis3XKNp47G/mw55jb7VRvJKTf9iAa7bp302\n" +
        "uQIDAQAB\n" +
        "-----END PUBLIC KEY-----";

    // Run once at startup. Never throws.
    internal static void Check()
    {
        try
        {
            var dllPath = Assembly.GetExecutingAssembly().Location;
            if (string.IsNullOrEmpty(dllPath) || !File.Exists(dllPath)) return; // loaded from memory: cannot self-check
            var provPath = Path.Combine(Path.GetDirectoryName(dllPath)!, "build-provenance.json");
            if (!File.Exists(provPath)) return; // unsigned (e.g. a Dev Kit or local build): stay Unknown, no banner

            var json = File.ReadAllText(provPath);
            string? version = JsonStr(json, "v"), dllHash = JsonStr(json, "dll"), sigB64 = JsonStr(json, "sig");
            if (version == null || dllHash == null || sigB64 == null) { State = Result.Tampered; return; }

            // 1) The signature must be a genuine Syndicate signature over exactly this payload.
            string payload = "tilted-build-v1|" + version + "|" + dllHash;
            using var rsa = RSA.Create();
            rsa.ImportFromPem(PublicKeyPem);
            bool sigOk = rsa.VerifyData(Encoding.UTF8.GetBytes(payload), Convert.FromBase64String(sigB64),
                HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
            if (!sigOk) { State = Result.Tampered; return; }

            // 2) The signature is real; confirm the running DLL is the exact one it covers.
            string actual;
            using (var sha = SHA256.Create()) actual = Convert.ToHexString(sha.ComputeHash(File.ReadAllBytes(dllPath))).ToLowerInvariant();
            State = string.Equals(actual, dllHash, StringComparison.OrdinalIgnoreCase) ? Result.Official : Result.Tampered;
        }
        catch { State = Result.Unknown; } // verification must never break startup
    }

    // Tiny reader for our own {"v":"..","dll":"..","sig":".."}; avoids pulling in a JSON dependency this early.
    private static string? JsonStr(string json, string key)
    {
        var m = Regex.Match(json, "\"" + Regex.Escape(key) + "\"\\s*:\\s*\"([^\"]*)\"");
        return m.Success ? m.Groups[1].Value : null;
    }
}
