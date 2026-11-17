// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
namespace SyndicateClassic;
public partial class MainWindow {
 private void RedesignAuthentication(){
  UpdateAuthMode();
  foreach(var control in new FrameworkElement[]{AuthHeading,AuthSubtitle,UsernamePanel,EmailBox,PasswordInput,ConfirmPanel,RememberEmail,AuthMessage,AuthSubmit,AuthSwitch,_resetPasswordButton!})ReleaseControl(control);
  var card=new Border{Width=470,MaxHeight=720,Padding=new Thickness(30),CornerRadius=new CornerRadius(18),Background=Ink("#15211A"),BorderBrush=Ink("#3A5142"),BorderThickness=new Thickness(1),HorizontalAlignment=HorizontalAlignment.Center,VerticalAlignment=VerticalAlignment.Center};
  AuthOverlay.Child=card;AuthOverlay.Padding=new Thickness(16);AuthOverlay.Background=new SolidColorBrush(Color.FromArgb(195,5,10,7));
  var scroll=new ScrollViewer{VerticalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled};scroll.Resources.Add(typeof(System.Windows.Controls.Primitives.ScrollBar),DarkScrollBar());card.Child=scroll;var body=new StackPanel();scroll.Content=body;
  var top=new DockPanel{Margin=new Thickness(0,0,0,24)};var close=new Button{Content="Close",Width=64,Height=30};PolishButton(close,"#293D31","#CFE6D7");close.Click+=CloseAuth_Click;DockPanel.SetDock(close,Dock.Right);top.Children.Add(close);var brand=Label("SYNDICATE / ACCOUNT",12,"#B1E9C9");brand.FontWeight=FontWeights.SemiBold;brand.VerticalAlignment=VerticalAlignment.Center;top.Children.Add(brand);body.Children.Add(top);
  AuthHeading.FontSize=27;AuthHeading.FontWeight=FontWeights.SemiBold;AuthHeading.Foreground=Ink("#EDF7F0");body.Children.Add(AuthHeading);AuthSubtitle.FontSize=13;AuthSubtitle.Foreground=Ink("#9DB6A6");AuthSubtitle.TextWrapping=TextWrapping.Wrap;AuthSubtitle.Margin=new Thickness(0,8,0,24);body.Children.Add(AuthSubtitle);
  void StyleInput(Control input){input.Height=42;input.Padding=new Thickness(11,8,11,8);input.Background=Ink("#0F1B13");input.Foreground=Ink("#EDF7F0");input.BorderBrush=Ink("#506C59");input.BorderThickness=new Thickness(1);input.FontSize=14;if(input is TextBox t)t.CaretBrush=Ink("#B1E9C9");if(input is PasswordBox p)p.CaretBrush=Ink("#B1E9C9");}
  foreach(var panel in new[]{UsernamePanel,ConfirmPanel}){panel.Margin=new Thickness(0,0,0,16);foreach(var text in SettingsTree(panel).OfType<TextBlock>()){text.Foreground=Ink("#C5DACC");text.FontSize=11;}foreach(var input in SettingsTree(panel).OfType<Control>().Where(x=>x is TextBox || x is PasswordBox))StyleInput(input);}
  body.Children.Add(UsernamePanel);
  void Field(string name,Control input){var label=Label(name,11,"#C5DACC");label.Margin=new Thickness(0,0,0,7);body.Children.Add(label);StyleInput(input);input.Margin=new Thickness(0,0,0,16);body.Children.Add(input);}
  Field("EMAIL ADDRESS",EmailBox);Field("PASSWORD",PasswordInput);body.Children.Add(ConfirmPanel);
  RememberEmail.Foreground=Ink("#B7CCBE");RememberEmail.Content="Keep me signed in";RememberEmail.Margin=new Thickness(0,0,0,12);body.Children.Add(RememberEmail);
  AuthMessage.Foreground=Ink("#F0CD86");AuthMessage.TextWrapping=TextWrapping.Wrap;AuthMessage.MinHeight=24;AuthMessage.Margin=new Thickness(0,0,0,10);body.Children.Add(AuthMessage);
  AuthSubmit.Height=44;PolishButton(AuthSubmit,"#B1E9C9","#10251A");body.Children.Add(AuthSubmit);AuthSwitch.Height=36;AuthSwitch.Margin=new Thickness(0,12,0,0);PolishButton(AuthSwitch,"#213429","#CFE6D7");body.Children.Add(AuthSwitch);_resetPasswordButton!.Height=32;_resetPasswordButton.Margin=new Thickness(0,6,0,0);PolishButton(_resetPasswordButton,"#15211A","#9DB6A6");body.Children.Add(_resetPasswordButton);
  var note=Label("Your local Syndicate account. Use a different password from your Epic Games account.",11,"#819C8B");note.TextWrapping=TextWrapping.Wrap;note.Margin=new Thickness(0,20,0,0);body.Children.Add(note);
 }
}