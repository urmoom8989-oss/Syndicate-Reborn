// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
namespace SyndicateClassic;
public partial class MainWindow
{
    private static bool MotionEnabled => SystemParameters.ClientAreaAnimation && !SystemParameters.HighContrast;
    private static void AnimateValue(Animatable target,DependencyProperty property,double value,int milliseconds=160)
    {
        if(!MotionEnabled){target.BeginAnimation(property,null);target.SetValue(property,value);return;}
        target.BeginAnimation(property,new DoubleAnimation { To=value,Duration=TimeSpan.FromMilliseconds(milliseconds),EasingFunction=new CubicEase { EasingMode=EasingMode.EaseOut } });
    }
    private static void AddButtonMotion(Button button)
    {
        if(button.ReadLocalValue(MotionAttachedProperty) is bool attached && attached)return;
        button.SetValue(MotionAttachedProperty,true);
        var scale=new ScaleTransform(1,1);button.RenderTransformOrigin=new Point(.5,.5);button.RenderTransform=scale;
        void State(bool pressed=false){double size=!button.IsEnabled?1:pressed?.97:button.IsMouseOver?1.025:1;AnimateValue(scale,ScaleTransform.ScaleXProperty,size,pressed?70:150);AnimateValue(scale,ScaleTransform.ScaleYProperty,size,pressed?70:150);}
        button.MouseEnter+=(_,__)=>State();button.MouseLeave+=(_,__)=>State();
        button.PreviewMouseLeftButtonDown+=(_,__)=>State(true);button.PreviewMouseLeftButtonUp+=(_,__)=>State();
        button.LostMouseCapture+=(_,__)=>State();button.IsEnabledChanged+=(_,__)=>State();
        button.PreviewKeyDown+=(_,e)=>{if(e.Key==Key.Space || e.Key==Key.Enter)State(true);};
        button.PreviewKeyUp+=(_,__)=>State();button.LostKeyboardFocus+=(_,__)=>State();
    }
    private static readonly DependencyProperty MotionAttachedProperty=DependencyProperty.RegisterAttached("MotionAttached",typeof(bool),typeof(MainWindow),new PropertyMetadata(false));
    private static void AddCardMotion(Border card,bool available,int index)
    {
        var offset=new TranslateTransform();card.RenderTransform=offset;
        card.MouseEnter+=(_,__)=>{if(available)AnimateValue(offset,TranslateTransform.YProperty,-4,180);};
        card.MouseLeave+=(_,__)=>AnimateValue(offset,TranslateTransform.YProperty,0,200);
        bool entered=false;
        card.Loaded+=(_,__)=>{
            if(entered || !MotionEnabled)return;entered=true;
            card.BeginAnimation(OpacityProperty,new DoubleAnimation(0,1,TimeSpan.FromMilliseconds(280)){BeginTime=TimeSpan.FromMilliseconds(index*24),FillBehavior=FillBehavior.Stop});
        };
    }
    private static void AddPanelMotion(FrameworkElement panel)
    {
        panel.IsVisibleChanged+=(_,__)=>{
            if(!panel.IsVisible || !MotionEnabled)return;
            panel.BeginAnimation(OpacityProperty,new DoubleAnimation(.25,1,TimeSpan.FromMilliseconds(180)){EasingFunction=new QuadraticEase{EasingMode=EasingMode.EaseOut},FillBehavior=FillBehavior.Stop});
        };
    }
    private void InitializeLauncherMotion()
    {
        AddPanelMotion(AuthOverlay);AddPanelMotion(SettingsPanel);AddPanelMotion(PassShop);
        void Visit(DependencyObject parent){for(int i=0;i<VisualTreeHelper.GetChildrenCount(parent);i++){var child=VisualTreeHelper.GetChild(parent,i);if(child is Button button && button.RenderTransform==Transform.Identity)AddButtonMotion(button);Visit(child);}}
        Loaded+=(_,__)=>Visit(this);
    }
}