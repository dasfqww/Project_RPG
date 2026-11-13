namespace ProjectRpg.Backend.Riding;

public sealed record RidingOwnerView(RidingCollection Collection, RidingPreferences Preferences);
public sealed record RidingCommandReceipt(string Operation, RidingRegistrationReceipt? Registration, RidingPreferencesReceipt? Preferences);
public interface IRidingPreferencesStore
{
    Task<RidingPreferencesResult> SaveRidingPreferencesAsync(string authenticatedSteamId, Guid characterId,
        RidingPreferencesCommand command, RidingCatalog catalog, CancellationToken cancellationToken);
    Task<RidingOwnerView> GetRidingOwnerViewAsync(string authenticatedSteamId, Guid characterId, CancellationToken cancellationToken);
    Task<RidingCommandReceipt?> GetRidingCommandReceiptAsync(string authenticatedSteamId, Guid characterId, Guid requestId, CancellationToken cancellationToken);
}
