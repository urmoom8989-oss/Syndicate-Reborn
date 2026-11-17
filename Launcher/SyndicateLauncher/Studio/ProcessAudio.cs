using System;
using System.Runtime.InteropServices;

namespace SyndicateLauncher.Studio
{
    // Mute/unmute one process's audio session (the embedded Fortnite in the Level Editor) via Windows Core Audio.
    // Best-effort: any COM failure returns false instead of throwing. Uncalled interface methods are declared as
    // zero-arg placeholders purely to hold their vtable slot, so the ones we DO call land on the right slot.
    internal static class ProcessAudio
    {
        public static bool SetMuted(int pid, bool mute)
        {
            object? enumObj = null;
            try
            {
                var t = Type.GetTypeFromCLSID(new Guid("BCDE0395-E52F-467C-8E3D-C4579291692E"));
                if (t == null) return false;
                enumObj = Activator.CreateInstance(t);
                var devEnum = (IMMDeviceEnumerator)enumObj!;
                if (devEnum.GetDefaultAudioEndpoint(0, 0, out var device) != 0 || device == null) return false;
                var iid = typeof(IAudioSessionManager2).GUID;
                if (device.Activate(ref iid, 1, IntPtr.Zero, out var mgrObj) != 0 || mgrObj == null) return false;
                var mgr = (IAudioSessionManager2)mgrObj;
                if (mgr.GetSessionEnumerator(out var sessions) != 0 || sessions == null) return false;
                sessions.GetCount(out int count);
                bool any = false;
                for (int i = 0; i < count; i++)
                {
                    if (sessions.GetSession(i, out var sObj) != 0 || sObj == null) continue;
                    try
                    {
                        if (sObj is IAudioSessionControl2 c2 && c2.GetProcessId(out int spid) == 0 && spid == pid && sObj is ISimpleAudioVolume vol)
                        {
                            var g = Guid.Empty; vol.SetMute(mute, ref g); any = true;
                        }
                    }
                    finally { Marshal.ReleaseComObject(sObj); }
                }
                return any;
            }
            catch { return false; }
            finally { if (enumObj != null) Marshal.ReleaseComObject(enumObj); }
        }

        [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IMMDeviceEnumerator
        {
            [PreserveSig] int EnumAudioEndpoints();
            [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice dev);
        }
        [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IMMDevice
        {
            [PreserveSig] int Activate(ref Guid iid, int clsCtx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
        }
        [ComImport, Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IAudioSessionManager2
        {
            [PreserveSig] int GetAudioSessionControl();
            [PreserveSig] int GetSimpleAudioVolume();
            [PreserveSig] int GetSessionEnumerator(out IAudioSessionEnumerator e);
        }
        [ComImport, Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IAudioSessionEnumerator
        {
            [PreserveSig] int GetCount(out int count);
            [PreserveSig] int GetSession(int index, [MarshalAs(UnmanagedType.IUnknown)] out object session);
        }
        [ComImport, Guid("BFB7FF88-7239-4FC9-8FA2-07C950BE9C6D"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IAudioSessionControl2
        {
            [PreserveSig] int GetState();
            [PreserveSig] int GetDisplayName();
            [PreserveSig] int SetDisplayName();
            [PreserveSig] int GetIconPath();
            [PreserveSig] int SetIconPath();
            [PreserveSig] int GetGroupingParam();
            [PreserveSig] int SetGroupingParam();
            [PreserveSig] int RegisterAudioSessionNotification();
            [PreserveSig] int UnregisterAudioSessionNotification();
            [PreserveSig] int GetSessionIdentifier();
            [PreserveSig] int GetSessionInstanceIdentifier();
            [PreserveSig] int GetProcessId(out int pid);
        }
        [ComImport, Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface ISimpleAudioVolume
        {
            [PreserveSig] int SetMasterVolume();
            [PreserveSig] int GetMasterVolume();
            [PreserveSig] int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid ctx);
        }
    }
}
