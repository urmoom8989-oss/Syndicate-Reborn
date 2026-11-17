using System.Windows.Media;

namespace SyndicateLauncher.Models;

/// <summary>One of Syndicate's OG profile pictures.</summary>
public sealed record AvatarChoice(string Id, ImageSource? Image);
