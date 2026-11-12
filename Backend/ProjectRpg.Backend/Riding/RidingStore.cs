using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Riding;

public enum RidingStoreFailure { CharacterNotOwned, RequiresLobby, IdempotencyConflict, InconsistentReceipt, ItemCommitFailed }
public sealed class RidingStoreException(RidingStoreFailure code) : Exception("riding_" + code)
{
    public RidingStoreFailure Code { get; } = code;
}

public sealed record RidingCollection(Guid RosterId, long Revision, IReadOnlyList<RidingUnlock> Mounts);
public sealed record RidingRegistrationReceipt(Guid RequestId, string Operation, string Fingerprint,
    Guid CharacterId, Guid RosterId, RidingRegistrationOutcome Outcome, string MountId, long RosterRevision,
    DateTimeOffset CommittedAtUtc, ItemRepositoryCommitResult? ItemReceipt);
public sealed record RidingRegistrationResult(bool Replayed, RidingRegistrationReceipt Receipt);

/// <summary>Trusted application port. The account comes from authentication; catalog comes from server configuration.
/// Implementations own authorization, lobby checks, item consumption and BOTH receipts as one transaction.</summary>
public interface IRidingRegistrationStore
{
    Task<RidingRegistrationResult> RegisterMountAsync(string authenticatedSteamId, Guid characterId,
        RidingRegistrationCommand command, RidingCatalog catalog, CancellationToken cancellationToken);
    Task<RidingCollection> GetRidingCollectionAsync(string authenticatedSteamId, Guid characterId, CancellationToken cancellationToken);
    Task<RidingRegistrationReceipt?> GetRidingRegistrationReceiptAsync(string authenticatedSteamId, Guid characterId,
        Guid requestId, CancellationToken cancellationToken);
}
