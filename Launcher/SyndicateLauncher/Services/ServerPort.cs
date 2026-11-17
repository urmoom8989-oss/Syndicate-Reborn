using System;
using System.Runtime.InteropServices;
namespace SyndicateLauncher.Services;

public static class ServerPort
{
    [DllImport("iphlpapi.dll")]
    private static extern uint GetExtendedUdpTable(IntPtr table, ref uint size, bool sorted, int family, int tableClass, uint reserved);

    public static bool IsOwnedBy(int processId, int port)
    {
        uint size = 0;
        GetExtendedUdpTable(IntPtr.Zero, ref size, false, 2, 1, 0);
        if (size < 4 || size > 16 * 1024 * 1024) return false;
        var table = Marshal.AllocHGlobal((int)size);
        try
        {
            if (GetExtendedUdpTable(table, ref size, false, 2, 1, 0) != 0) return false;
            int rows = Marshal.ReadInt32(table);
            if (rows < 0 || 4L + rows * 12L > size) return false;
            for (int i = 0; i < rows; i++)
            {
                int row = 4 + i * 12;
                int encoded = Marshal.ReadInt32(table, row + 4);
                int boundPort = ((encoded & 255) << 8) | ((encoded >> 8) & 255);
                if (boundPort == port && Marshal.ReadInt32(table, row + 8) == processId) return true;
            }
            return false;
        }
        finally { Marshal.FreeHGlobal(table); }
    }
}
