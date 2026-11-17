// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
namespace SyndicateClassic;
public partial class MainWindow
{
    private readonly System.Collections.Generic.List<Button> _seasonLaunchButtons=new();
    private readonly System.Collections.Generic.List<Button> _seasonFolderButtons=new();
    private static SolidColorBrush Ink(string hex)=>new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex switch { "#101615"=>"#05060D", "#17211C"=>"#0D1020", "#15211A"=>"#0D1020", "#3A5142"=>"#22284A", "#1D2D23"=>"#141831", "#101C14"=>"#0D1020", "#0F1B13"=>"#0D1020", "#B1E9C9"=>"#FFD23F", "#10251A"=>"#1A1405", "#293C31"=>"#141831", "#DFF8E8"=>"#F5F7FF", "#A7BEAF"=>"#A3ABCB", "#293D31"=>"#1A1F3D", "#CFE6D7"=>"#E6EAF8", "#8FAC98"=>"#A3ABCB", "#EDF7F0"=>"#F5F7FF", "#E5F3EA"=>"#F5F7FF", "#99B1A1"=>"#A3ABCB", "#EBF5EE"=>"#F5F7FF", "#506C59"=>"#22284A", "#D9E8DF"=>"#DDE2F5", "#9DB6A6"=>"#A3ABCB", "#A6C1AF"=>"#A3ABCB", "#2B4032"=>"#141831", "#BED6C6"=>"#C9CFE6", "#8FA999"=>"#8A92B5", "#8DA69D"=>"#FFD23F", "#A0ADA7"=>"#A5B1CB", "#25312D"=>"#2C3150", "#DBE8E0"=>"#E3E8FA", "#81958B"=>"#95A4C3", "#E8EFEA"=>"#E8EDFA", "#34463D"=>"#36425D", "#1A2420"=>"#182033", "#17261F"=>"#1B243A", "#B9DFC9"=>"#D5DFFE", "#92AD9E"=>"#9EACCB", "#A6B8AC"=>"#AAB5CE", "#23322A"=>"#26314A", "#B8CDBF"=>"#CED8EF", "#CAD8CE"=>"#CBD5EB", "#B4C5BA"=>"#B7C4DE", "#788E80"=>"#8A9AB9", "#2A372F"=>"#29334A", "#151E19"=>"#131B2B", "#202E26"=>"#26314A", "#B8CCBF"=>"#CBD5EB", "#C5DACC"=>"#CDD7EE", "#B7CCBE"=>"#BDCAE4", "#213429"=>"#26314A", "#819C8B"=>"#91A2C2", "#43372A"=>"#3A1E26", "#E8D4B4"=>"#FFB4BC", _=>hex }));
    private static TextBlock Label(string text,double size,string color)=>new TextBlock {Text=text,FontSize=size,Foreground=Ink(color),FontFamily=new FontFamily("Segoe UI")};
    private static void PolishButton(Button button,string background,string foreground)
    {
        button.Background=Ink(background);button.Foreground=Ink(foreground);button.BorderThickness=new Thickness(0);button.FontFamily=new FontFamily("Segoe UI");button.FontSize=12.5;button.FontWeight=FontWeights.SemiBold;if(button.ReadLocalValue(PaddingProperty)==DependencyProperty.UnsetValue)button.Padding=new Thickness(16,6,16,6);button.Cursor=System.Windows.Input.Cursors.Hand;
        var border=new FrameworkElementFactory(typeof(Border));border.SetValue(Border.CornerRadiusProperty,TiltCorners(9));border.SetBinding(Border.BackgroundProperty,new System.Windows.Data.Binding("Background"){RelativeSource=new System.Windows.Data.RelativeSource(System.Windows.Data.RelativeSourceMode.TemplatedParent)});
        var content=new FrameworkElementFactory(typeof(ContentPresenter));content.SetValue(HorizontalAlignmentProperty,HorizontalAlignment.Center);content.SetValue(VerticalAlignmentProperty,VerticalAlignment.Center);content.SetValue(MarginProperty,new TemplateBindingExtension(PaddingProperty));border.AppendChild(content);   // Padding was ignored, so short buttons (Redeem) squeezed their text
        var template=new ControlTemplate(typeof(Button)){VisualTree=border};
        var hover=new Trigger{Property=IsMouseOverProperty,Value=true};hover.Setters.Add(new Setter(OpacityProperty,0.83));template.Triggers.Add(hover);
        var disabled=new Trigger{Property=IsEnabledProperty,Value=false};disabled.Setters.Add(new Setter(OpacityProperty,0.45));template.Triggers.Add(disabled);button.Template=template;AddButtonMotion(button);
    }
    // Every season picture is decoded once per launcher run: the hidden classic grid, the home screen,
    // the browser cards and the pass pages all ask for the same pictures, and full-size decodes on the
    // UI thread were most of the time before the main window appeared.
    private static readonly System.Collections.Generic.Dictionary<string, ImageSource> _decodedArt = new();
    private static T Cached<T>(string key, Func<T> load) where T : ImageSource
    {
        lock (_decodedArt) { if (_decodedArt.TryGetValue(key, out var hit) && hit is T typed) return typed; }
        var image = load();
        lock (_decodedArt) _decodedArt[key] = image;
        return image;
    }
    private static BitmapImage SeasonCardImage(int season) => Cached("card" + season, () => LoadSeasonCardImage(season));
    // Last card in the season grid: Chapter 2 Season 2 (12).
    private const int LastGridSeason = 12;
    private static ImageSource SeasonArtImage(int season) => Cached("art" + season, () => LoadSeasonArtImage(season));
    private static BitmapImage LoadSeasonCardImage(int season)
    {
        using var stream=typeof(MainWindow).Assembly.GetManifestResourceStream("SyndicateClassic.SeasonCard"+season) ?? throw new InvalidOperationException("Missing season artwork");
        var image=new BitmapImage();image.BeginInit();image.CacheOption=BitmapCacheOption.OnLoad;image.StreamSource=stream;image.EndInit();image.Freeze();return image;
    }
    // Season 6+ art: the season's own loading-screen art from its game folder (FortniteGame\Content\Splash\Splash.bmp)
    // when that folder is set, otherwise the card shipped with the launcher, otherwise Season 5's.
    private static ImageSource LoadSeasonArtImage(int season)
    {
        if (season >= 2 && season < 6) return SeasonCardImage(5);
        // Pre-season and Season 1: your own picture (season-art\season0.png / season1.png) wins over the game's splash.
        if (season < 2 || season == 8 || season == 11 || season == 12) // Season 8 too: the game's own splash was cropped badly. Chapter 2 Season 1 (11) and 2 (12): the Chapter 2 key art
            try
            {
                using var custom = typeof(MainWindow).Assembly.GetManifestResourceStream("SyndicateClassic.SeasonCard" + season);
                if (custom != null)
                {
                    var picture = new BitmapImage(); picture.BeginInit(); picture.CacheOption = BitmapCacheOption.OnLoad; picture.StreamSource = custom; picture.EndInit(); picture.Freeze(); return picture;
                }
            }
            catch { }
        try
        {
            string build = season switch { 0 => "1.7.2", 1 => "1.8.2", 6 => "6.20", 7 => "7.40", 8 => "8.50", 9 => "9.40", 11 => "11.00", 12 => "12.50", 19 => "19.00", _ => "10.31" };
            var folder = Launcher.GameFolderFor(build);
            var splash = folder == null ? null : System.IO.Path.Combine(folder, "FortniteGame", "Content", "Splash", "Splash.bmp");
            if (splash != null && System.IO.File.Exists(splash))
            {
                var image = new BitmapImage(); image.BeginInit(); image.CacheOption = BitmapCacheOption.OnLoad; image.DecodePixelWidth = 1600; /* splash BMPs are huge; 1600 px is plenty for the launcher */ image.UriSource = new Uri(splash); image.EndInit(); image.Freeze(); return image;
            }
        }
        catch { }
        try
        {
            using var stream = typeof(MainWindow).Assembly.GetManifestResourceStream("SyndicateClassic.SeasonCard" + season);
            if (stream != null)
            {
                var image = new BitmapImage(); image.BeginInit(); image.CacheOption = BitmapCacheOption.OnLoad; image.StreamSource = stream; image.EndInit(); image.Freeze(); return image;
            }
        }
        catch { }
        if (season < 2)
        {
            // Until the folder is chosen, the pre-season and Season 1 borrow the Season 2 key art.
            var fallback = new BitmapImage(new Uri("pack://application:,,,/SyndicateLauncher;component/Assets/Fortnitebp.png")); fallback.Freeze(); return fallback;
        }
        return SeasonCardImage(5);
    }

    // Loading-screen art is portrait; show the band with the characters' faces.
    private static Rect SeasonArtViewbox(int season, ImageSource image) =>
        (season >= 6 || season < 2) && image is BitmapSource bitmap && bitmap.PixelHeight > bitmap.PixelWidth * 0.9 ? new Rect(0, .18, 1, .53) : new Rect(0, 0, 1, 1);

    private static Style DarkScrollBar()
    {
        return (Style)System.Windows.Markup.XamlReader.Parse(@"<Style xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' TargetType='ScrollBar'><Setter Property='Width' Value='8'/><Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ScrollBar'><Track x:Name='PART_Track' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' Orientation='Vertical' IsDirectionReversed='True'><Track.Thumb><Thumb><Thumb.Template><ControlTemplate TargetType='Thumb'><Border Background='#26324D' CornerRadius='4'/></ControlTemplate></Thumb.Template></Thumb></Track.Thumb></Track></ControlTemplate></Setter.Value></Setter></Style>");
    }
    private void BuildSeasonGrid()
    {
        var hero=(Grid)BackgroundHost.Parent;var page=(Grid)hero.Parent;var news=(Canvas)ClassicArt.Parent;
        void Detach(FrameworkElement element){if(element.Parent is Panel panel)panel.Children.Remove(element);element.ClearValue(WidthProperty);element.ClearValue(HeightProperty);element.Margin=new Thickness(0);element.HorizontalAlignment=HorizontalAlignment.Stretch;element.VerticalAlignment=VerticalAlignment.Stretch;}
        Detach(LaunchControls);Detach(DownloadPanel);Detach(StatusText);Detach(LogToggleButton);page.Children.Remove(hero);page.Children.Remove(news);
        page.Background=new LinearGradientBrush(Ink("#0B1020").Color,Ink("#17192D").Color,25);
        var layout=new Grid{Name="SeasonLibrary",Margin=new Thickness(32,16,32,20)};Grid.SetRow(layout,1);Grid.SetRowSpan(layout,2);
        layout.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});layout.RowDefinitions.Add(new RowDefinition());layout.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});
        var heading=new Grid{Margin=new Thickness(4,0,4,22)};heading.ColumnDefinitions.Add(new ColumnDefinition());heading.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});layout.Children.Add(heading);
        var words=new StackPanel();heading.Children.Add(words);
        var eyebrow=Label("CHAPTER 1",10,"#8DA69D");eyebrow.FontWeight=FontWeights.SemiBold;words.Children.Add(eyebrow);
        var title=Label("Library",32,"#F2F6F3");title.FontWeight=FontWeights.SemiBold;title.Margin=new Thickness(0,6,0,7);words.Children.Add(title);
        words.Children.Add(Label("Choose a season, set its game folder once, then press Launch.",13,"#A0ADA7"));
        var settings=new Button{Content="Settings",Width=110,Height=36,VerticalAlignment=VerticalAlignment.Center,ToolTip="Bot difficulty, match settings and launcher preferences"};PolishButton(settings,"#25312D","#DBE8E0");settings.Click+=SettingsButton_Click;Grid.SetColumn(settings,1);heading.Children.Add(settings);
        var scroll=new ScrollViewer{VerticalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled,Padding=new Thickness(0,5,0,0)};scroll.Resources.Add(typeof(System.Windows.Controls.Primitives.ScrollBar),DarkScrollBar());Grid.SetRow(scroll,1);layout.Children.Add(scroll);
        var library=new StackPanel();scroll.Content=library;
        var section=new DockPanel{Margin=new Thickness(4,0,4,12)};library.Children.Add(section);
        var count=Label(LastGridSeason+1+" SEASONS",10,"#81958B");count.HorizontalAlignment=HorizontalAlignment.Right;DockPanel.SetDock(count,Dock.Right);section.Children.Add(count);
        var availableLabel=Label("Ready to play",15,"#E8EFEA");availableLabel.FontWeight=FontWeights.SemiBold;section.Children.Add(availableLabel);
        var cards=new Grid{Name="SeasonCards"};library.Children.Add(cards);for(int c=0;c<=LastGridSeason;c++)cards.ColumnDefinitions.Add(new ColumnDefinition());
        string[] names={"Before the seasons","The first season","The first battle pass","Out of this world","Brace for impact","Worlds collide","Darkness rises","You better watch out","X marks the spot","Future is yours","Out of time","A new world","Top secret"};
        for(int season=0;season<=LastGridSeason;season++){
            string accent=season switch{0=>"#B388FF",1=>"#D9A06B",2=>"#EBC578",3=>"#FF8A4C",4=>"#8EC5FF",5=>"#FFACB9",6=>"#B98CFF",7=>"#9FE3FF",8=>"#FFB36B",9=>"#7DF0E0",11=>"#6FA8FF",12=>"#F2B135",_=>"#FFD86B"};
            string build=season switch{0=>"1.7.2",1=>"1.8.2",2=>"1.11",3=>"3.5.1",4=>"4.5",5=>"5.10",6=>"6.20",7=>"7.40",8=>"8.50",9=>"9.40",11=>"11.00",12=>"12.50",_=>"10.31"};
            var card=new Border{Name="Season"+season+"Card",Margin=new Thickness(4,0,8,8),CornerRadius=new CornerRadius(14),BorderThickness=new Thickness(1),BorderBrush=Ink("#34463D"),Background=Ink("#1A2420"),ClipToBounds=true};Grid.SetColumn(card,season);cards.Children.Add(card);AddCardMotion(card,true,season);
            var stack=new StackPanel();card.Child=stack;
            ImageSource image=season>=6 || season<2?SeasonArtImage(season):season==3 || season==5?SeasonCardImage(season):new BitmapImage(new Uri(season==2?"pack://application:,,,/SyndicateLauncher;component/Assets/Fortnitebp.png":"pack://application:,,,/SyndicateLauncher;component/Assets/Season4Reference.png"));
            var art=new Grid{Height=140,Background=new ImageBrush(image){Stretch=Stretch.UniformToFill,AlignmentY=AlignmentY.Top,Viewbox=season==2?new Rect(.18,0,.74,.84):season==4?new Rect(.38,.04,.56,.65):SeasonArtViewbox(season,image),ViewboxUnits=BrushMappingMode.RelativeToBoundingBox}};stack.Children.Add(art);
            art.Children.Add(new Border{Background=new LinearGradientBrush(Color.FromArgb(0,13,18,32),Color.FromArgb(220,13,18,32),90)});
            var badge=new Border{HorizontalAlignment=HorizontalAlignment.Left,VerticalAlignment=VerticalAlignment.Top,Margin=new Thickness(12),Padding=new Thickness(8,4,8,4),CornerRadius=new CornerRadius(5),Background=Ink("#17261F"),Child=Label(season>=11?"CHAPTER 02":"CHAPTER 01",9,"#B9DFC9")};art.Children.Add(badge);
            var number=Label(season>=11?"0"+(season-10):season>=10?"X":season==0?"P":"0"+season,58,"#F4FAF6");number.FontWeight=FontWeights.Bold;number.VerticalAlignment=VerticalAlignment.Bottom;number.Margin=new Thickness(14,0,0,4);art.Children.Add(number);
            stack.Children.Add(new Border{Height=3,Background=Ink(accent)});
            var body=new StackPanel{Margin=new Thickness(14,15,14,16)};stack.Children.Add(body);
            var row=new DockPanel();body.Children.Add(row);var version=Label("v"+build,11,"#92AD9E");version.VerticalAlignment=VerticalAlignment.Center;DockPanel.SetDock(version,Dock.Right);row.Children.Add(version);
            var label=Label(season>=11?"C2 Season "+(season-10):season>=10?"Season X":season==0?"Pre-season":"Season "+season,20,"#F1F6F2");label.FontWeight=FontWeights.SemiBold;row.Children.Add(label);
            var tagline=Label(names[season],11,"#A6B8AC");tagline.Margin=new Thickness(0,6,0,14);tagline.TextTrimming=TextTrimming.CharacterEllipsis;body.Children.Add(tagline);
            if(season==4){
                LaunchButton.Tag=build;LaunchControls.Height=42;PolishButton(LaunchButton,accent,"#10251A");LaunchButton.FontSize=14;LaunchButton.Margin=new Thickness(0);
                foreach(var button in LaunchControls.Children)if(button is Button settingsButton && settingsButton!=LaunchButton)settingsButton.Visibility=Visibility.Collapsed;
                Grid.SetColumn(LaunchButton,0);Grid.SetColumnSpan(LaunchButton,Math.Max(1,LaunchControls.ColumnDefinitions.Count));body.Children.Add(LaunchControls);body.Children.Add(DownloadPanel);
            }else{
                var launch=new Button{Name="LaunchSeason"+season,Tag=build,Content="Launch",Height=42};PolishButton(launch,accent,"#10251A");launch.FontSize=14;launch.Click+=LaunchButton_Click;body.Children.Add(launch);_seasonLaunchButtons.Add(launch);
            }
            var choose=new Button{Name="ChooseSeason"+season+"Folder",Content="Choose game folder",Height=32,Margin=new Thickness(0,7,0,0),ToolTip="Select your extracted Fortnite "+build+" folder"};PolishButton(choose,"#23322A","#B8CDBF");choose.FontSize=11;choose.Click+=async(_,__)=>await PickFolderAsync(expectedBuild:build);body.Children.Add(choose);_seasonFolderButtons.Add(choose);
        }
