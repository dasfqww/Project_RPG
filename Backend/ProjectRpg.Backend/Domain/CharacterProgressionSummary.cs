namespace ProjectRpg.Backend.Domain;

/// <summary>Read-only roster projection of the stored save, without settlement reconciliation.</summary>
public sealed record CharacterProgressionSummary(string State, string? ClassId = null, int? Level = null)
{
    public static readonly CharacterProgressionSummary Missing = new("Missing");
    public static readonly CharacterProgressionSummary Unavailable = new("Unavailable");

    public static CharacterProgressionSummary FromStored(Guid characterId, CharacterProgressionState? stored)
    {
        if (stored is null) return Missing;
        try
        {
            var profile = CharacterProgressionProfiles.Registered.Resolve(stored, allowLegacy: true);
            profile.ValidateState(stored, allowLegacyState: true);
            if (stored.CharacterId != characterId) return Unavailable;
            // The known draft alias has the same presentation identity. A roster
            // read does not migrate the save or advance its revision.
            return new("Available", profile.ClassId, FirstSliceProgression.Level(stored));
        }
        catch (ProgressionConflictException) { return Unavailable; }
    }
}
