using System.Globalization;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public sealed record SkillConfigurationRequest(Guid RequestId, string ExpectedRevision,
    IReadOnlyList<ProgressionSkill> Skills, IReadOnlyList<string> SkillSlots);

public static class CharacterProgressionApiEndpoints
{
    public static void MapCharacterProgressionApi(this WebApplication app)
    {
        app.MapGet("/api/characters/{characterId:guid}/progression", async (
            Guid characterId, HttpContext context, IGameRepository game, ICharacterProgressionRepository progression,
            TimeProvider clock, CancellationToken cancellationToken) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            string? owner = await game.GetCharacterOwnerAsync(characterId, cancellationToken);
            if (owner is null) return Results.NotFound();
            DateTimeOffset now = clock.GetUtcNow();
            bool allowed = principal.IsAdministrator || (principal.Kind == PrincipalKind.Player && principal.SteamId == owner);
            if (!allowed && principal.IsGameServer && principal.DungeonSessionId is Guid sessionId)
            {
                DungeonSession? session = await game.GetDungeonSessionAsync(sessionId, now, cancellationToken);
                allowed = session is not null && principal.MatchesGameServer(session.ServerId ?? "", sessionId)
                    && await game.IsActiveDungeonSessionMemberAsync(sessionId, owner, characterId, now, cancellationToken);
            }
            if (!allowed) return Results.StatusCode(StatusCodes.Status403Forbidden);
            try
            {
                CharacterProgressionState? state = await progression.LoadProgressionAsync(characterId, now, cancellationToken);
                return state is null ? Results.NotFound(new { error = "progression_not_initialized" }) : Results.Ok(ToResponse(state));
            }
            catch (ProgressionConflictException error) { return Results.Conflict(new { error = error.Message }); }
        });

        app.MapPut("/api/characters/{characterId:guid}/skill-configuration", async (
            Guid characterId, SkillConfigurationRequest request, HttpContext context,
            ICharacterProgressionRepository progression, TimeProvider clock, CancellationToken cancellationToken) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
                return Results.StatusCode(StatusCodes.Status403Forbidden);
            if (!long.TryParse(request.ExpectedRevision, NumberStyles.None, CultureInfo.InvariantCulture, out long revision))
                return Results.BadRequest(new { error = "invalid_progression_revision" });
            try
            {
                CharacterProgressionState state = await progression.CommitSkillConfigurationAsync(characterId,
                    principal.SteamId, new SkillConfigurationCommand(request.RequestId, revision, request.Skills, request.SkillSlots),
                    clock.GetUtcNow(), cancellationToken);
                return Results.Ok(ToResponse(state));
            }
            catch (ProgressionConflictException error)
            {
                if (error.Message == "character_not_owned") return Results.StatusCode(StatusCodes.Status403Forbidden);
                if (error.Message == "progression_not_initialized") return Results.NotFound(new { error = error.Message });
                return Results.Conflict(new { error = error.Message });
            }
        });
    }

    public static async Task<bool> CanEnterAsync(IGameRepository repository, Guid characterId,
        string dungeonId, string difficulty, DateTimeOffset now, CancellationToken cancellationToken)
    {
        if (!FirstSliceProgression.IsContent(dungeonId)) return true;
        try
        {
            CharacterProgressionState? state = await ((ICharacterProgressionRepository)repository)
                .LoadProgressionAsync(characterId, now, cancellationToken);
            return FirstSliceProgression.CanEnter(state, dungeonId, difficulty);
        }
        catch (ProgressionConflictException) { return false; }
    }

    private static object ToResponse(CharacterProgressionState state) => new
    {
        state.CharacterId, state.ProfileId, state.ClassId,
        Revision = state.Revision.ToString(CultureInfo.InvariantCulture), state.Experience,
        Level = FirstSliceProgression.Level(state), TotalSP = FirstSliceProgression.TotalSP(state),
        UsedSP = FirstSliceProgression.UsedSP(state), state.Milestones,
        IdentityUnlocked = FirstSliceProgression.IdentityUnlocked(state), state.Skills, state.SkillSlots
    };
}
