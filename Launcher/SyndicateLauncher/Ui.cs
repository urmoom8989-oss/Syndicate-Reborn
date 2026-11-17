using System.Windows;

namespace SyndicateLauncher;

/// <summary>Small attached flags used by control templates for visual states.</summary>
public static class Ui
{
    public static readonly DependencyProperty ActiveProperty = DependencyProperty.RegisterAttached(
        "Active", typeof(bool), typeof(Ui), new PropertyMetadata(false));

    public static bool GetActive(DependencyObject element) => (bool)element.GetValue(ActiveProperty);
    public static void SetActive(DependencyObject element, bool value) => element.SetValue(ActiveProperty, value);
}