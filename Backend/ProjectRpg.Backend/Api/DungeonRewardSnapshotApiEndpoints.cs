using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public static class DungeonRewardSnapshotApiEndpoints
{
    public static void MapDungeonRewardSnapshotApi(this WebApplication app)
    {
        app.MapGet("/api/dungeon-sessions/{dungeonSessionId:guid}/reward-snapshot/{characterId:guid}",
            async (Guid dungeonSessionId, Guid characterId, HttpContext context,
                IGameRepository gameRepository, IItemRepository itemRepository, IEconomyRepository economyRepository,
                TimeProvider timeProvider, CancellationToken cancellationToken, string? rewardVersion) =>
            {
                context.Response.Headers.CacheControl = "no-store";
                AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
                if (!principal.IsAdministrator && (!principal.IsGameServer || principal.DungeonSessionId != dungeonSessionId))
                    return Results.StatusCode(StatusCodes.Status403Forbidden);

                DungeonSession? session = await gameRepository.GetDungeonSessionAsync(dungeonSessionId, timeProvider.GetUtcNow(), cancellationToken);
                if (session is null) return Results.NotFound();
                if ((!principal.IsAdministrator && !principal.MatchesGameServer(session.ServerId ?? string.Empty))
                    || !session.Members.Any(member => member.CharacterId == characterId))
                    return Results.StatusCode(StatusCodes.Status403Forbidden);

                if (rewardVersion is not null && !EconomyRules.IsSimpleIdentifier(rewardVersion, 64))
                    return Results.BadRequest(new { error = "Invalid reward version." });

                if (session.State == DungeonSessionState.SettlementPending)
                    return Results.Json(new { dungeonSessionId, characterId, state = "Pending" }, statusCode: StatusCodes.Status202Accepted);
                if (session.State != DungeonSessionState.Cleared)
                    return Results.Conflict(new { error = "The dungeon has no completed reward snapshot." });

                CharacterEconomyContext? ownerContext = await gameRepository.GetCharacterEconomyContextAsync(characterId, cancellationToken);
                if (ownerContext is null) return Results.NotFound();
                ItemOwnerRef owner = new(ItemOwnerType.Character, characterId.ToString("D"));
                IReadOnlyList<ItemRecord> items = await itemRepository.FindByOwnerAsync(owner, false, 501, cancellationToken);
                if (items.Count > 500)
                    return Results.Conflict(new { error = "The active item snapshot exceeds the current 500-record projection limit." });
                CurrencyWallet wallet = await economyRepository.LoadWalletAsync(ownerContext, cancellationToken);
                int? deliveredItemQuantity = null;
                if (rewardVersion is not null)
                {
                    Guid requestId = DungeonRewardIdentity.ItemRequestId(dungeonSessionId, characterId, rewardVersion);
                    ItemRepositoryCommitResult? receipt = await itemRepository.TryGetCommitResultAsync(requestId, cancellationToken);
                    if (receipt is not null && (receipt.Actor != owner || receipt.Operation != "DungeonReward"
                        || receipt.AffectedQuantity < 0
                        || receipt.Status is not (ItemRepositoryCommitStatus.Committed or ItemRepositoryCommitStatus.AlreadyCommitted)))
                        return Results.Conflict(new { error = "Invalid reward delivery receipt." });
                    // A pure-currency or all-miss personal pool has no item command.
                    deliveredItemQuantity = receipt?.AffectedQuantity ?? 0;
                }
                return Results.Ok(new
                {
                    dungeonSessionId, characterId, state = "Ready",
                    wallet = EconomyApiContractMapper.ToContract(wallet), items = items.Select(ItemApiContractMapper.ToContract).ToArray(),
                    delivery = deliveredItemQuantity is int quantity ? new { rewardVersion, itemQuantity = quantity } : null
                });
            });
    }
}
