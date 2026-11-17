// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;using System.Threading.Tasks;
namespace SyndicateClassic;
internal static class LauncherUpdater {
 public static Task<bool> Check(Action<string> status,bool repair=false){if(repair)status("Close Syndicate and reopen the standalone EXE to restore its bundled program files. Accounts stay saved.");return Task.FromResult(false);}
}