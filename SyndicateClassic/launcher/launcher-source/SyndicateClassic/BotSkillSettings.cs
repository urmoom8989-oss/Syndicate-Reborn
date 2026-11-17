// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.IO;
namespace SyndicateClassic;
internal static class BotSkillSettings {
 internal static readonly string[] Values={"bot","easy","medium","hard","variety"};
 internal static string PathName=>Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"ProjectTilted","bot-skill.txt");
 internal static string Load(string? path=null){try {var v=File.ReadAllText(path??PathName).Trim();return Array.IndexOf(Values,v)>=0?v:"medium";}catch(IOException){return "medium";}catch(UnauthorizedAccessException){return "medium";}}
 internal static void Save(string value,string? path=null){if(Array.IndexOf(Values,value)<0)throw new ArgumentException("Invalid bot skill");path??=PathName;Directory.CreateDirectory(Path.GetDirectoryName(path)!);var temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";try{File.WriteAllText(temp,value);File.Move(temp,path,true);}finally{if(File.Exists(temp))File.Delete(temp);}}
}