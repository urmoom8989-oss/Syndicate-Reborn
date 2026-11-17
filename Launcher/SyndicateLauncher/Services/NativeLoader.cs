using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
namespace SyndicateLauncher.Services;
internal static class NativeLoader
{
    [DllImport("kernel32.dll",SetLastError=true)] private static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
    [DllImport("kernel32.dll",SetLastError=true)] private static extern IntPtr VirtualAllocEx(IntPtr process,IntPtr address,UIntPtr size,uint allocation,uint protect);
    [DllImport("kernel32.dll",SetLastError=true)] private static extern bool VirtualFreeEx(IntPtr process,IntPtr address,UIntPtr size,uint type);
    [DllImport("kernel32.dll",SetLastError=true)] private static extern bool WriteProcessMemory(IntPtr process,IntPtr address,byte[] bytes,UIntPtr size,out UIntPtr written);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] private static extern IntPtr GetModuleHandle(string module);
    [DllImport("kernel32.dll",CharSet=CharSet.Ansi)] private static extern IntPtr GetProcAddress(IntPtr module,string name);
    [DllImport("kernel32.dll",SetLastError=true)] private static extern bool GetModuleHandleEx(uint flags,IntPtr address,out IntPtr module);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] private static extern uint GetModuleFileName(IntPtr module,StringBuilder file,int size);
    [DllImport("kernel32.dll",SetLastError=true)] private static extern IntPtr CreateRemoteThread(IntPtr process,IntPtr attributes,UIntPtr stack,IntPtr start,IntPtr argument,uint flags,out uint id);
    [DllImport("kernel32.dll",SetLastError=true)] private static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
    [DllImport("kernel32.dll",SetLastError=true)] private static extern bool GetExitCodeThread(IntPtr thread,out uint exitCode);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
    public static async Task LoadAsync(Process ownedProcess,string file,Action<string>? progress=null,string? fortniteVersion=null)
    {
        file=Path.GetFullPath(file);if(!File.Exists(file))throw new LaunchProblemException(LaunchProblem.ModuleMissing(Path.GetFileName(file)));
        if(ownedProcess.HasExited)throw new InvalidOperationException("Game exited before its module could load");
        await Task.Run(async ()=>{
            var process=OpenProcess(0x0002|0x0008|0x0010|0x0020|0x0400,false,ownedProcess.Id);
            if(process==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error());
            IntPtr memory=IntPtr.Zero,thread=IntPtr.Zero;bool finished=false;
            try {
                var bytes=Encoding.Unicode.GetBytes(file+"\0");
                memory=VirtualAllocEx(process,IntPtr.Zero,(UIntPtr)bytes.Length,0x3000,0x04);
                if(memory==IntPtr.Zero||!WriteProcessMemory(process,memory,bytes,(UIntPtr)bytes.Length,out var written)||written.ToUInt64()!=(ulong)bytes.Length)throw new Win32Exception(Marshal.GetLastWin32Error());
                var load=GetProcAddress(GetModuleHandle("kernel32.dll"),"LoadLibraryW");
                if(load==IntPtr.Zero||!GetModuleHandleEx(0x00000004|0x00000002,load,out var actualModule))throw new Win32Exception(Marshal.GetLastWin32Error());
                var moduleFile=new StringBuilder(1024);
                if(GetModuleFileName(actualModule,moduleFile,moduleFile.Capacity)==0)throw new Win32Exception(Marshal.GetLastWin32Error());
                var actualName=Path.GetFileName(moduleFile.ToString());
                // Process.Start returns before the native loader finishes mapping system DLLs.
                ProcessModule? remote=null;
                var deadline=Stopwatch.StartNew();
                while(deadline.Elapsed<TimeSpan.FromSeconds(10)){
                    ownedProcess.Refresh();
                    if(ownedProcess.HasExited)throw new InvalidOperationException("Game exited while initializing "+Path.GetFileName(file));
                    try{remote=ownedProcess.Modules.Cast<ProcessModule>().FirstOrDefault(m=>string.Equals(m.ModuleName,actualName,StringComparison.OrdinalIgnoreCase));}
                    catch(Win32Exception ex)when(ex.NativeErrorCode==299||ex.NativeErrorCode==24){}
                    if(remote!=null)break;
                    Thread.Sleep(25);
                }
                if(remote==null)throw new TimeoutException("Game did not initialize its Windows loader ("+actualName+") before loading "+Path.GetFileName(file));
                var remoteLoad=remote.BaseAddress+(int)(load.ToInt64()-actualModule.ToInt64());
                thread=CreateRemoteThread(process,IntPtr.Zero,UIntPtr.Zero,remoteLoad,memory,0,out _);
                if(thread==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error());
                var loading=Stopwatch.StartNew();int reported=-1;
                while(true){
                    var wait=WaitForSingleObject(thread,0);
                    if(wait==0)break;
                    if(wait==uint.MaxValue)throw new Win32Exception(Marshal.GetLastWin32Error(),"Could not monitor game module initialization");
                    if(wait!=258)throw new InvalidOperationException("Unexpected game module wait result: "+wait);
                    ownedProcess.Refresh();
                    if(ownedProcess.HasExited)throw new InvalidOperationException("Fortnite exited while loading "+Path.GetFileName(file)+" (0x"+ownedProcess.ExitCode.ToString("X8")+")");
                    if(loading.Elapsed>=ModuleLoadPolicy.Timeout)throw new LaunchProblemException(LaunchProblem.ModuleTimedOut(Path.GetFileName(file)));
                    int seconds=(int)loading.Elapsed.TotalSeconds;
                    if(seconds/5!=reported){reported=seconds/5;progress?.Invoke("Loading "+Path.GetFileName(file)+"... "+seconds+"s");}
                    await Task.Delay(100);
                }
                finished=true;
                if(!GetExitCodeThread(thread,out var result))throw new Win32Exception(Marshal.GetLastWin32Error());
                bool mapped=false;var verifying=Stopwatch.StartNew();
                do{
                    ownedProcess.Refresh();
                    if(ownedProcess.HasExited)throw new InvalidOperationException("Fortnite exited after loading "+Path.GetFileName(file)+" (0x"+ownedProcess.ExitCode.ToString("X8")+")");
                    try{mapped=ownedProcess.Modules.Cast<ProcessModule>().Any(m=>string.Equals(m.FileName,file,StringComparison.OrdinalIgnoreCase));}
                    catch(Win32Exception ex)when(ex.NativeErrorCode==299||ex.NativeErrorCode==24){}
                    if(mapped||result==0||result>=0xC0000000)break;
                    await Task.Delay(100);
                }while(verifying.Elapsed<TimeSpan.FromSeconds(5));
                // HMODULE is 64-bit, but GetExitCodeThread returns only its low
                // DWORD. A mapped DLL is authoritative even if those bits are 0.
                if(!ModuleLoadPolicy.Confirmed(result,mapped)){
                    // LoadLibraryW returned NULL inside Fortnite. Find out why without running the DLL:
                    // blocked by Windows Security, missing/damaged, or a DLL it needs is missing. If none of
                    // those, the DLL's own startup code failed in this Fortnite version.
                    var problem=ModuleCheck.Diagnose(file)??LaunchProblem.ModuleStartFailed(Path.GetFileName(file),fortniteVersion);
                    throw new LaunchProblemException(problem);
                }
                progress?.Invoke("Loaded "+Path.GetFileName(file)+" ("+loading.Elapsed.TotalSeconds.ToString("F1")+"s, loader=0x"+result.ToString("X8")+")");
            }
            finally {if(thread!=IntPtr.Zero)CloseHandle(thread);if((finished||thread==IntPtr.Zero)&&memory!=IntPtr.Zero)VirtualFreeEx(process,memory,UIntPtr.Zero,0x8000);CloseHandle(process);}
        });
    }
}
