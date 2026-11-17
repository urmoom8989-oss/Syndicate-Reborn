// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace SyndicateClassic;

internal static class NativeMethods
{
	[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
	private struct StartupInfo
	{
		public int cb;

		public string? reserved;

		public string? desktop;

		public string? title;

		public uint x;

		public uint y;

		public uint xSize;

		public uint ySize;

		public uint xCount;

		public uint yCount;

		public uint fill;

		public uint flags;

		public ushort show;

		public ushort reservedBytes;

		public nint reserved2;

		public nint input;

		public nint output;

		public nint error;
	}

	private struct ProcessInformation
	{
		public nint process;

		public nint thread;

		public uint pid;

		public uint tid;
	}

	private delegate bool EnumWindowsProc(nint hWnd, nint lParam);

	private const uint PROCESS_ALL_ACCESS = 2035711u;

	private const uint MEM_COMMIT = 4096u;

	private const uint MEM_RELEASE = 32768u;

	private const uint PAGE_READWRITE = 4u;

	[DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
	private static extern nint CreateDesktop(string name, nint device, nint mode, uint flags, uint access, nint security);

	[DllImport("user32.dll", SetLastError = true)]
	private static extern bool CloseDesktop(nint desktop);

	[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
	private static extern bool CreateProcess(string application, StringBuilder command, nint processSecurity, nint threadSecurity, bool inherit, uint flags, nint environment, string directory, ref StartupInfo startup, out ProcessInformation process);

	public static void ConfigureSyndicateCompatibility(ProcessStartInfo info)
	{
		info.Environment["OPENSSL_ia32cap"] = ":~0x20000000";
	}

	public static Process StartHiddenHost(ProcessStartInfo info)
	{
		ConfigureSyndicateCompatibility(info);
		string text = "TiltedHost_" + Guid.NewGuid().ToString("N");
		nint num = CreateDesktop(text, IntPtr.Zero, IntPtr.Zero, 0u, 983551u, IntPtr.Zero);
		if (num == IntPtr.Zero)
		{
			throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not create the background host desktop.");
		}
		nint num2 = Marshal.StringToHGlobalUni(string.Join("\0", from p in info.Environment.OrderBy<KeyValuePair<string, string>, string>((KeyValuePair<string, string> p) => p.Key, StringComparer.OrdinalIgnoreCase)
			select p.Key + "=" + p.Value) + "\0\0");
		try
		{
			StartupInfo startup = new StartupInfo
			{
				cb = Marshal.SizeOf<StartupInfo>(),
				desktop = text,
				flags = 1u,
				show = 0
			};
			StringBuilder command = new StringBuilder("\"" + info.FileName + "\" " + info.Arguments);
			if (!CreateProcess(info.FileName, command, IntPtr.Zero, IntPtr.Zero, inherit: false, 134218752u, num2, info.WorkingDirectory, ref startup, out var process))
			{
				throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not start the background host.");
			}
			try
			{
				Process process2 = Process.GetProcessById((int)process.pid);
				nint ownedDesktop = num;
				num = IntPtr.Zero;
				Task.Run(delegate
				{
					try
					{
						process2.WaitForExit();
					}
					finally
					{
						CloseDesktop(ownedDesktop);
					}
				});
				return process2;
			}
			finally
			{
				CloseHandle(process.thread);
				CloseHandle(process.process);
			}
		}
		finally
		{
			Marshal.FreeHGlobal(num2);
			if (num != IntPtr.Zero)
			{
				CloseDesktop(num);
			}
		}
	}

	[DllImport("kernel32.dll", SetLastError = true)]
	private static extern nint OpenProcess(uint dwDesiredAccess, bool bInheritHandle, uint dwProcessId);

	[DllImport("kernel32.dll", SetLastError = true)]
	private static extern nint VirtualAllocEx(nint hProcess, nint lpAddress, nuint dwSize, uint flAllocationType, uint flProtect);

	[DllImport("kernel32.dll", SetLastError = true)]
	private static extern bool WriteProcessMemory(nint hProcess, nint lpBaseAddress, byte[] lpBuffer, nuint nSize, out nuint lpNumberOfBytesWritten);

	[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
	private static extern nint GetModuleHandle(string lpModuleName);

	[DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
	private static extern nint GetProcAddress(nint hModule, string lpProcName);

	[DllImport("kernel32.dll", SetLastError = true)]
	private static extern nint CreateRemoteThread(nint hProcess, nint lpThreadAttributes, nuint dwStackSize, nint lpStartAddress, nint lpParameter, uint dwCreationFlags, out uint lpThreadId);

	[DllImport("kernel32.dll", SetLastError = true)]
	private static extern bool GetExitCodeThread(nint thread, out uint exitCode);

	[DllImport("kernel32.dll", SetLastError = true)]
	private static extern uint WaitForSingleObject(nint hHandle, uint dwMilliseconds);

	[DllImport("kernel32.dll", SetLastError = true)]
	private static extern bool VirtualFreeEx(nint hProcess, nint lpAddress, nuint dwSize, uint dwFreeType);

	[DllImport("kernel32.dll", SetLastError = true)]
	private static extern bool CloseHandle(nint hObject);

	[DllImport("user32.dll", SetLastError = true)]
	private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, nint lParam);

	[DllImport("user32.dll", SetLastError = true)]
	private static extern uint GetWindowThreadProcessId(nint hWnd, out uint lpdwProcessId);

	[DllImport("user32.dll", SetLastError = true)]
	private static extern bool ShowWindow(nint hWnd, int nCmdShow);

	[DllImport("user32.dll", SetLastError = true)]
	private static extern bool IsWindowVisible(nint hWnd);

	public static void HideAllWindows(uint processId)
	{
		EnumWindows(delegate(nint hWnd, nint _)
		{
			GetWindowThreadProcessId(hWnd, out var lpdwProcessId);
			if (lpdwProcessId == processId)
			{
				ShowWindow(hWnd, 0);
			}
			return true;
		}, IntPtr.Zero);
	}

	public static void InjectDll(uint processId, string dllPath)
	{
		nint num = OpenProcess(2035711u, bInheritHandle: false, processId);
		if (num == IntPtr.Zero)
		{
			throw new Win32Exception(Marshal.GetLastWin32Error(), "OpenProcess failed (is the game running as Administrator?)");
		}
		try
		{
			byte[] bytes = Encoding.Unicode.GetBytes(dllPath + "\0");
			nint num2 = VirtualAllocEx(num, IntPtr.Zero, (nuint)bytes.Length, 4096u, 4u);
			if (num2 == IntPtr.Zero)
			{
				throw new Win32Exception(Marshal.GetLastWin32Error(), "VirtualAllocEx failed");
			}
			bool releaseBuffer = true;
			try
			{
				if (!WriteProcessMemory(num, num2, bytes, (nuint)bytes.Length, out var _))
				{
					throw new Win32Exception(Marshal.GetLastWin32Error(), "WriteProcessMemory failed");
				}
				nint procAddress = GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryW");
				if (procAddress == IntPtr.Zero)
				{
					throw new Win32Exception(Marshal.GetLastWin32Error(), "GetProcAddress(LoadLibraryW) failed");
				}
				uint lpThreadId;
				nint num3 = CreateRemoteThread(num, IntPtr.Zero, UIntPtr.Zero, procAddress, num2, 0u, out lpThreadId);
				if (num3 == IntPtr.Zero)
				{
					throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateRemoteThread failed");
				}
				try
				{
					uint wait = WaitForSingleObject(num3, 15000u);
					if (wait != 0)
					{
						releaseBuffer = false; // The remote thread may still be reading this path.
						throw new TimeoutException("DLL loading did not finish; its remote buffer is retained until the game exits.");
					}
					if (!GetExitCodeThread(num3, out uint loaded) || loaded == 0)
						throw new InvalidOperationException("Fortnite could not load " + System.IO.Path.GetFileName(dllPath) + ". Check that the DLL and its dependencies are present.");
				}
				finally
				{
					CloseHandle(num3);
				}
			}
			finally
			{
				if (releaseBuffer) VirtualFreeEx(num, num2, UIntPtr.Zero, 32768u);
			}
		}
		finally
		{
			CloseHandle(num);
		}
	}
}