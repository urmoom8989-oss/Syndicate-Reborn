// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.IO;
namespace SyndicateClassic;

// Pre-season (1.7.2), Season 1 (1.8.x) and Season 2 (1.11) only send their chat/party login over TLS, and they check
// the server certificate against a root list built into the game. Syndicate's backend uses its own
// certificate, so the game refused it ("unknown ca") and ready-up waited forever.
// This swaps one expired built-in root (UTN-USERFirst-Hardware, expired 2019) for Syndicate's
// loopback certificate of the exact same size. A copy of the untouched game is kept next to it.
internal static class LegacyXmppTrust
{
    private const string ExpiredRoot = "MIIEdDCCA1ygAwIBAgIQRL4Mi1AAJLQR0zYq/mUK/TANBgkqhkiG9w0BAQUFADCBlzELMAkGA1UEBhMCVVMxCzAJBgNVBAgTAlVUMRcwFQYDVQQHEw5TYWx0IExha2UgQ2l0eTEeMBwGA1UEChMVVGhlIFVTRVJUUlVTVCBOZXR3b3JrMSEwHwYDVQQLExhodHRwOi8vd3d3LnVzZXJ0cnVzdC5jb20xHzAdBgNVBAMTFlVUTi1VU0VSRmlyc3QtSGFyZHdhcmUwHhcNOTkwNzA5MTgxMDQyWhcNMTkwNzA5MTgxOTIyWjCBlzELMAkGA1UEBhMCVVMxCzAJBgNVBAgTAlVUMRcwFQYDVQQHEw5TYWx0IExha2UgQ2l0eTEeMBwGA1UEChMVVGhlIFVTRVJUUlVTVCBOZXR3b3JrMSEwHwYDVQQLExhodHRwOi8vd3d3LnVzZXJ0cnVzdC5jb20xHzAdBgNVBAMTFlVUTi1VU0VSRmlyc3QtSGFyZHdhcmUwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCx98M4P7Sof885glFn0G2f0v9Y8+efK+wNiVSZuTiZFvfgIXlIwrthdBKWHTxqctU8EGc6Oe0rE81m65UJM6Rsl7HoxuzBdXmcRl6Nq9Bq/bkqVRcQVLMZ8Jr28bFdtqdt++BxF2uiiPsA3/4aMXcMmgF6sTLjKwEHOG7DpV4jvEWbe1DByTCP2+UretNb+zNAHqDVmBe8i4fDidNdoI6yqqr2jmmIBsX6iSHzCJ1pLgkzmykNRg+MzEk0sGlRvfkGzWitZky8PqxhvQqIDsjfPe58BEydCl5rkdbux+0ojatNh4lz0G6k0B4WixThdkQDf2Os5M1JnMWS9KsyoUhbAgMBAAGjgbkwgbYwCwYDVR0PBAQDAgHGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFKFyXyYbKJhDlV0HN9WFlp1L0sNFMEQGA1UdHwQ9MDswOaA3oDWGM2h0dHA6Ly9jcmwudXNlcnRydXN0LmNvbS9VVE4tVVNFUkZpcnN0LUhhcmR3YXJlLmNybDAxBgNVHSUEKjAoBggrBgEFBQcDAQYIKwYBBQUHAwUGCCsGAQUFBwMGBggrBgEFBQcDBzANBgkqhkiG9w0BAQUFAAOCAQEARxkP3nTGmZev/K0oXnWO6y1n7k57K9cM//bey1WiCuFMVGWTYGufEpytXoMs61quwOQt9ABjHbjAbPLPSbtNk28GpgoiskliCE7/yMgUsogWXecB5BKV5UU0s4tpvc+0hY91UZ59Ojg6FEgSxvunOxqNDYJAB+gECJChicsZUN/KHAG8HQQZexB2lzvukJDKxA4fFm517zP4029bHpbj4HR3dHuKom4t3XbWOTCC8KucUvIqx69JXn7HaOWCgchqJ/kniCrVWFCVH/A7HFe7fRQ5YiuayZSSKqMiDP+JJn1fIytH1xUdqWqeUQ0qUZ6B+dQ7XnASfxAynB67nfhmqA==";
    // Rotated 2026-10-31: a fresh key + certificate (the old key had leaked). Both are loopback-only and self-signed.
    private const string SyndicateCert = "MIIEdDCCA1ygAwIBAgIBKjANBgkqhkiG9w0BAQsFADCBszELMAkGA1UEBhMCVVMxFzAVBgNVBAoMDlByb2plY3QgVGlsdGVkMUMwQQYDVQQLDDpUaWx0ZWQgbG9jYWwgWE1QUCB4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4MSYwJAYDVQQLDB1Mb29wYmFjayBvbmx5IHl5eXl5eXl5eXl5eXl5eTEeMBwGA1UEAwwVcHJvZC5vbC5lcGljZ2FtZXMuY29tMB4XDTI2MTAzMTAwMDAwMFoXDTM2MTAyODAwMDAwMFowgbMxCzAJBgNVBAYTAlVTMRcwFQYDVQQKDA5Qcm9qZWN0IFRpbHRlZDFDMEEGA1UECww6VGlsdGVkIGxvY2FsIFhNUFAgeHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eDEmMCQGA1UECwwdTG9vcGJhY2sgb25seSB5eXl5eXl5eXl5eXl5eXkxHjAcBgNVBAMMFXByb2Qub2wuZXBpY2dhbWVzLmNvbTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALPdliKtZiJ6xxCEaUrU0rUvLsjspL/6lgAwfgGQIggogb1EtS7AvQF0lf3U8kSLaEyTppynhbGEH9bHqSp9DxDSbENB1l9bpfhpDiyUa/lhwaN5EcvwxdCKYQbEOrHzpx6Th05H5daIU34sKelIuPqflpZQnwhyLEMpgnIdxvNJoXgUrqAQNqJuRrXpa8DVXjXAJepQhP5uWISkPTkBZpC7oChdCF/bDVuPRHJwE2pFn00ASyx8rYmalVw3eWZzUTr/NUQdsXd8JF2kzXSALjVjwGZeF7yzBXkzhoco3/d00XMkI0yRs9ziSHYhTti91Gx6SrWxZNJ05Bl0hC+kIAECAwEAAaOBkDCBjTAdBgNVHQ4EFgQUJBVk30IL4+VpSl09DGSDSawrZ00wHwYDVR0jBBgwFoAUJBVk30IL4+VpSl09DGSDSawrZ00wOgYDVR0RBDMwMYIVcHJvZC5vbC5lcGljZ2FtZXMuY29tghIqLm9sLmVwaWNnYW1lcy5jb22HBH8AAAEwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAYZByC/GmnVwYkbww74RL6/IIofhEwkyunavqik6CSTqTyLkZH40WLan1hZUaG89louISrOGdqZnSaZCUr9UWADssLteMkK+J7mb5GOtKTs4Hccz78CXBQIPdx/c9GJ4c/3zFE4txrqZ6axp4lZH6ibC6dfvtaWhnh1LovIHVq1RIDnqhambEiNpjbP+Zsn7s/rs/xBnkxH/nGXig7ef79txYkLukJMR9lMLHOMQn1hT3IAtdtd3OgzSNfNEUdL9AOjqQY9kqG2kM7udRUhcJjIIV11m/rTvptrgwdmLPs9Z93SnFzkDScDYR536OBEoHuQYEkLKct8Kojq/yzgNegg==";
    // The previous Syndicate certificate. A game already patched by an earlier launcher carries this instead of the
    // expired root, so it is swapped for the new one too — otherwise it would still trust the old (rotated-out) cert.
    private const string OldSyndicateCert = "MIIEdDCCA1ygAwIBAgIBKjANBgkqhkiG9w0BAQsFADCBszELMAkGA1UEBhMCVVMxFzAVBgNVBAoMDlByb2plY3QgVGlsdGVkMUMwQQYDVQQLDDpUaWx0ZWQgbG9jYWwgWE1QUCB4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4MSYwJAYDVQQLDB1Mb29wYmFjayBvbmx5IHl5eXl5eXl5eXl5eXl5eTEeMBwGA1UEAwwVcHJvZC5vbC5lcGljZ2FtZXMuY29tMB4XDTI2MTAyNjAyMjY0NFoXDTM2MTAyMzAyMjY0NFowgbMxCzAJBgNVBAYTAlVTMRcwFQYDVQQKDA5Qcm9qZWN0IFRpbHRlZDFDMEEGA1UECww6VGlsdGVkIGxvY2FsIFhNUFAgeHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eDEmMCQGA1UECwwdTG9vcGJhY2sgb25seSB5eXl5eXl5eXl5eXl5eXkxHjAcBgNVBAMMFXByb2Qub2wuZXBpY2dhbWVzLmNvbTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALKNp7KDMcVbhob3RKIA+fOJkVkPT00P0wjDA/proizZzZD+XrSi8lfTWc4hVoNSmDCjEtp+/j6xFyJX09jmNuZTbsQNAv7Rh+qJezOHG3TCEjyvId/ju62NJG6Uzl4NoavAGjYgLdJ6ospD9fh+F99jcTw+AMQrMw+XBs9bnTbFjgnWuxwQL65XASWdngOjdRqR0rvdaP8+BXM2A9yIMDRkZI2HXNJnjk/zmP2BZ7BgPdB1aYEPchgeJRlnZVI4HFiqozmZVbM2zU1y507PFbWT6PR4AnkxuDiozjuYr7W3h0nB0IwA8fot4d8PuaQpsTx+t607aHtA1ZQQwo6KG+UCAwEAAaOBkDCBjTAdBgNVHQ4EFgQUkY+Do4REg4PDQNMk56blaQx5np4wHwYDVR0jBBgwFoAUkY+Do4REg4PDQNMk56blaQx5np4wOgYDVR0RBDMwMYIVcHJvZC5vbC5lcGljZ2FtZXMuY29tghIqLm9sLmVwaWNnYW1lcy5jb22HBH8AAAEwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEANUNAQSlMkrynag+dW5zzfcYHXwWGItLrwhgHnfEanKXxmX3WwfQkEMU9v8ySTg7nwO0egp0gwN9H1wRRh2FaDulvB6hQ53FoobeJiHFwvFvxu3eEz4aCxYO01IGd3Wnpok9krSsr226imvU7JG3i5sLG8n0PbvYY95IbW4WGQGolL4tCuYxCWZtU9ekDrJUX4hJw2dskPpb7RWogslVR3oPU8cQK1sihsXOFN95eRYykxaLghxs7pSZ3lpV/DRaBu2PnhW8CV7WETAM1aK0OORLUJhLxeE34Yv427fynoy3XXtH4Fa4UP4ZkPdBkkQ0l0XXrxq13H6OQaseZZeEgmA==";