#if TILTED_DEVKIT
        // Chapter 3 Season 1 (19.00) is experimental and has no grid card, but it still needs its own launch
        // and folder buttons registered so the hero PLAY / LINK FOLDER act on 19.00 instead of falling back to
        // season 4's LaunchButton. These live off-screen; the hero controls RaiseEvent them.
        var c3Launch=new Button{Name="LaunchSeason19",Tag="19.00",Content="Launch",Visibility=Visibility.Collapsed};c3Launch.Click+=LaunchButton_Click;_seasonLaunchButtons.Add(c3Launch);
        var c3Folder=new Button{Name="ChooseSeason19Folder",Content="Choose game folder",Visibility=Visibility.Collapsed};c3Folder.Click+=async(_,__)=>await PickFolderAsync(expectedBuild:"19.00");_seasonFolderButtons.Add(c3Folder);
#endif
        var archiveTitle=Label("More from Chapter One",14,"#CAD8CE");archiveTitle.Margin=new Thickness(4,16,0,10);library.Children.Add(archiveTitle);
        var archive=new System.Windows.Controls.Primitives.UniformGrid{Columns=5};library.Children.Add(archive);archiveTitle.Visibility=Visibility.Collapsed; // Seasons 6-10 are playable now
        foreach(int season in System.Array.Empty<int>()){
            var content=new StackPanel{Margin=new Thickness(12,13,10,13)};var label=Label("Season "+season,13,"#B4C5BA");label.FontWeight=FontWeights.SemiBold;content.Children.Add(label);
            var state=Label(season==1?"Temporarily disabled":"Coming soon",10,season==1?"#CCB890":"#788E80");state.Margin=new Thickness(0,7,0,0);state.TextWrapping=TextWrapping.Wrap;content.Children.Add(state);
            archive.Children.Add(new Border{Margin=new Thickness(4,0,8,8),CornerRadius=new CornerRadius(9),BorderBrush=Ink("#2A372F"),BorderThickness=new Thickness(1),Background=Ink("#151E19"),Child=content,ToolTip=season==1?"Fortnite 1.8.2 is unavailable while matchmaking is repaired.":"This season is not available yet."});
        }
        var footer=new DockPanel{Margin=new Thickness(4,18,4,0),LastChildFill=true};Grid.SetRow(footer,2);layout.Children.Add(footer);LogToggleButton.Width=100;LogToggleButton.Height=30;PolishButton(LogToggleButton,"#202E26","#B8CCBF");DockPanel.SetDock(LogToggleButton,Dock.Right);footer.Children.Add(LogToggleButton);StatusText.FontFamily=new FontFamily("Segoe UI");StatusText.FontSize=11;StatusText.TextWrapping=TextWrapping.Wrap;StatusText.VerticalAlignment=VerticalAlignment.Center;StatusText.Margin=new Thickness(0,0,18,0);footer.Children.Add(StatusText);
        page.Children.Insert(page.Children.IndexOf(PassShop),layout);
    }
}