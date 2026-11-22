using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using SyndicateLauncher.ViewModels;

namespace SyndicateLauncher.Views;

public partial class NewsEditorView : UserControl
{
    public NewsEditorView() => InitializeComponent();

    private void OnIsVisibleChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        if (IsVisible) Dispatcher.BeginInvoke(new Action(() => Focus()));
    }

    private void OnKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Escape || DataContext is not MainViewModel vm) return;
        vm.CloseNewsEditorCommand.Execute(null);
        e.Handled = true;
    }
}
