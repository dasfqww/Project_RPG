using System.Globalization;
using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Operations.Core.Characters;

public enum CharacterLookupKind { SteamId, CharacterId }
public enum ProgressionState { Available, Missing, Unavailable }
public sealed record ProgressionSummary(ProgressionState State, string? ClassId, int? Level);
public sealed record CharacterHeader(Guid CharacterId, string AccountId, Guid RosterId, string Name,
    DateTimeOffset CreatedAtUtc, ProgressionSummary Progression);
public sealed record CharacterLookup(CharacterLookupKind Kind, string Value, PageRequest Page)
{
    public bool IsValid => Page.IsValid() && (Page.After is null || Page.After.Timestamp.HasValue)
        && (Kind == CharacterLookupKind.SteamId ? ValidSteamId(Value)
            : Kind == CharacterLookupKind.CharacterId && ValidGuid(Value, out _));

    public static bool ValidSteamId(string? value) => value is { Length: >= 1 and <= 20 }
        && value[0] != '0' && value.All(c => c is >= '0' and <= '9')
        && ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out ulong id) && id != 0;
    public static bool ValidGuid(string? value, out Guid id) => Guid.TryParseExact(value, "D", out id)
        && id != Guid.Empty && id.ToString("D") == value;
}

public interface ICharacterReadStore
{
    Task<Result<Page<CharacterHeader>>> SearchAsync(CharacterLookup query, CancellationToken cancellationToken);
    Task<Result<CharacterHeader>> FindAsync(Guid characterId, CancellationToken cancellationToken);
}

public sealed class SearchCharacters(IOperatorAccess access, ICharacterReadStore store)
{
    public async Task<Result<Page<CharacterHeader>>> ExecuteAsync(CharacterLookup query, CancellationToken ct)
    {
        Result<OperatorActor> actor = await access.AuthorizeAsync(OpsCapability.ReadCharacter, ct);
        if (!actor.IsSuccess) return Result<Page<CharacterHeader>>.Fail(actor.Error!);
        if (!query.IsValid) return Result<Page<CharacterHeader>>.Fail(Failures.InvalidQuery);
        return await store.SearchAsync(query, ct);
    }
}

public sealed class GetCharacterSummary(IOperatorAccess access, ICharacterReadStore store)
{
    public async Task<Result<CharacterHeader>> ExecuteAsync(Guid characterId, CancellationToken ct)
    {
        Result<OperatorActor> actor = await access.AuthorizeAsync(OpsCapability.ReadCharacter, ct);
        if (!actor.IsSuccess) return Result<CharacterHeader>.Fail(actor.Error!);
        if (characterId == Guid.Empty) return Result<CharacterHeader>.Fail(Failures.InvalidQuery);
        return await store.FindAsync(characterId, ct);
    }
}
