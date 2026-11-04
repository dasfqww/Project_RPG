using Microsoft.Extensions.DependencyInjection;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

static class RepositoryContractTests
{
    public static async Task Run()
    {
        var ownership = new OwnershipOnly();
        var service = new ItemOwnerAccessService(ownership);
        var character = Guid.NewGuid();
        var player = new AuthenticatedPrincipal(PrincipalKind.Player, "76561198000000001", null);
        var owner = new ItemOwnerRef(ItemOwnerType.Character, character.ToString("D"));
        using var cancellation = new CancellationTokenSource();
        ownership.Owner = player.SteamId;
        Checks.Require(await service.CanReadAsync(player, owner, cancellation.Token)
            && ownership.Character == character && ownership.Cancellation == cancellation.Token,
            "Ownership-only implementation could not authorize its character.");
        ownership.Owner = "76561198000000002";
        Checks.Require(!await service.CanReadAsync(player, owner, default), "Foreign character was authorized.");
        ownership.Owner = null;
        Checks.Require(!await service.CanReadAsync(player, owner, default), "Missing character was authorized.");

        var store = new InMemoryGameRepository();
        var services = new ServiceCollection();
        services.AddSingleton<IGameRepository>(store);
        services.AddGameRepositoryContracts(); services.AddGameRepositoryContracts();
        using var provider = services.BuildServiceProvider();
        object[] views = [provider.GetRequiredService<IAuthenticationRepository>(),
            provider.GetRequiredService<ICharacterRepository>(), provider.GetRequiredService<ICharacterOwnershipRepository>(),
            provider.GetRequiredService<ICharacterEconomyRepository>(), provider.GetRequiredService<IDungeonSessionRepository>(),
            provider.GetRequiredService<IDungeonMembershipRepository>(), provider.GetRequiredService<IGameServerMembershipRepository>(),
            provider.GetRequiredService<IDungeonSettlementRepository>(), provider.GetRequiredService<IJoinTicketRepository>(),
            provider.GetRequiredService<ILegacyInventoryRepository>(), provider.GetRequiredService<ICurrencyAuthorityRepository>()];
        Checks.Require(views.All(view => ReferenceEquals(view, store)), "Contract registration created independent storage state.");
        services = new ServiceCollection();
        services.AddSingleton<ICharacterOwnershipRepository>(ownership); services.AddGameRepositoryContracts();
        using var replacement = services.BuildServiceProvider();
        Checks.Require(ReferenceEquals(replacement.GetRequiredService<ICharacterOwnershipRepository>(), ownership),
            "Default aliases overwrote an explicit narrow provider.");
        Console.WriteLine("PASS repository contracts: ownership-only authorization and shared-state/replaceable DI views.");
    }

    private sealed class OwnershipOnly : ICharacterOwnershipRepository
    {
        public string? Owner;
        public Guid Character;
        public CancellationToken Cancellation;
        public Task<string?> GetCharacterOwnerAsync(Guid characterId, CancellationToken cancellationToken)
        {
            Character = characterId; Cancellation = cancellationToken;
            return Task.FromResult(Owner);
        }
    }
}
