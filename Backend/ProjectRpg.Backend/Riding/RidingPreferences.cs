using System.Globalization;

namespace ProjectRpg.Backend.Riding;

public enum RidingShortcutMode { Selected, Specific, RandomFavorites }
public sealed record RidingShortcut(RidingShortcutMode Mode, string? MountId);
public sealed record RidingPreferences(long Revision, string? SelectedMountId, IReadOnlyList<string> FavoriteMountIds, RidingShortcut Shortcut)
{
    public static RidingPreferences Empty => new(0, null, Array.Empty<string>(), new(RidingShortcutMode.Selected, null));
}
public sealed record RidingPreferencesCommand(Guid RequestId, long ExpectedRevision, string? SelectedMountId,
    IReadOnlyList<string> FavoriteMountIds, RidingShortcut Shortcut);
public sealed record RidingPreferencesReceipt(Guid RequestId, string Operation, string Fingerprint, Guid CharacterId,
    Guid RosterId, RidingPreferences Preferences, DateTimeOffset CommittedAtUtc);
public sealed record RidingPreferencesResult(bool Replayed, RidingPreferencesReceipt Receipt);

public static class RidingPreferencesRules
{
    public const string Operation = "Riding.Preferences";
    public static RidingPreferencesCommand Normalize(RidingPreferencesCommand command)
    {
        if (command is null || command.RequestId == Guid.Empty || command.ExpectedRevision is < 0 or long.MaxValue
            || command.FavoriteMountIds is null || command.FavoriteMountIds.Count > 32 || command.Shortcut is null
            || !Enum.IsDefined(command.Shortcut.Mode) || !Optional(command.SelectedMountId)
            || command.FavoriteMountIds.Any(id => !RidingIdentity.Identifier(id, 64, true))
            || command.FavoriteMountIds.Distinct(StringComparer.Ordinal).Count() != command.FavoriteMountIds.Count
            || (command.Shortcut.Mode == RidingShortcutMode.Specific ? !RidingIdentity.Identifier(command.Shortcut.MountId, 64, true) : command.Shortcut.MountId is not null))
            throw new RidingRuleException(RidingFailure.InvalidRequest);
        return command with { FavoriteMountIds = Array.AsReadOnly(command.FavoriteMountIds.Order(StringComparer.Ordinal).ToArray()) };
    }
    private static bool Optional(string? id) => id is null || RidingIdentity.Identifier(id, 64, true);
    public static string Fingerprint(RidingRegistrationActor actor, RidingPreferencesCommand command)
    {
        command = Normalize(command);
        if (actor is null || actor.CharacterId == Guid.Empty || actor.RosterId == Guid.Empty) throw new RidingRuleException(RidingFailure.InvalidRequest);
        return RidingIdentity.Sha256(string.Join('\n', "ProjectRPG.Riding.Preferences/v1", "character=" + actor.CharacterId.ToString("D"),
            "roster=" + actor.RosterId.ToString("D"), "request=" + command.RequestId.ToString("D"),
            "revision=" + command.ExpectedRevision.ToString(CultureInfo.InvariantCulture), "selected=" + (command.SelectedMountId ?? "-"),
            "favorites=" + string.Join(',', command.FavoriteMountIds), "mode=" + command.Shortcut.Mode, "mount=" + (command.Shortcut.MountId ?? "-"), ""));
    }
    public static RidingPreferences Plan(RidingPreferencesCommand command, long currentRevision, RidingCatalog catalog, RidingCollection collection)
    {
        command = Normalize(command);
        if (currentRevision < 0 || currentRevision == long.MaxValue || command.ExpectedRevision != currentRevision)
            throw new RidingRuleException(RidingFailure.RevisionConflict);
        if (catalog is null) throw new RidingRuleException(RidingFailure.DefinitionMismatch);
        var owned = collection.Mounts.Select(m => m.MountId).ToHashSet(StringComparer.Ordinal);
        foreach (string id in command.FavoriteMountIds.Concat(new[] { command.SelectedMountId, command.Shortcut.MountId }.OfType<string>()).Distinct(StringComparer.Ordinal))
            if (!owned.Contains(id) || !catalog.Mount(id).Enabled) throw new RidingRuleException(RidingFailure.DefinitionMismatch);
        return new(currentRevision + 1, command.SelectedMountId, command.FavoriteMountIds, command.Shortcut);
    }
}
