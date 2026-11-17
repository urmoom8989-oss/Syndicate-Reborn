// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;

namespace SyndicateClassic;

// On-screen keyboard for the controller: A on a text box or password box opens it. The d-pad / stick moves over the
// keys, A types, X deletes, Y adds a space, LB / RB switch case, B (or Menu / Done) closes it and puts the controller
// back on the box. Letters go in at the box's caret (or replace its selection); passwords are added at the end.
public partial class MainWindow
{
    private Border? _padKeyboard;
    private Control? _padKeyTarget;
    private TextBlock? _padKeyPreview;
    private bool _padKeyUpper;
    private readonly System.Collections.Generic.List<(Button Key, string Lower)> _padLetterKeys = new();

    private bool PadKeyboardOpen => _padKeyboard is { Visibility: Visibility.Visible } && _padKeyTarget != null;

    private void OpenPadKeyboard(Control target)
    {
        if (_modernFrame is not Grid host) return;
        if (_padKeyboard == null) BuildPadKeyboard(host);
        if (_padKeyboard == null) return;
        _padKeyTarget = target;
        _padInRail = _padInMusic = false;
        UpdatePadKeyPreview();
        _padKeyboard.Visibility = Visibility.Visible;
        Keyboard.ClearFocus();
        FocusPadRoot();
        UpdatePadHints(force: true);
    }

    private void ClosePadKeyboard()
    {
        if (_padKeyboard == null) return;
        _padKeyboard.Visibility = Visibility.Collapsed;
        var target = _padKeyTarget;
        _padKeyTarget = null;
        if (target is { IsVisible: true }) target.Focus();
        else FocusPadRoot();
        UpdatePadHints(force: true);
    }

    private void BuildPadKeyboard(Grid host)
    {
        var rows = new[] { "1234567890", "qwertyuiop", "asdfghjkl@", "zxcvbnm-_.", ":/!?#&+=()" };
        var keys = new StackPanel { HorizontalAlignment = HorizontalAlignment.Center };
        Button Key(string label, double width, Action press)
        {
            var b = new Button { Width = width, Height = 42, Margin = new Thickness(3), Padding = new Thickness(0), Content = label, FontSize = 15, FontWeight = FontWeights.SemiBold, FocusVisualStyle = null };
            RoundButton(b, "#1FFFFFFF", Night.Text, 9);
            b.MouseEnter += (_, __) => b.Background = Hex(Night.FrostHover);
            b.MouseLeave += (_, __) => b.Background = Hex("#1FFFFFFF");
            b.Click += (_, __) => press();
            return b;
        }
        foreach (var row in rows)
        {
            var line = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center };
            foreach (char c in row)
            {
                string lower = c.ToString();
                var b = Key(lower, 46, () => PadKeyType(char.IsLetter(lower[0]) && _padKeyUpper ? lower.ToUpperInvariant() : lower));
                if (char.IsLetter(c)) _padLetterKeys.Add((b, lower));
                line.Children.Add(b);
            }
            keys.Children.Add(line);
        }
        var last = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center };
        last.Children.Add(Key("Shift", 92, PadKeyShift));
        last.Children.Add(Key("Space", 196, () => PadKeyType(" ")));
        last.Children.Add(Key("Delete", 92, PadKeyBackspace));
        var done = Key("Done", 104, ClosePadKeyboard);
        RoundButton(done, Night.Gold, Night.OnGold, 9);
        done.MouseLeave += (_, __) => done.Background = Hex(Night.Gold);
        last.Children.Add(done);
        keys.Children.Add(last);

        _padKeyPreview = new TextBlock { FontSize = 18, Foreground = Hex(Night.Text), TextTrimming = TextTrimming.CharacterEllipsis, TextWrapping = TextWrapping.NoWrap, Margin = new Thickness(4, 0, 4, 12), MinHeight = 24 };
        var body = new StackPanel();
        body.Children.Add(new Border { Child = _padKeyPreview, Background = Hex("#26000000"), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(10), Padding = new Thickness(12, 10, 12, 0), Margin = new Thickness(3, 0, 3, 10) });
        body.Children.Add(keys);
        var card = new Border
        {
            Child = body, Padding = new Thickness(16), CornerRadius = new CornerRadius(16), Background = Hex("#F20B0E1F"), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1),
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(0, 0, 0, 70)
        };
        _padKeyboard = new Border { Child = card, Background = Hex("#99000000"), Visibility = Visibility.Collapsed };
        if (host.RowDefinitions.Count > 1) Grid.SetRowSpan(_padKeyboard, host.RowDefinitions.Count);
        if (host.ColumnDefinitions.Count > 1) Grid.SetColumnSpan(_padKeyboard, host.ColumnDefinitions.Count);
        Panel.SetZIndex(_padKeyboard, 10000);
        // Clicking the dark area closes it too.
        _padKeyboard.MouseLeftButtonDown += (_, e) => { if (e.OriginalSource == _padKeyboard) { e.Handled = true; ClosePadKeyboard(); } };
        _padKeyboard.PreviewKeyDown += (_, e) => { if (e.Key == System.Windows.Input.Key.Escape) { e.Handled = true; ClosePadKeyboard(); } };
        host.Children.Add(_padKeyboard);
    }

    private void PadKeyType(string s)
    {
        switch (_padKeyTarget)
        {
            case TextBox text when !text.IsReadOnly:
            {
                int at = Math.Clamp(text.SelectionStart, 0, text.Text.Length);
                int cut = Math.Clamp(text.SelectionLength, 0, text.Text.Length - at);
                if (text.MaxLength > 0 && text.Text.Length - cut + s.Length > text.MaxLength) return;
                text.Text = text.Text.Remove(at, cut).Insert(at, s);
                text.CaretIndex = at + s.Length;
                break;
            }
            case PasswordBox password:
                if (password.MaxLength > 0 && password.Password.Length + s.Length > password.MaxLength) return;
                password.Password += s;
                break;
        }
        UpdatePadKeyPreview();
    }

    private void PadKeyBackspace()
    {
        switch (_padKeyTarget)
        {
            case TextBox text when !text.IsReadOnly:
            {
                int at = Math.Clamp(text.SelectionStart, 0, text.Text.Length);
                int cut = Math.Clamp(text.SelectionLength, 0, text.Text.Length - at);
                if (cut > 0) { text.Text = text.Text.Remove(at, cut); text.CaretIndex = at; }
                else if (at > 0) { text.Text = text.Text.Remove(at - 1, 1); text.CaretIndex = at - 1; }
                break;
            }
            case PasswordBox password when password.Password.Length > 0:
                password.Password = password.Password[..^1];
                break;
        }
        UpdatePadKeyPreview();
    }

    private void PadKeyShift()
    {
        _padKeyUpper = !_padKeyUpper;
        foreach (var (key, lower) in _padLetterKeys) key.Content = _padKeyUpper ? lower.ToUpperInvariant() : lower;
    }

    private void UpdatePadKeyPreview()
    {
        if (_padKeyPreview == null) return;
        _padKeyPreview.Text = _padKeyTarget switch
        {
            TextBox text => text.Text.Insert(Math.Clamp(text.CaretIndex, 0, text.Text.Length), "|"),
            PasswordBox password => new string('•', password.Password.Length) + "|",
            _ => ""
        };
    }
}
