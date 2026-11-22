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

    // Dragging a tile follows the mouse with capture on the editor instead of Windows drag-and-drop: the OLE drag
    // never delivered its drop here, so no move was ever saved (settings.json kept the same order for days).
    private GameModeToggle? _dragging;
    private FrameworkElement? _dragSource;
    private double _dragOpacity = 1;

    private void Tile_PreviewMouseMove(object sender, MouseEventArgs e)
    {
        if (_pressed is null || _dragging is not null) return;
        if (e.LeftButton != MouseButtonState.Pressed) { _pressed = null; return; }
        Point now = e.GetPosition(this);
        if (Math.Abs(now.X - _pressAt.X) < SystemParameters.MinimumHorizontalDragDistance &&
            Math.Abs(now.Y - _pressAt.Y) < SystemParameters.MinimumVerticalDragDistance) return;
        _dragging = _pressed;
        _pressed = null;
        _dragSource = sender as FrameworkElement;
        if (_dragSource is not null) { _dragOpacity = _dragSource.Opacity; _dragSource.Opacity = 0.4; }
        Cursor = Cursors.SizeAll;
        CaptureMouse();
        e.Handled = true;
    }

    private void Editor_PreviewMouseMove(object sender, MouseEventArgs e)
    {
        if (_dragging is null) return;
        ClearMarks();
        if (DropTargetAt(e.GetPosition(this)) is { } target && (target.Row is not null || !_dragging.IsOff)) Mark(target.Border);
    }

    private void Editor_PreviewMouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        _pressed = null;
        if (_dragging is not { } tile) return;
        var target = DropTargetAt(e.GetPosition(this));
        int index = target?.Row is not null ? DropIndex(target.Value.Border) : -1;
        EndDrag();
        e.Handled = true;
        if (target is not { } drop || Vm is not { } vm) return;
        if (drop.Row is { } row) Dispatcher.BeginInvoke(new Action(() => vm.PlaceLobbyTile(tile, row.Index, index)));
        else if (!tile.IsOff) Dispatcher.BeginInvoke(new Action(() => vm.TakeLobbyTileOut(tile)));
    }

    private void Editor_LostMouseCapture(object sender, MouseEventArgs e)
    {
        if (_dragging is not null && !IsMouseCaptured) EndDrag();
    }

    private void EndDrag()
    {
        var source = _dragSource;
        _dragging = null;
        _dragSource = null;
        if (source is not null) source.Opacity = _dragOpacity;
        ClearValue(CursorProperty);
        ClearMarks();
        if (IsMouseCaptured) ReleaseMouseCapture();
    }

    // The row (or the "Not in the lobby" tray, Row = null) under a point of the editor.
    private (Border Border, LobbyRow? Row)? DropTargetAt(Point point)
    {
        if (InputHitTest(point) is not DependencyObject hit) return null;
        for (DependencyObject? node = hit; node is not null; node = VisualTreeHelper.GetParent(node))
        {
            if (node is Border { DataContext: LobbyRow row, AllowDrop: true } rowBorder) return (rowBorder, row);
            if (ReferenceEquals(node, TrayDrop)) return (TrayDrop, null);
            if (ReferenceEquals(node, this)) break;
        }
        return null;
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
    private static int DropIndex(DependencyObject rowBorder) => DropIndex(rowBorder, item => Mouse.GetPosition(item));
    private static int DropIndex(DependencyObject rowBorder, DragEventArgs e) => DropIndex(rowBorder, item => e.GetPosition(item));

    private static int DropIndex(DependencyObject rowBorder, Func<IInputElement, Point> position)
    {
        var list = FindChild<ItemsControl>(rowBorder);
        if (list is null) return -1;
        for (int i = 0; i < list.Items.Count; i++)
        {
            if (list.ItemContainerGenerator.ContainerFromIndex(i) is not FrameworkElement item) continue;
            Point p = position(item);
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
