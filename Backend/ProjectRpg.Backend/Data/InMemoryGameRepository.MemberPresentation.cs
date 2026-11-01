namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository
{
    // Character names are immutable in the current repository contract. Read the existing
    // authoritative character at create/join; never accept a name from a membership request.
    private string? GetDungeonMemberCharacterName(Guid characterId, string steamId) =>
        _characters.TryGetValue(characterId, out var character)
        && string.Equals(character.SteamId, steamId, StringComparison.Ordinal)
            ? character.Name
            : null;
}
