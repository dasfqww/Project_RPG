using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Town;

internal static class TownEndpoints
{
    public static void MapTownChannels(this WebApplication app)
    {
        app.MapPost("/api/characters/{characterId:guid}/town", async (Guid characterId, string? destination, HttpContext http,
            ICharacterRepository repository, TownChannelPool pool, CancellationToken ct) =>
        {
            http.Response.Headers.CacheControl = "no-store";
            var principal = http.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId == null) return Results.StatusCode(403);
            if (!pool.Enabled) return Results.StatusCode(503);
            var region = destination switch
            {
                null or "town" => TownChannelPool.Region.Town,
                "bellmarket" => TownChannelPool.Region.Bellmarket,
                _ => (TownChannelPool.Region?)null
            };
            if (region == null) return Results.BadRequest();
            var characters = await repository.GetCharactersAsync(principal.SteamId, ct);
            var character = characters.FirstOrDefault(c => c.CharacterId == characterId);
            if (character == null) return Results.StatusCode(403);
            var rosters = await repository.GetRostersAsync(principal.SteamId, ct);
            var roster = rosters.FirstOrDefault(r => r.RosterId == character.RosterId);
            return roster == null ? Results.StatusCode(403) :
                await pool.AdmitAsync(characterId, principal.SteamId, character.Name, roster.WorldId, ct, region.Value);
        });
    }
}
