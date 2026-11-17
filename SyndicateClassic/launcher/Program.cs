using System;using System.IO;using System.IO.Compression;using System.Reflection;using System.Security.Cryptography;using System.Diagnostics;using System.Threading;using System.Windows;using System.Linq;
class Program {
 const string Version="0.3.40";
 static Window? splash;static System.Windows.Controls.TextBlock? status;static bool closing;
 static void Progress(string message){if(status==null)return;status.Text=message;status.Dispatcher.Invoke(()=>{},System.Windows.Threading.DispatcherPriority.Background);}
 static void ShowProgress(){status=new System.Windows.Controls.TextBlock{Text="Preparing Syndicate…",Margin=new Thickness(24),TextWrapping=TextWrapping.Wrap,VerticalAlignment=VerticalAlignment.Center,Foreground=System.Windows.Media.Brushes.White};splash=new Window{Title="Project Tilted",Width=460,Height=150,ResizeMode=ResizeMode.NoResize,WindowStartupLocation=WindowStartupLocation.CenterScreen,Background=new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(17,24,39)),Content=status};splash.Closing+=(_,e)=>e.Cancel=!closing;splash.Show();Progress("Preparing encrypted files. First launch can take a few minutes.");}
 static void HideProgress(){closing=true;splash?.Close();splash=null;status=null;}
 static string SafePath(string root,string name){
  if(string.IsNullOrEmpty(name)||name.Contains(':')||name.Replace('\\','/').Split('/').Any(p=>p is "" or "." or ".."))throw new InvalidDataException("Invalid bundled path");
  var path=Path.GetFullPath(Path.Combine(root,name));var prefix=Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar;
  if(!path.StartsWith(prefix,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Invalid bundled path");
  for(var dir=Path.GetDirectoryName(path);dir!=null;dir=Path.GetDirectoryName(dir))if(Directory.Exists(dir)&&(File.GetAttributes(dir)&FileAttributes.ReparsePoint)!=0)throw new IOException("Linked runtime folders are not supported");
  if(File.Exists(path)&&(File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0)throw new IOException("Linked runtime files are not supported");return path;
 }
 static byte[] Embedded(){using var s=Assembly.GetExecutingAssembly().GetManifestResourceStream("Syndicate.Payload")!;using var m=new MemoryStream();s.CopyTo(m);return m.ToArray();}
 static byte[] Decrypt(byte[] encrypted){if(encrypted.Length<28)throw new InvalidDataException("Incomplete bundle");var plain=new byte[encrypted.Length-28];using var aes=new AesGcm(Convert.FromBase64String(PayloadKey.Value),16);aes.Decrypt(encrypted.AsSpan(0,12),encrypted.AsSpan(28),encrypted.AsSpan(12,16),plain);return plain;}
 static string Hash(Stream s)=>Convert.ToHexString(SHA256.HashData(s));
 static bool Matches(ZipArchiveEntry e,string file){try{if(!File.Exists(file)||new FileInfo(file).Length!=e.Length)return false;using var a=e.Open();using var b=File.OpenRead(file);return Hash(a)==Hash(b);}catch(IOException){return false;}}
 // Integrity check on every start: the encrypted bundle carries a "tilted-hashes" entry listing the
 // SHA-256 of every file. Because it lives inside the AES-GCM payload it cannot be forged on disk, so a
 // player cannot edit a program file and have it trusted — the contents are re-checked, not a timestamp.
 // When every file on disk already matches, the ~300 MB of entries are not decompressed again.
 static string? PayloadId;
 const string ManifestName="tilted-hashes";
 static bool Ready(ZipArchive zip,string root){
  try{
   var manifest=zip.GetEntry(ManifestName);if(manifest==null)return false; // older bundle: do a full verified extract
   string text;using(var r=new StreamReader(manifest.Open()))text=r.ReadToEnd();
   foreach(var raw in text.Split('\n')){
    var line=raw.TrimEnd('\r');if(line.Length==0)continue;
    if(line.Length<66||line[64]!=' ')return false;
    var hash=line.Substring(0,64);var path=SafePath(root,line.Substring(65));
    if(!File.Exists(path))return false;
    using var fs=File.OpenRead(path);if(!Hash(fs).Equals(hash,StringComparison.OrdinalIgnoreCase))return false;
   }
   return true;
  }catch(Exception e)when(e is IOException or FormatException or UnauthorizedAccessException or InvalidDataException){return false;}
 }
 static void Extract(ZipArchive zip,string root){
  Directory.CreateDirectory(root);
  if(Ready(zip,root))return;
  // Preflight every destination before changing any program file. Accounts are not in this archive.
  foreach(var e in zip.Entries){var path=SafePath(root,e.FullName);if(File.Exists(path)&&!Matches(e,path)){using var probe=new FileStream(path,FileMode.Open,FileAccess.ReadWrite,FileShare.None);}}
  int count=0;foreach(var e in zip.Entries){if(count++%200==0)Progress($"Preparing verified game services… {count*100/zip.Entries.Count}%\nYour accounts and settings stay saved.");var path=SafePath(root,e.FullName);if(Matches(e,path))continue;Directory.CreateDirectory(Path.GetDirectoryName(path)!);var temp=path+".restore-"+Guid.NewGuid().ToString("N");try{e.ExtractToFile(temp);if(!Matches(e,temp))throw new IOException("File verification failed: "+e.FullName+(File.Exists(temp)?"":" (the file was removed right after it was written)")+"\n\nThis usually means Windows Security or another antivirus removed or changed it. Open Windows Security > Virus & threat protection > Protection history to see what it did.");File.Move(temp,path,true);}finally{if(File.Exists(temp))File.Delete(temp);}}
  foreach(var e in zip.Entries)if(!Matches(e,SafePath(root,e.FullName)))throw new IOException("Bundle incomplete: "+e.FullName);
 }
 static int Cleanup(ZipArchive zip,string root){int retained=0;foreach(var e in zip.Entries){var path=SafePath(root,e.FullName);try{if(File.Exists(path))File.Delete(path);}catch(IOException){retained++;}catch(UnauthorizedAccessException){retained++;}}return retained;}
 [STAThread]static int Main(string[] args){
  var home=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"SyndicateClassicStandalone",Version);
  bool test=args.Length==2&&(args[0]=="--verify-only"||args[0]=="--exercise");
  try{
   using var mutex=new Mutex(false,"Local\\ProjectTiltedStandalone040");bool acquired;try{acquired=mutex.WaitOne(0);}catch(AbandonedMutexException){acquired=true;}if(!acquired){if(!test)MessageBox.Show("Syndicate is already running. Use its open launcher window.","Project Tilted");return 2;}
   var root=test?Path.GetFullPath(args[1]):Path.Combine(home,"RuntimeFiles");
   if(test&&Directory.Exists(root))throw new IOException("Use a new empty test destination");
   if(!test)ShowProgress();var encrypted=Embedded();
   if(!test){Directory.CreateDirectory(home);var vault=Path.Combine(home,"SyndicateClassic.encrypted");SafePath(home,"SyndicateClassic.encrypted");PayloadId=Convert.ToHexString(SHA256.HashData(encrypted));var vaultId=vault+".sha256";bool valid=File.Exists(vault)&&File.Exists(vaultId)&&new FileInfo(vault).Length==encrypted.Length&&File.ReadAllText(vaultId).Trim()==PayloadId;if(!valid){var temp=vault+".new";File.WriteAllBytes(temp,encrypted);File.Move(temp,vault,true);File.WriteAllText(vaultId,PayloadId);}}
   var bytes=Decrypt(encrypted);using var stream=new MemoryStream(bytes,false);using var zip=new ZipArchive(stream,ZipArchiveMode.Read);Extract(zip,root);
   // Keep the extracted runtime out of plain sight: mark it Hidden so it does not show in a default folder
   // listing. This is only a speed bump (it does not stop anyone who shows hidden files or copies mid-match),
   // and processes read a Hidden folder normally, so Node/Mongo/the game server are unaffected. Never fatal.
   if(!test)try{new DirectoryInfo(root).Attributes|=FileAttributes.Hidden;}catch{}
   if(test){
    if(args[0]=="--exercise"){
     bool rejected=false;var bad=(byte[])encrypted.Clone();bad[^1]^=1;try{Decrypt(bad);}catch(CryptographicException){rejected=true;}if(!rejected)throw new Exception("Tampered encrypted payload accepted");
     foreach(var name in new[]{"../escape","C:/escape","folder/../../escape"}){rejected=false;try{SafePath(root,name);}catch(InvalidDataException){rejected=true;}if(!rejected)throw new Exception("Traversal accepted");}
     var sentinel=Path.Combine(root,"user-account-preservation.txt");File.WriteAllText(sentinel,"preserve");var entry=zip.Entries.First();var path=SafePath(root,entry.FullName);File.WriteAllText(path,"damaged");
     var locked=SafePath(root,zip.Entries[1].FullName);using(var hold=new FileStream(locked,FileMode.Open,FileAccess.ReadWrite,FileShare.None)){rejected=false;try{Extract(zip,root);}catch(IOException){rejected=true;}if(!rejected||File.ReadAllText(path)!="damaged")throw new Exception("Lock preflight changed files");}
     Extract(zip,root);if(!Matches(entry,path))throw new Exception("Repair failed");
     if(Cleanup(zip,root)!=0)throw new Exception("Cleanup failed");if(File.ReadAllText(sentinel)!="preserve")throw new Exception("User data changed");
    }
    File.WriteAllText(root+".verified.txt","PASS encrypted payload authentication, full extraction and component hashes"+(args[0]=="--exercise"?", corruption recovery, path containment, cleanup and user-data preservation":""));return 0;
   }
   var start=new ProcessStartInfo(SafePath(root,"Launcher/SyndicateLauncher.exe")){WorkingDirectory=Path.Combine(root,"Launcher"),UseShellExecute=false};start.ArgumentList.Add("--skip-update");
   using var child=Process.Start(start)??throw new IOException("Launcher could not start");HideProgress();child.WaitForExit();
   int remaining=Cleanup(zip,root);File.AppendAllText(Path.Combine(home,"standalone.log"),$"{DateTime.UtcNow:O} Launcher exit={child.ExitCode}; locked program files retained={remaining}\n");return child.ExitCode;
  }catch(Exception e){HideProgress();try{Directory.CreateDirectory(home);File.AppendAllText(Path.Combine(home,"standalone.log"),DateTime.UtcNow.ToString("O")+" "+e+"\n");}catch{}if(!test)MessageBox.Show("Syndicate could not start. Your saved accounts have not been deleted.\n\n"+e.Message+"\n\nClose other Syndicate windows and retry. Details: "+Path.Combine(home,"standalone.log"),"Project Tilted");return 1;}
 }
}
