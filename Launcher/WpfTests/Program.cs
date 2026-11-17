using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Diagnostics;
using SyndicateLauncher;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;
using SyndicateLauncher.ViewModels;
internal static class Program
{
 [STAThread] static int Main(string[] args)
 {
  int checks=0;void Check(bool b,string text){if(!b)throw new Exception(text);checks++;}
  string temp=Path.Combine(Path.GetTempPath(),"SyndicateDifficultyUi-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(temp);
  var errors=new StringWriter();PresentationTraceSources.DataBindingSource.Listeners.Add(new TextWriterTraceListener(errors));PresentationTraceSources.DataBindingSource.Switch.Level=SourceLevels.Error;
  var app=new App();app.InitializeComponent();
  try {
   var store=new SettingsService(temp);var library=new PresetStore(Path.Combine(temp,"Presets"));
   var vm=new MainViewModel(store,library);Check(vm.SkillSliders.Count==20&&vm.DifficultyPresets.Count==6,"Six built-ins and twenty controls.");
   for(int level=0;level<6;level++){vm.SetDifficultyCommand.Execute(level.ToString());Check(vm.DifficultyName==BotPreset.Levels[level]&&vm.DifficultyIndex==level,"Difficulty slider selects the corresponding built-in.");}
   Check(store.Load().BotPreset!.Mixed,"Mixed is saved.");
   vm.SkillSliders[0].Value=17;Check(vm.DifficultyName=="Custom"&&!store.Load().BotPreset!.Mixed,"Mixed slider edit becomes a fixed custom preset.");
   Check(vm.SkillSliders[0].Value==17&&vm.SkillSliders[1].Value==50,"Independent slider change.");
   vm.PresetName="Shared profile";vm.SavePresetCommand.Execute(null);Check(vm.DifficultyName=="Shared profile"&&library.Load().Single().Values["aimAccuracy"]==17,"Save preset command.");
   var restored=new MainViewModel(store,library);Check(restored.SkillSliders[0].Value==17&&restored.DifficultyName=="Shared profile","Custom settings survive restart.");
   restored.SetDifficultyCommand.Execute("4");restored=new MainViewModel(store,library);Check(restored.DifficultyIndex==4&&restored.DifficultyName=="Sweat","Built-in slider position survives restart.");
   restored.DifficultyIndex=5;Check(store.Load().BotPreset!.Mixed,"Slider chooses Mixed.");
   restored.DifficultyIndex=2;restored.IsSettingsOpen=true;
   var window=new MainWindow(restored);var root=(FrameworkElement)window.Content;
   root.Measure(new Size(996,576));root.Arrange(new Rect(0,0,996,576));root.UpdateLayout();
   foreach(var e in Descendants(root).OfType<FrameworkElement>()){e.BeginAnimation(UIElement.OpacityProperty,null);if(e.RenderTransform is TranslateTransform t)t.BeginAnimation(TranslateTransform.XProperty,null);}
   var scroll=Descendants(root).OfType<ScrollViewer>().First(s=>s.Content is StackPanel);
   var label=Descendants(root).OfType<TextBlock>().First(t=>t.Text=="Bot difficulty");
   var point=label.TransformToAncestor((Visual)scroll.Content).Transform(new Point());scroll.ScrollToVerticalOffset(point.Y);root.UpdateLayout();
   Directory.CreateDirectory(args[0]);Render(root,Path.Combine(args[0],"ui-difficulty.png"));
   var expander=Descendants(root).OfType<Expander>().Single();expander.IsExpanded=true;root.UpdateLayout();
   var items=Descendants(expander).OfType<ItemsControl>().First();items.Measure(new Size(450,double.PositiveInfinity));items.Arrange(new Rect(new Point(),items.DesiredSize));items.UpdateLayout();
   Check(Descendants(items).OfType<Slider>().Count()==20,"All twenty visible sliders have WPF templates.");
   Render(items,Path.Combine(args[0],"ui-all-skills.png"));
   Check(errors.ToString().Length==0,"WPF binding errors: "+errors);
   Console.WriteLine($"PASS: {checks} WPF difficulty, persistence, preset-command and offscreen rendering checks; no window shown or game launched.");
   window.Close();return 0;
  }finally{app.Shutdown();Directory.Delete(temp,true);}
 }
 static IEnumerable<DependencyObject> Descendants(DependencyObject parent)
 {
  for(int i=0;i<VisualTreeHelper.GetChildrenCount(parent);i++){var child=VisualTreeHelper.GetChild(parent,i);yield return child;foreach(var next in Descendants(child))yield return next;}
 }
 static void Render(FrameworkElement e,string path)
 {
  int width=(int)Math.Ceiling(e.ActualWidth),height=(int)Math.Ceiling(e.ActualHeight);
  var image=new RenderTargetBitmap(width,height,96,96,PixelFormats.Pbgra32);image.Render(e);
  var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(image));using var file=File.Create(path);encoder.Save(file);
 }
}

