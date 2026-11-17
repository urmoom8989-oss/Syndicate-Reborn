// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Linq;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
namespace SyndicateClassic;
public partial class MainWindow
{
    private static IEnumerable<DependencyObject> SettingsTree(DependencyObject root){yield return root;foreach(var child in LogicalTreeHelper.GetChildren(root).OfType<DependencyObject>())foreach(var item in SettingsTree(child))yield return item;}
    private static void ReleaseControl(FrameworkElement control){if(control.Parent is Panel panel)panel.Children.Remove(control);else if(control.Parent is Decorator decorator)decorator.Child=null;control.Margin=new Thickness(0);control.HorizontalAlignment=HorizontalAlignment.Stretch;control.ClearValue(WidthProperty);}

    // Settings in the Night Drop style: a sidebar of pages (Match & bots, Modded, Display, Discord,
    // Cosmetics, Match data, Launcher & files, About) and the selected page beside it. "Play with
    // friends" moved to the Multiplayer tab in the rail.
    private void RedesignHeaderAndSettings()
    {
        var navigation=SettingsTree(this).OfType<Button>().FirstOrDefault(b=>string.Equals(b.Content?.ToString(),"FORTNITE",StringComparison.OrdinalIgnoreCase));
        if(navigation?.Parent is StackPanel tabs){
            var brand=Label("SYNDICATE",21,"#B1E9C9");brand.FontWeight=FontWeights.Bold;brand.VerticalAlignment=VerticalAlignment.Center;brand.Margin=new Thickness(0,0,30,0);tabs.Children.Insert(0,brand);
            foreach(var button in tabs.Children.OfType<Button>()){button.Content=button==navigation?"Library":"Battle passes";button.Height=34;button.MinWidth=95;button.Margin=new Thickness(0,0,8,0);PolishButton(button,button==navigation?"#293C31":"#17211C",button==navigation?"#DFF8E8":"#A7BEAF");}
            var packs=new Button{Content="Starter packs",Tag="packs",Height=34,MinWidth=105,Margin=new Thickness(0,0,8,0)};PolishButton(packs,"#17211C","#A7BEAF");packs.Click+=PassesTab_Click;tabs.Children.Add(packs);
            if(tabs.Parent is Grid header){header.Background=Ink("#17211C");}
        }
        if(AccountButton.Parent is StackPanel accountActions){accountActions.VerticalAlignment=VerticalAlignment.Center;accountActions.Margin=new Thickness(0);}
        AccountButton.MinWidth=126;AccountButton.Height=30;AccountButton.Margin=new Thickness(0,0,12,0);PolishButton(AccountButton,"#293C31","#DFF8E8");
        var controls=SettingsTree(SettingsPanel).ToArray();
        var count=controls.OfType<TextBox>().Single(x=>x.Name=="PolishBotCount");var hint=controls.OfType<TextBlock>().Single(x=>x.Name=="PolishBotHint");var skill=controls.OfType<ComboBox>().Single(x=>x.Name=="PolishBotSkill");
        var repair=controls.OfType<Button>().Single(x=>x.Content?.ToString()=="Repair Local Services");var folder=controls.OfType<Button>().First(x=>x.Content?.ToString()=="Choose game folder");
        foreach(var control in new FrameworkElement[]{count,hint,skill,repair,folder,RadminHostBox,SaveRadminButton,RadminStatus,StopBackendButton})ReleaseControl(control);

        // Settings as a full app page: a sidebar of sections on the left (icon, name, one-line hint) and
        // the selected section on the right with its title and a short explanation. It sits right of the
        // rail and below the title bar, so nothing overlaps, and scrolls on its own when a section is long.
        SettingsPanel.Background=new SolidColorBrush(Color.FromArgb(205,5,6,13));SettingsPanel.Padding=new Thickness(RailWidth+20,64,36,22);RailTrack(SettingsPanel,true);
        var shell=new Border{MaxWidth=1180,MaxHeight=860,HorizontalAlignment=HorizontalAlignment.Stretch,VerticalAlignment=VerticalAlignment.Stretch,CornerRadius=TiltCorners(18),Background=GlassFill(),BorderBrush=GlassEdgeFill(),BorderThickness=new Thickness(1),ClipToBounds=true};
        shell.Effect=new System.Windows.Media.Effects.DropShadowEffect{Color=Colors.Black,BlurRadius=40,ShadowDepth=10,Opacity=.55};
        SettingsPanel.Child=shell;
        var layout=new Grid();layout.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(260)});layout.ColumnDefinitions.Add(new ColumnDefinition());
        // Behind everything: the diagonal hatch and the open page's name, huge and faint (the Syndicate look, NightDrop.cs).
        var shellBack=new Grid();shellBack.Children.Add(TiltHatch());shell.Child=shellBack;shellBack.Children.Add(layout);

        // Sidebar
        var sidebar=new Grid{Background=Hex("#D906070C")};sidebar.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});sidebar.RowDefinitions.Add(new RowDefinition());sidebar.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});
        layout.Children.Add(new Border{Child=sidebar,BorderBrush=Hex(Night.Line),BorderThickness=new Thickness(0,0,1,0)});
        var sideBrand=new StackPanel{Margin=new Thickness(22,24,22,18)};sideBrand.Children.Add(NightEyebrow("Make it yours"));sideBrand.Children.Add(NightTitle("Settings",34).Also(t=>t.Margin=new Thickness(0,2,0,0)));sidebar.Children.Add(sideBrand);
        var navList=new StackPanel{Margin=new Thickness(12,0,12,12)};
        var navScroll=new ScrollViewer{Content=navList,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled};navScroll.Resources.Add(typeof(System.Windows.Controls.Primitives.ScrollBar),DarkScrollBar());Grid.SetRow(navScroll,1);sidebar.Children.Add(navScroll);
        var footer=NightText("Syndicate v"+LauncherVersion()+"\nEsc closes settings",11,Night.Muted).Also(t=>{t.Margin=new Thickness(24,8,22,18);t.LineHeight=17;});Grid.SetRow(footer,2);sidebar.Children.Add(footer);

        // Content: page header (title, explanation, close) and the scrolling page.
        var content=new Grid{Margin=new Thickness(32,24,24,20)};content.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});content.RowDefinitions.Add(new RowDefinition());Grid.SetColumn(content,1);layout.Children.Add(content);
        var top=new DockPanel{Margin=new Thickness(0,0,0,18)};content.Children.Add(top);
        var close=NightRoundIcon("","Close settings (Esc)",40,12);close.VerticalAlignment=VerticalAlignment.Top;close.Click+=(_,__)=>SettingsPanel.Visibility=Visibility.Collapsed;DockPanel.SetDock(close,Dock.Right);top.Children.Add(close);
        var pageHeading=new StackPanel{Margin=new Thickness(0,2,16,0)};var pageTitle=NightTitle("",44,Night.Text);var pageIndex=NightText("",11,Night.Gold,bold:true).Also(t=>{t.TextWrapping=TextWrapping.NoWrap;t.Margin=new Thickness(2,0,0,0);});var pageDetail=NightText("",13,Night.Sub).Also(t=>{t.TextWrapping=TextWrapping.Wrap;t.Margin=new Thickness(0,6,0,0);t.MaxWidth=760;t.HorizontalAlignment=HorizontalAlignment.Left;});
        pageHeading.Children.Add(pageIndex);pageHeading.Children.Add(pageTitle);pageHeading.Children.Add(pageDetail);top.Children.Add(pageHeading);
        var pageWatermark=Display("",170,Night.Text).Also(t=>{t.Opacity=.035;t.IsHitTestVisible=false;t.HorizontalAlignment=HorizontalAlignment.Right;t.VerticalAlignment=VerticalAlignment.Bottom;t.Margin=new Thickness(0,0,-10,-40);});Grid.SetRowSpan(pageWatermark,2);content.Children.Insert(0,pageWatermark);
        var scroll=new ScrollViewer{VerticalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled,Padding=new Thickness(0,0,8,0)};scroll.Resources.Add(typeof(System.Windows.Controls.Primitives.ScrollBar),DarkScrollBar());Grid.SetRow(scroll,1);content.Children.Add(scroll);var sections=new StackPanel{MaxWidth=860,HorizontalAlignment=HorizontalAlignment.Left};scroll.Content=sections;

        var pages=new List<(string Name,string Hint,string Glyph,string Detail,Border Card)>();
        // One page = one card. Name and explanation go in the page header; Hint is the sidebar line.
        StackPanel Section(string name,string detail,string glyph="",string hint=""){
            var body=new StackPanel();
            var notched=new Grid();notched.Children.Add(body);notched.Children.Add(TiltBar(30,3).Also(b=>{b.HorizontalAlignment=HorizontalAlignment.Left;b.VerticalAlignment=VerticalAlignment.Top;b.Margin=new Thickness(0,-22,0,0);}));
            var card=new Border{CornerRadius=new CornerRadius(16,3,16,3),Background=GlassFill(),BorderBrush=GlassEdgeFill(),BorderThickness=new Thickness(1),Padding=new Thickness(26,22,26,24),Margin=new Thickness(0,0,0,12),Child=notched,Visibility=Visibility.Collapsed};
            sections.Children.Add(card);pages.Add((name,hint,glyph,detail,card));
            return body;
        }
        TextBlock RowLabel(string text){var label=NightText(text,13.5,Night.Text,bold:true);return label;}

        var bots=Section("Match & bots","Your local match: how many bots and how hard they are. Choose the playlist in Fortnite. Changes apply to the next match.","","Bots and difficulty");
        var numberRow=new DockPanel();var countLabel=RowLabel("Number of bots");countLabel.VerticalAlignment=VerticalAlignment.Center;NightField(count);count.Width=90;count.Height=40;DockPanel.SetDock(count,Dock.Right);numberRow.Children.Add(count);numberRow.Children.Add(countLabel);bots.Children.Add(numberRow);
        hint.MaxWidth=double.PositiveInfinity;hint.Foreground=Hex(Night.Sub);hint.FontSize=11.5;hint.FontFamily=NightBody;hint.Margin=new Thickness(0,8,0,18);bots.Children.Add(hint);
        bots.Children.Add(RowLabel("Difficulty"));var choices=new System.Windows.Controls.Primitives.UniformGrid{Columns=5,Margin=new Thickness(0,10,0,10)};bots.Children.Add(choices);var descriptions=new[]{"A relaxed match. Bots rarely shoot or build.","Forgiving aim with a little defensive building.","Balanced aim, movement and building.","Accurate shots and aggressive building.","A mix of Easy, Medium and Hard opponents."};var explanation=NightText("",12,Night.Sub);bots.Children.Add(explanation);
        var buttons=new List<Button>();void Refresh(){for(int i=0;i<buttons.Count;i++)SetChip(buttons[i],i==skill.SelectedIndex);explanation.Text=descriptions[Math.Clamp(skill.SelectedIndex,0,4)];}
        for(int i=0;i<5;i++){int index=i;var button=NightChip(skill.Items[i]?.ToString()??"");button.Height=36;button.Margin=new Thickness(0,0,6,0);button.HorizontalContentAlignment=HorizontalAlignment.Center;button.Click+=(_,__)=>skill.SelectedIndex=index;buttons.Add(button);choices.Children.Add(button);}skill.Visibility=Visibility.Collapsed;bots.Children.Add(skill);skill.SelectionChanged+=(_,__)=>Refresh();Refresh();
        bots.Children.Add(NightText("Bot count saves on Enter or when you leave the field. Difficulty saves immediately.",11.5,Night.Muted).Also(t=>{t.Margin=new Thickness(0,16,0,0);t.TextWrapping=TextWrapping.Wrap;}));
        bots.Children.Add(SettingsToggle("Quick queue","While you are in the Fortnite lobby, Syndicate starts your last playlist's match server and loads the island in the background, so pressing Ready joins in seconds instead of a minute or two. Picking a different playlist starts that one instead (the usual wait). Uses more memory while you sit in the lobby; turn it off on PCs with 8 GB of RAM or less.",GameServer.QuickQueueEnabled,on=>GameServer.QuickQueueEnabled=on).Also(t=>t.Margin=new Thickness(0,18,0,0)));
        bots.Children.Add(SettingsToggle("60 ticks in full lobbies","The match server updates the game 60 times a second instead of 30 when there are more than 30 bots, so shots, building and movement are checked twice as often and feel snappier, also for Radmin friends. It needs a strong PC: if the match stutters or bots lag, turn it off. Applies from the next match.",GameServer.HighTickFullLobbies,on=>GameServer.HighTickFullLobbies=on).Also(t=>t.Margin=new Thickness(0,18,0,0)));

        var moddedPage=Section("Mods","Extras that go beyond the original game. Every card shows which seasons it works in and whether it changes while you play.","","Controller, movement, building");BuildModdedPage(moddedPage);
        var display=Section("Display","Play stretched and set your field of view, in every season.","","Stretched resolution and FOV");AddDisplaySettings(display);AddFovSettings(display);
        // Discord status is always on now (no tab, no toggle) but still shows and works.
        // Cosmetics moved into the Modded page (see BuildModdedPage).
        var redeemPage=Section("Redeem code","Have a one-time code? Enter it here to unlock an exclusive item, like Psycho Bandit, for the selected player.","\uE8EC","Exclusive items");AddRedeemSettings(redeemPage);
        // Match data tab removed. Developer Tools (diagnostics window, custom-map editor, Discord relay test)
        // moved to their own Dev-Kit-only page below; public builds never compile it.
