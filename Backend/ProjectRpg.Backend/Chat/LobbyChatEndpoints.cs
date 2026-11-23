using Microsoft.AspNetCore.Mvc;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Chat;

internal static class LobbyChatEndpoints
{
    public static void MapLobbyChat(this WebApplication app)
    {
        var chat = app.MapGroup("/api/characters/{characterId:guid}/chat");
        chat.AddEndpointFilter(async (context, next) =>
        {
            var http = context.HttpContext;
            http.Response.Headers.CacheControl = "no-store";
            var principal = http.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId == null)
                return Results.StatusCode(403);
            var repository = http.RequestServices.GetRequiredService<ICharacterRepository>();
            var ct = http.RequestAborted;
            var characters = await repository.GetCharactersAsync(principal.SteamId, ct);
            var character = characters.FirstOrDefault(c => c.CharacterId == context.GetArgument<Guid>(0));
            if (character == null) return Results.StatusCode(403);
            var rosters = await repository.GetRostersAsync(principal.SteamId, ct);
            var roster = rosters.FirstOrDefault(r => r.RosterId == character.RosterId);
            if (roster == null) return Results.StatusCode(403);
            http.Items[typeof(ChatActor)] = new ChatActor(character.CharacterId, character.Name, roster.WorldId);
            return await next(context);
        });
        chat.MapGet("", (Guid characterId, bool lobby, Guid? epoch, long after, HttpContext http, LobbyChat service) =>
            after < 0 ? Results.BadRequest() : Results.Ok(service.Poll(Actor(http), lobby, epoch, after)));
        chat.MapPost("", (Guid characterId, ChatPost post, HttpContext http, LobbyChat service) =>
            service.Send(Actor(http), post)).WithMetadata(new RequestSizeLimitAttribute(8192));
    }

    private static ChatActor Actor(HttpContext http) => (ChatActor)http.Items[typeof(ChatActor)]!;
}
