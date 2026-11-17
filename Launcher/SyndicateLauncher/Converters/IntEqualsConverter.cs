using System;
using System.Globalization;
using System.Windows.Data;

namespace SyndicateLauncher.Converters;

/// <summary>True when the bound integer equals the ConverterParameter (used by the difficulty chips).</summary>
public sealed class IntEqualsConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        => value is int number && int.TryParse(parameter?.ToString(), out var expected) && number == expected;

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => Binding.DoNothing;
}
