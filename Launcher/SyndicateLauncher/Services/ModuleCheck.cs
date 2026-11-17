using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection.PortableExecutable;
using System.Runtime.InteropServices;
using System.Text;

namespace SyndicateLauncher.Services;

/// <summary>
/// Finds out, without running any of its code, why a game DLL would not load: Windows Security blocking it,
/// a missing or damaged file, or a DLL it needs that isn't on this PC. Used before Fortnite starts (so a
/// blocked file is reported in seconds, not after the game has loaded) and again when a load fails.
/// </summary>
internal static class ModuleCheck
{
    private const uint DontResolveDllReferences = 0x00000001;
    private const uint LoadLibraryAsDatafile = 0x00000002;
    private const uint LoadLibrarySearchSystem32 = 0x00000800;
    private const int ErrorFileNotFound = 2, ErrorPathNotFound = 3, ErrorAccessDenied = 5, ErrorBadExeFormat = 193,
        ErrorVirusInfected = 225, ErrorVirusDeleted = 226, ErrorInvalidImageHash = 577;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibraryExW(string file, IntPtr reserved, uint flags);
    [DllImport("kernel32.dll")] private static extern bool FreeLibrary(IntPtr module);

    /// <summary>Null when Windows would map the DLL and every DLL it imports exists.</summary>
    public static LaunchProblem? Diagnose(string file)
    {
        string name = Path.GetFileName(file);
        if (!File.Exists(file)) return LaunchProblem.ModuleMissing(name);

        // Opening the file is where Windows Security steps in (ERROR_VIRUS_INFECTED).
        try { using var _ = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete); }
        catch (IOException ex) when (IsBlocked(ex.HResult & 0xFFFF)) { return LaunchProblem.ModuleBlocked(name); }
        catch (UnauthorizedAccessException) { return LaunchProblem.ModuleBlocked(name); }
        catch (IOException) { return LaunchProblem.ModuleMissing(name); }

        // Map it as an image without running DllMain or resolving imports: this is where Windows checks the
        // image itself (and where an on-access scan blocks a flagged DLL).
        var mapped = LoadLibraryExW(file, IntPtr.Zero, DontResolveDllReferences);
        if (mapped == IntPtr.Zero)
        {
            int error = Marshal.GetLastWin32Error();
            if (IsBlocked(error)) return LaunchProblem.ModuleBlocked(name);
            if (error is ErrorFileNotFound or ErrorPathNotFound) return LaunchProblem.ModuleMissing(name);
            if (error == ErrorBadExeFormat) return LaunchProblem.ModuleDamaged(name);
            return null; // Unknown: let the real load report it.
        }
        FreeLibrary(mapped);

        foreach (var dependency in Imports(file))
        {
            if (dependency.StartsWith("api-ms-", StringComparison.OrdinalIgnoreCase) || dependency.StartsWith("ext-ms-", StringComparison.OrdinalIgnoreCase))
                continue;
            if (File.Exists(Path.Combine(Path.GetDirectoryName(file) ?? "", dependency))) continue;
            var found = LoadLibraryExW(dependency, IntPtr.Zero, LoadLibraryAsDatafile | LoadLibrarySearchSystem32);
            if (found != IntPtr.Zero) { FreeLibrary(found); continue; }
            return LaunchProblem.ModuleNeedsDll(name, dependency);
        }
        return null;
    }

    private static bool IsBlocked(int error) =>
        error is ErrorVirusInfected or ErrorVirusDeleted or ErrorAccessDenied or ErrorInvalidImageHash;

    /// <summary>Names of the DLLs in the import table (no code is run).</summary>
    private static List<string> Imports(string file)
    {
        var names = new List<string>();
        try
        {
            var image = File.ReadAllBytes(file);
            using var pe = new PEReader(new MemoryStream(image));
            var headers = pe.PEHeaders;
            var directory = headers.PEHeader?.ImportTableDirectory ?? default;
            if (directory.RelativeVirtualAddress == 0) return names;
            int ToOffset(int rva)
            {
                foreach (var section in headers.SectionHeaders)
                    if (rva >= section.VirtualAddress && rva < section.VirtualAddress + Math.Max(section.VirtualSize, section.SizeOfRawData))
                        return rva - section.VirtualAddress + section.PointerToRawData;
                return -1;
            }
            int at = ToOffset(directory.RelativeVirtualAddress);
            for (int i = 0; at >= 0 && i < 512; i++, at += 20)
            {
                if (at + 20 > image.Length) break;
                int nameRva = BitConverter.ToInt32(image.AsSpan(at + 12, 4));
                if (nameRva == 0) break;
                int nameAt = ToOffset(nameRva);
                if (nameAt < 0) continue;
                int end = nameAt;
                while (end < image.Length && image[end] != 0 && end - nameAt < 260) end++;
                names.Add(Encoding.ASCII.GetString(image, nameAt, end - nameAt));
            }
        }
        catch (BadImageFormatException) { }
        catch (IOException) { }
        return names;
    }
}