    internal static void Ensure(string gameExe, string? build, Action<string> log)
    {
        if (build is not ("1.7.2" or "1.8" or "1.8.2" or "1.11")) return;
        var replacement = Convert.FromBase64String(SyndicateCert);
        var data = File.ReadAllBytes(gameExe);
        // Already on the current certificate: nothing to do.
        if (data.AsSpan().IndexOf((ReadOnlySpan<byte>)replacement) >= 0) return;
        // Swap the expired built-in root (never patched) or an earlier Syndicate certificate (patched before a rotation).
        foreach (var oldB64 in new[] { ExpiredRoot, OldSyndicateCert })
        {
            var original = Convert.FromBase64String(oldB64);
            if (original.Length != replacement.Length) continue;
            int at = data.AsSpan().IndexOf((ReadOnlySpan<byte>)original);
            if (at < 0) continue;
            var backup = gameExe + ".tilted-original";
            if (!File.Exists(backup)) File.Copy(gameExe, backup);
            var attributes = File.GetAttributes(gameExe);
            if (attributes.HasFlag(FileAttributes.ReadOnly)) File.SetAttributes(gameExe, attributes & ~FileAttributes.ReadOnly);
            using (var stream = new FileStream(gameExe, FileMode.Open, FileAccess.Write, FileShare.Read))
            {
                stream.Position = at;
                stream.Write(replacement, 0, replacement.Length);
            }
            log((build == "1.11" ? "Season 2" : "Season 1") + " chat: installed Syndicate's certificate in the game (original kept as " + Path.GetFileName(backup) + ").");
            return;
        }
        log((build == "1.11" ? "Season 2" : "Season 1") + " chat: this game has no matching built-in certificate, so friends, invites and party/ready-up may not work.");
    }
}