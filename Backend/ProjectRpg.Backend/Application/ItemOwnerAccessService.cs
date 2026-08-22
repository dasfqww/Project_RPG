using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Application;

public sealed class ItemOwnerAccessService(IGameRepository gameRepository)
{
    public async Task<bool> CanReadAsync(
        AuthenticatedPrincipal principal,
        ItemOwnerRef owner,
        CancellationToken cancellationToken)
    {
        if (principal.IsGameServer)
        {
            return true;
        }

        if (principal.SteamId is null)
        {
            return false;
        }

        if (owner.Type == ItemOwnerType.Account)
        {
            return string.Equals(
                owner.OwnerId,
                principal.SteamId,
                StringComparison.Ordinal);
        }

        if (owner.Type != ItemOwnerType.Character
            || !Guid.TryParse(owner.OwnerId, out Guid characterId))
        {
            return false;
        }

        string? steamId = await gameRepository.GetCharacterOwnerAsync(
            characterId,
            cancellationToken);
        return string.Equals(
            steamId,
            principal.SteamId,
            StringComparison.Ordinal);
    }
}
