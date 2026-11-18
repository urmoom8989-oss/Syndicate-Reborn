using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using SyndicateLauncher.ViewModels;

namespace SyndicateLauncher.Views;

/// <summary>
/// The lobby editor's drag and drop. A tile is picked up after the mouse moves a few pixels; dropping it on a row puts it
/// at that place in the row (MainViewModel.PlaceLobbyTile), dropping it on "Not in the lobby" turns it off
/// (MainViewModel.TakeLobbyTileOut). The view model runs after the drop has finished, so the tile under the mouse is
/// never rebuilt mid-drag.
/// </summary>
public partial class LobbyEditorView : UserControl
{
    private readonly HashSet<Border> _marked = new();
    private GameModeToggle? _pressed;
    private Point _pressAt;

    public LobbyEditorView() => InitializeComponent();

    private MainViewModel? Vm => DataContext as MainViewModel;

    private void OnIsVisibleChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        if (IsVisible) Dispatcher.BeginInvoke(new Action(() => Focus()));
    }

    private void OnKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Escape || Vm is not { } vm) return;
        vm.CloseLobbyEditorCommand.Execute(null);
        e.Handled = true;
    }

    private void Tile_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (sender is not FrameworkElement { DataContext: GameModeToggle tile } element) return;
        _pressed = tile;
        _pressAt = e.GetPosition(this);
        Vm?.SelectLobbyTile(tile);
        element.Focus();
    }

    private void Tile_PreviewMouseLeftButtonUp(object sender, MouseButtonEventArgs e) => _pressed = null;

    private void Tile_PreviewMouseMove(object sender, MouseEventArgs e)
    {
        if (_pressed is null) return;
        if (e.LeftButton != MouseButtonState.Pressed) { _pressed = null; return; }
        Point now = e.GetPosition(this);
        if (Math.Abs(now.X - _pressAt.X) < SystemParameters.MinimumHorizontalDragDistance &&
            Math.Abs(now.Y - _pressAt.Y) < SystemParameters.MinimumVerticalDragDistance) return;
        var tile = _pressed;
        _pressed = null;
        var source = (FrameworkElement)sender;
        double opacity = source.Opacity;
        source.Opacity = 0.4;
        try
        {
            DragDrop.DoDragDrop(source, new DataObject(typeof(GameModeToggle), tile), DragDropEffects.Move);
        }
        finally
        {
            source.Opacity = opacity;
            ClearMarks();
        }
    }

    private void Tile_KeyDown(object sender, KeyEventArgs e)
    {
        if ((e.Key == Key.Enter || e.Key == Key.Space) && sender is FrameworkElement { DataContext: GameModeToggle tile })
        {
            Vm?.SelectLobbyTile(tile);
            e.Handled = true;
        }
    }

    private static GameModeToggle? Dragged(DragEventArgs e) =>
        e.Data.GetDataPresent(typeof(GameModeToggle)) ? e.Data.GetData(typeof(GameModeToggle)) as GameModeToggle : null;

    private void Row_DragOver(object sender, DragEventArgs e)
    {
        bool ok = Dragged(e) is not null && sender is Border;
        e.Effects = ok ? DragDropEffects.Move : DragDropEffects.None;
        if (ok) Mark((Border)sender);
        e.Handled = true;
    }

    private void Row_Drop(object sender, DragEventArgs e)
    {
        e.Handled = true;
        ClearMarks();
        if (sender is not Border { DataContext: LobbyRow row } border || Dragged(e) is not { } tile || Vm is not { } vm) return;
        int index = DropIndex(border, e);
        Dispatcher.BeginInvoke(new Action(() => vm.PlaceLobbyTile(tile, row.Index, index)));
    }

    private void Tray_DragOver(object sender, DragEventArgs e)
    {
        bool ok = Dragged(e) is { IsOff: false } && sender is Border;
        e.Effects = ok ? DragDropEffects.Move : DragDropEffects.None;
        if (ok) Mark((Border)sender);
        e.Handled = true;
    }

    private void Tray_Drop(object sender, DragEventArgs e)
    {
        e.Handled = true;
        ClearMarks();
        if (Dragged(e) is not { IsOff: false } tile || Vm is not { } vm) return;
        Dispatcher.BeginInvoke(new Action(() => vm.TakeLobbyTileOut(tile)));
    }

    private void Drop_DragLeave(object sender, DragEventArgs e)
    {
        if (sender is Border border && !border.IsMouseOver) Unmark(border);
    }

    // The place in the row under the mouse, counted over the row's tiles as they are now (the dragged one included).
    private static int DropIndex(DependencyObject rowBorder, DragEventArgs e)
    {
        var list = FindChild<ItemsControl>(rowBorder);
        if (list is null) return -1;
        for (int i = 0; i < list.Items.Count; i++)
        {
            if (list.ItemContainerGenerator.ContainerFromIndex(i) is not FrameworkElement item) continue;
            Point p = e.GetPosition(item);
            if (p.Y < 0) return i;
            if (p.Y <= item.ActualHeight && p.X < item.ActualWidth / 2) return i;
        }
        return list.Items.Count;
    }

    private static T? FindChild<T>(DependencyObject parent) where T : DependencyObject
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            if (child is T match) return match;
            if (FindChild<T>(child) is { } deeper) return deeper;
        }
        return null;
    }

    private void Mark(Border border)
    {
        if (!_marked.Add(border)) return;
        border.SetResourceReference(Border.BorderBrushProperty, "AccentBrush");
        border.BorderThickness = new Thickness(2);
    }

    private void Unmark(Border border)
    {
        if (!_marked.Remove(border)) return;
        border.SetResourceReference(Border.BorderBrushProperty, "HairlineBrush");
        border.BorderThickness = new Thickness(1);
    }

    private void ClearMarks()
    {
        foreach (var border in new List<Border>(_marked)) Unmark(border);
    }
}
