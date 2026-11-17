// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.IO;
using System.Security.Cryptography;
namespace SyndicateClassic;
internal static class ConsoleCompatibility
{
    // The console hook is verified only against this stock 4.00 executable.
    // Directory names and empty PE version resources cannot establish compatibility.
    internal static bool Supports(string executable)
    {
        try { using var input=File.OpenRead(executable);return Convert.ToHexString(SHA256.HashData(input)).Equals("aa9ed44215cceb465d94ec9a74e27a8122b344c12a2e727168aa11d5c1195049",StringComparison.OrdinalIgnoreCase); }
        catch(IOException){return false;}
        catch(UnauthorizedAccessException){return false;}
    }
}