#if TILTED_DEVKIT
        var devToolsPage=Section("Developer tools","Dev Kit only: match reports, bot weaknesses, landing stats, the custom-map editor and the Discord relay test. Not present in public builds.","","Diagnostics & map editor");AddDeveloperToolsSettings(devToolsPage);
#endif
        var maintenance=Section("Launcher & files","Your game folder and Syndicate's local services, the version, where Syndicate keeps its files, and updates. Repair keeps your accounts and settings.","","Folders, version and updates");
        foreach(var button in new[]{folder,StopBackendButton,repair}){button.Height=38;button.Margin=new Thickness(0,0,0,8);button.HorizontalAlignment=HorizontalAlignment.Left;button.MinWidth=260;NightButton(button,"base");maintenance.Children.Add(button);}repair.Content="Repair launcher & services";
        // Look and effects (LauncherThemes.cs).
        maintenance.Children.Add(new Border{Height=1,Background=Hex(Night.Line),Margin=new Thickness(0,18,0,16)});
        AddThemeSettings(maintenance);
        maintenance.Children.Add(SettingsToggle("Show the music player","The small music card on the launcher. Turn it off to hide it (the music keeps its own on/off and volume). Drag the card by its note or title to move it anywhere.",!MusicPlayerHidden,on=>{MusicPlayerHidden=!on;_musicRefresh?.Invoke();}).Also(t=>t.Margin=new Thickness(0,14,0,8)));
        var resetMusic=new Button{Content="Put the music player back in its usual place",Height=34,Padding=new Thickness(14,0,14,0),HorizontalAlignment=HorizontalAlignment.Left,Margin=new Thickness(0,0,0,4)};NightButton(resetMusic,"base");
        resetMusic.Click+=(_,__)=>{MusicPlayerPos=null;_musicPlace?.Invoke();_musicRefresh?.Invoke();};maintenance.Children.Add(resetMusic);
        // About (version, files location, updates) now lives at the bottom of Launcher & files.
        maintenance.Children.Add(new Border{Height=1,Background=Hex(Night.Line),Margin=new Thickness(0,18,0,16)});
        maintenance.Children.Add(RowLabel("About").Also(t=>t.Margin=new Thickness(0,0,0,10)));
        AddAboutSettings(maintenance);

        // Sidebar buttons, one per page.
        var navButtons=new List<Button>();
        int currentSection=0;
        void SelectSection(int selected){
            currentSection=selected;
            for(int i=0;i<pages.Count;i++){
                bool on=i==selected;pages[i].Card.Visibility=on?Visibility.Visible:Visibility.Collapsed;
                // The open page: a leaning gold bar, a gold number and icon, and a faint gold wash (like the side menu).
                var nav=navButtons[i];nav.Tag=on?"on":null;nav.Background=Hex(on?"#17FFD23F":"#00000000");nav.BorderBrush=Hex(on?"#40FFD23F":"#00000000");
                if(nav.Content is Grid row)foreach(var part in row.Children.OfType<FrameworkElement>())switch(part.Tag as string){
                    case "bar":part.Visibility=on?Visibility.Visible:Visibility.Hidden;break;
                    case "index":((TextBlock)part).Foreground=Hex(on?Night.Gold:Night.Muted);break;
                    case "icon":((TextBlock)part).Foreground=Hex(on?Night.Gold:Night.Muted);break;
                    case "words":foreach(var text in ((StackPanel)part).Children.OfType<TextBlock>())text.Foreground=Hex(text.FontSize>12?(on?"#FFFFFF":Night.Text):(on?"#D9C78A":Night.Muted));break;}
            }
            pageTitle.Text=pages[selected].Name.ToUpperInvariant();pageDetail.Text=pages[selected].Detail;scroll.ScrollToTop();
            pageIndex.Text=(selected+1).ToString("00")+"  /  "+pages.Count.ToString("00");pageWatermark.Text=pages[selected].Name.Split(' ')[0].ToUpperInvariant();
            if(MotionEnabled){pageTitle.BeginAnimation(OpacityProperty,new System.Windows.Media.Animation.DoubleAnimation(0,1,TimeSpan.FromMilliseconds(260)));SlideIn(pages[selected].Card,-24,0,300);}
        }
        for(int i=0;i<pages.Count;i++){
            int selected=i;var page=pages[i];
            // Numbered rows like the side menu: leaning bar | 01 | name and hint | icon.
            string glyph=page.Glyph.Length>0?page.Glyph:page.Name.StartsWith("Developer")?"\uEC7A":page.Name.StartsWith("Launcher")?"\uE8B7":"\uE713";
            var row=new Grid();row.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(12)});row.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(30)});row.ColumnDefinitions.Add(new ColumnDefinition());row.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});
            row.Children.Add(TiltBar(4,22).Also(b=>{b.Tag="bar";b.Visibility=Visibility.Hidden;b.HorizontalAlignment=HorizontalAlignment.Left;}));
            row.Children.Add(NightText((i+1).ToString("00"),11,Night.Muted,bold:true).Also(t=>{t.Tag="index";t.TextWrapping=TextWrapping.NoWrap;t.VerticalAlignment=VerticalAlignment.Center;Grid.SetColumn(t,1);}));
            var words=new StackPanel{VerticalAlignment=VerticalAlignment.Center,Tag="words"};words.Children.Add(NightText(page.Name,13.5,Night.Text,bold:true).Also(t=>t.TextWrapping=TextWrapping.NoWrap));
            if(page.Hint.Length>0)words.Children.Add(NightText(page.Hint,11,Night.Muted).Also(t=>{t.TextWrapping=TextWrapping.NoWrap;t.TextTrimming=TextTrimming.CharacterEllipsis;}));
            Grid.SetColumn(words,2);row.Children.Add(words);
            row.Children.Add(GlyphIcon(glyph,14,Night.Muted).Also(g=>{g.Tag="icon";g.VerticalAlignment=VerticalAlignment.Center;g.Margin=new Thickness(8,0,0,0);Grid.SetColumn(g,3);}));
            var nav=new Button{Content=row,Height=54,Padding=new Thickness(6,0,12,0),Margin=new Thickness(0,0,0,4),HorizontalContentAlignment=HorizontalAlignment.Stretch,ToolTip=page.Detail};
            RoundButton(nav,"#00000000",Night.Text,12,"#00000000");nav.BorderThickness=new Thickness(1);nav.HorizontalContentAlignment=HorizontalAlignment.Stretch;
            nav.MouseEnter+=(_,__)=>{if(!Equals(nav.Tag,"on"))nav.Background=Hex(Night.FrostHover);};nav.MouseLeave+=(_,__)=>{if(!Equals(nav.Tag,"on"))nav.Background=Hex("#00000000");};
            nav.Click+=(_,__)=>SelectSection(selected);navButtons.Add(nav);navList.Children.Add(nav);
        }
        SelectSection(0);
        _settingsStep=step=>SelectSection((currentSection+step+pages.Count)%pages.Count);
        _openSettingsPage=name=>{int index=pages.FindIndex(p=>p.Name==name);if(index>=0)SelectSection(index);};
    }

    private Action<string>? _openSettingsPage; // selects a settings page by name (e.g. from a controller notice)

    private Button? _railUpdateButton;

    // Update notice: a few seconds after the window opens, ask GitHub (in the background) whether a newer
    // release than this Project-Syndicate-vX.exe exists. If so, the update button on the rail turns gold and
    // the status line says so. Nothing installs by itself: the player still presses the button.
    private async System.Threading.Tasks.Task CheckForUpdateNotice()
    {
        try
        {
            var exe=Environment.GetEnvironmentVariable("TILTED_STANDALONE_EXE");
            if(string.IsNullOrWhiteSpace(exe) || !System.IO.File.Exists(exe))return;   // built on this PC: no releases
            var info=System.Diagnostics.FileVersionInfo.GetVersionInfo(exe);
            if(!Version.TryParse((info.ProductVersion ?? info.FileVersion ?? "").Split('+','-',' ')[0], out var current))return;
            // The release repo is baked into the standalone EXE's updater; updater.json can override it, but
            // when it is missing fall back to the public release repo so the notice still works.
            var repo="Sacttt/ProjectTilted";
            var config=System.IO.Path.Combine(AppContext.BaseDirectory,"updater.json");
            if(System.IO.File.Exists(config))
            {
                using var settings=System.Text.Json.JsonDocument.Parse(System.IO.File.ReadAllText(config));
                var configured=settings.RootElement.TryGetProperty("repository",out var r)?r.GetString():null;
                if(!string.IsNullOrWhiteSpace(configured))repo=configured;
            }
            if(!System.Text.RegularExpressions.Regex.IsMatch(repo,"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$"))return;
            await System.Threading.Tasks.Task.Delay(4000);
            using var http=new System.Net.Http.HttpClient{Timeout=TimeSpan.FromSeconds(8)};
            http.DefaultRequestHeaders.UserAgent.ParseAdd("SyndicateClassic-Launcher/"+current);
            http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
            using var release=System.Text.Json.JsonDocument.Parse(await http.GetStringAsync($"https://api.github.com/repos/{repo}/releases/latest"));
            var tag=release.RootElement.GetProperty("tag_name").GetString() ?? "";
            if(!Version.TryParse(tag.TrimStart('v','V'),out var latest) || latest<=current)return;
            if(_railUpdateButton!=null){_railUpdateButton.Content=GlyphIcon("",14,Night.Gold);_railUpdateButton.ToolTip=$"Update {tag} is available. Click to install it (accounts and settings stay saved).";}
            SetStatus($"Syndicate {tag} is out. Press the gold update button at the bottom left to install it.");
        }
        catch { /* offline or GitHub unavailable: the button still works by hand */ }
    }

    // Check for updates (no automatic updates any more): the public Syndicate EXE is restarted with
    // --check-updates. It installs a newer version if there is one, then opens Syndicate again.
    private void CheckForUpdates(Action<string> say)
    {
        var exe=Environment.GetEnvironmentVariable("TILTED_STANDALONE_EXE");
        if(string.IsNullOrWhiteSpace(exe) || !System.IO.File.Exists(exe)){say("Update checks work in the Syndicate download (Project-Syndicate-vX.exe). This launcher was built on this PC, so update it with BUILD LAUNCHER.");return;}
        if(_sessionStarted || _busy || _authBusy || _download!=null){say(_download!=null?"Wait for the download to finish, then check for updates.":"Quit Fortnite first, then check for updates.");return;}
        var answer=MessageBox.Show(this,"Syndicate will close, check for a new version and open again. Your accounts and settings stay saved.\n\nCheck now?","Check for updates",MessageBoxButton.OKCancel,MessageBoxImage.Question);
        if(answer!=MessageBoxResult.OK)return;
        try{
            var start=new System.Diagnostics.ProcessStartInfo(exe){UseShellExecute=false,WorkingDirectory=System.IO.Path.GetDirectoryName(exe)!};
            start.ArgumentList.Add("--check-updates");
            // Start the check only once Syndicate has fully closed (closing stops the local services first),
            // so the update check doesn't find Syndicate still running.
            Closed+=(_,__)=>{try{System.Diagnostics.Process.Start(start);}catch{}};
            Close();
        }catch(Exception error){say("Couldn't start the update check: "+error.Message);}
    }

    // About: version, data folders, update check.
    private void AddAboutSettings(Panel group)
    {
        string data=System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"ProjectTilted");
        group.Children.Add(NightText("Syndicate "+LauncherVersion()+"  ·  .NET "+Environment.Version.ToString(3)+"  ·  "+(Environment.Is64BitProcess?"x64":"x86"),13.5,Night.Text,bold:true));
        group.Children.Add(NightText("Data: "+data,12,Night.Sub).Also(t=>{t.FontFamily=NightMono;t.Margin=new Thickness(0,6,0,16);}));
        var row=new WrapPanel();group.Children.Add(row);
        var note=NightText("",12,Night.Sub);note.Margin=new Thickness(0,12,0,0);note.Visibility=Visibility.Collapsed;group.Children.Add(note);
        void Say(string text){note.Text=text;note.Visibility=Visibility.Visible;}
        void Open(string folder){
            try{System.IO.Directory.CreateDirectory(folder);System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo{FileName=folder,UseShellExecute=true});}
            catch(Exception error){Say("Could not open "+folder+": "+error.Message);}
        }
        var check=new Button{Content="Check for updates",Height=38,Margin=new Thickness(0,0,8,8)};NightButton(check,"base");
        check.Click+=(_,__)=>CheckForUpdates(Say);
        var logs=new Button{Content="Open logs folder",Height=38,Margin=new Thickness(0,0,8,8)};NightButton(logs,"ghost");
        logs.Click+=(_,__)=>{if(_paths==null){Say("Choose a game folder first; the logs live next to Syndicate's local services.");return;}Open(System.IO.Path.Combine(_paths.Root,"Runtime","Server"));};
        var folder=new Button{Content="Open data folder",Height=38,Margin=new Thickness(0,0,8,8)};NightButton(folder,"ghost");
        folder.Click+=(_,__)=>Open(data);
        row.Children.Add(check);row.Children.Add(logs);row.Children.Add(folder);
    }
}