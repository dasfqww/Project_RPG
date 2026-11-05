using System.Net;
using ProjectRpg.Operations.Core.Characters;

namespace ProjectRpg.Backend.Operations.Hosting;

public sealed class OperationsOptions
{
    public bool Enabled { get; set; }
    public string EnvironmentId { get; set; } = "";
    public string ManagementOrigin { get; set; } = "";
    public string[] AllowedNetworks { get; set; } = ["127.0.0.0/8", "::1/128"];
    public string[] KnownProxies { get; set; } = [];
    public string ClientId { get; set; } = "";
    public string ClientSecret { get; set; } = "";
    public string AllowedGitHubUserId { get; set; } = "";
    public string AuthEpoch { get; set; } = "";
    public string KeyDirectory { get; set; } = "";
    public string KeyCertificatePath { get; set; } = "";
    public string KeyCertificatePassword { get; set; } = "";
    public bool AllowUnencryptedDevelopmentKeys { get; set; }
    public string LogRoot { get; set; } = "";
    public string SourceId { get; set; } = "backend";
    public Infrastructure.Compensation.CompensationOptions Compensation { get; set; } = new();
    public Monitoring.OperationsMonitorOptions Monitor { get; set; } = new();
    public bool RestrictionsEnabled { get; set; }
    public string OperatorId => "github:" + AllowedGitHubUserId;

    public void Validate(IHostEnvironment environment)
    {
        if (!Enabled) return;
        Monitor.Validate();
        if (!Uri.TryCreate(ManagementOrigin, UriKind.Absolute, out Uri? origin) || origin.Scheme != "https"
            || origin.AbsolutePath != "/" || origin.Query.Length != 0 || origin.Fragment.Length != 0 || origin.UserInfo.Length != 0)
            throw new InvalidOperationException("Operations requires an HTTPS management origin without a path.");
        if (EnvironmentId.Length is < 1 or > 64 || !EnvironmentId.All(c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_')
            || SourceId.Length is < 1 or > 64 || !SourceId.All(c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_')
            || !CharacterLookup.ValidSteamId(AllowedGitHubUserId) || AuthEpoch.Length is < 16 or > 128
            || string.IsNullOrWhiteSpace(ClientId) || string.IsNullOrWhiteSpace(ClientSecret)
            || !Path.IsPathFullyQualified(KeyDirectory) || !Path.IsPathFullyQualified(LogRoot)
            || AllowedNetworks.Length == 0 || AllowedNetworks.Any(n => !System.Net.IPNetwork.TryParse(n, out _))
            || KnownProxies.Any(p => !IPAddress.TryParse(p, out _)))
            throw new InvalidOperationException("Operations settings are incomplete or invalid; secret values are not included in this error.");
        bool development = environment.IsDevelopment() || environment.IsEnvironment("Testing");
        if (AllowUnencryptedDevelopmentKeys && !development)
            throw new InvalidOperationException("Unencrypted operations keys are only permitted in Development/Testing.");
        if (!OperatingSystem.IsWindows() && KeyCertificatePath.Length == 0 && !AllowUnencryptedDevelopmentKeys)
            throw new InvalidOperationException("Operations keys require a protection certificate on this host.");
        // File sink date rolling uses local time. Do not silently produce mixed-zone files.
        if (!development && TimeZoneInfo.Local.BaseUtcOffset != TimeSpan.Zero)
            throw new InvalidOperationException("Operations production host timezone must be UTC.");
        ManagedPath.EnsurePrivateDirectory(KeyDirectory, environment.ContentRootPath);
        ManagedPath.EnsurePrivateDirectory(LogRoot, environment.ContentRootPath);
        if (Path.GetFullPath(KeyDirectory).TrimEnd(Path.DirectorySeparatorChar)
            .Equals(Path.GetFullPath(LogRoot).TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Operations keys and logs require separate directories.");
    }
}

internal static class ManagedPath
{
    internal static void RejectLinks(string path)
    {
        for (DirectoryInfo? current = new DirectoryInfo(Path.GetFullPath(path)); current is not null; current = current.Parent)
            if (current.Exists && (current.Attributes & FileAttributes.ReparsePoint) != 0)
                throw new InvalidOperationException("Operations directories cannot traverse reparse points.");
    }

    internal static void EnsurePrivateDirectory(string path, string contentRoot)
    {
        string full = Path.GetFullPath(path);
        foreach (string name in new[] { "wwwroot", "Content", "Plugins" })
        {
            string forbidden = Path.GetFullPath(Path.Combine(contentRoot, name)) + Path.DirectorySeparatorChar;
            if ((full + Path.DirectorySeparatorChar).StartsWith(forbidden, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Operations data cannot be stored in public/content directories.");
        }
        RejectLinks(full);
        Directory.CreateDirectory(full);
        RejectLinks(full);
        if (!OperatingSystem.IsWindows())
            File.SetUnixFileMode(full, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
    }
}
