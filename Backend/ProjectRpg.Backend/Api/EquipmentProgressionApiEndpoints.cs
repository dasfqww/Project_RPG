using System.Globalization;
using System.Text.Json;
using Microsoft.AspNetCore.Http.Json;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public sealed record EquipmentExpectationRequest(Guid ItemId, string? ExpectedRevision, int? ExpectedLevel);
public sealed record EquipmentOreRequest(Guid ItemId, string? ExpectedRevision);
public sealed record EquipmentUpgradeRequest(Guid RequestId, int? RuleVersion,
    EquipmentExpectationRequest? Equipment, IReadOnlyList<EquipmentOreRequest?>? OreStacks);
public sealed record EquipmentTransferRequest(Guid RequestId, int? RuleVersion,
    EquipmentExpectationRequest? Source, EquipmentExpectationRequest? Target);
public sealed record EquipmentTokenRequest(Guid ItemId, string? ExpectedRevision);
public sealed record EquipmentCraftRequest(Guid RequestId, int? RuleVersion, string? TargetDefinitionName,
    int? TargetDefinitionVersion, IReadOnlyList<EquipmentTokenRequest?>? TokenStacks);
public sealed record EquipmentDismantleRequest(Guid RequestId, int? RuleVersion, EquipmentExpectationRequest? Equipment);

public static class EquipmentProgressionApiEndpoints
{
    public const int MaximumBodyBytes = 16 * 1024;

    public static void MapEquipmentProgressionApi(this WebApplication app)
    {
        app.MapPost("/api/characters/{characterId:guid}/equipment/craft", async (Guid characterId,
            HttpContext context, IEquipmentProgressionRepository repository, TimeProvider clock, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null) return Results.StatusCode(403);
            var (request, failure) = await ReadAsync<EquipmentCraftRequest>(context, ct);
            if (failure is not null) return failure;
            if (request?.RuleVersion is not int version || request.TargetDefinitionName is null
                || request.TargetDefinitionVersion is not int definitionVersion || request.TokenStacks is null
                || request.TokenStacks.Count is < 1 or > FirstSliceEquipmentCraftingPlanner.MaximumTokenStacks) return BadRequest();
            List<EquipmentTokenExpectation> tokens = [];
            foreach (EquipmentTokenRequest? stack in request.TokenStacks)
            {
                if (stack is null || !TryRevision(stack.ExpectedRevision, out long revision)) return BadRequest();
                tokens.Add(new(stack.ItemId, revision));
            }
            try
            {
                return Response(await repository.CraftEquipmentAsync(principal.SteamId,
                    new(request.RequestId, characterId, version, request.TargetDefinitionName, definitionVersion, tokens), clock.GetUtcNow(), ct));
            }
            catch (EquipmentProgressionRuleException error) { return Failure(error); }
        }).AddEndpointFilter<EquipmentReceiptObservationFilter>();
        app.MapPost("/api/characters/{characterId:guid}/equipment/dismantle", async (Guid characterId,
            HttpContext context, IEquipmentProgressionRepository repository, TimeProvider clock, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null) return Results.StatusCode(403);
            var (request, failure) = await ReadAsync<EquipmentDismantleRequest>(context, ct);
            if (failure is not null) return failure;
            if (request?.RuleVersion is not int version || !TryEquipment(request.Equipment, out EquipmentItemExpectation equipment)) return BadRequest();
            try
            {
                return Response(await repository.DismantleEquipmentAsync(principal.SteamId,
                    new(request.RequestId, characterId, version, equipment), clock.GetUtcNow(), ct));
            }
            catch (EquipmentProgressionRuleException error) { return Failure(error); }
        }).AddEndpointFilter<EquipmentReceiptObservationFilter>();
        app.MapPost("/api/characters/{characterId:guid}/equipment/upgrade", async (Guid characterId,
            HttpContext context, IEquipmentProgressionRepository repository, TimeProvider clock, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null) return Results.StatusCode(403);
            var (request, failure) = await ReadAsync<EquipmentUpgradeRequest>(context, ct);
            if (failure is not null) return failure;
            if (request?.RuleVersion is not int version || !TryEquipment(request.Equipment, out EquipmentItemExpectation equipment)
                || request.OreStacks is null || request.OreStacks.Count is < 1 or > FirstSliceEquipmentCommandPlanner.MaximumOreStacks)
                return BadRequest();
            List<EquipmentOreExpectation> ore = [];
            foreach (EquipmentOreRequest? stack in request.OreStacks)
            {
                if (stack is null || !TryRevision(stack.ExpectedRevision, out long revision)) return BadRequest();
                ore.Add(new(stack.ItemId, revision));
            }
            try
            {
                ItemRepositoryCommitResult result = await repository.UpgradeEquipmentAsync(principal.SteamId,
                    new(request.RequestId, characterId, version, equipment, ore), clock.GetUtcNow(), ct);
                return Response(result);
            }
            catch (EquipmentProgressionRuleException error) { return Failure(error); }
        }).AddEndpointFilter<EquipmentReceiptObservationFilter>();
        app.MapPost("/api/characters/{characterId:guid}/equipment/transfer", async (Guid characterId,
            HttpContext context, IEquipmentProgressionRepository repository, TimeProvider clock, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null) return Results.StatusCode(403);
            var (request, failure) = await ReadAsync<EquipmentTransferRequest>(context, ct);
            if (failure is not null) return failure;
            if (request?.RuleVersion is not int version || !TryEquipment(request.Source, out EquipmentItemExpectation source)
                || !TryEquipment(request.Target, out EquipmentItemExpectation target)) return BadRequest();
            try
            {
                ItemRepositoryCommitResult result = await repository.TransferEquipmentAsync(principal.SteamId,
                    new(request.RequestId, characterId, version, source, target), clock.GetUtcNow(), ct);
                return Response(result);
            }
            catch (EquipmentProgressionRuleException error) { return Failure(error); }
        }).AddEndpointFilter<EquipmentReceiptObservationFilter>();
    }

    private static bool TryRevision(string? text, out long revision) => long.TryParse(text,
        NumberStyles.None, CultureInfo.InvariantCulture, out revision) && revision is > 0 and < long.MaxValue;

    private static bool TryEquipment(EquipmentExpectationRequest? value, out EquipmentItemExpectation expected)
    {
        expected = null!;
        if (value is null || value.ExpectedLevel is not int level || !TryRevision(value.ExpectedRevision, out long revision)) return false;
        expected = new(value.ItemId, revision, level);
        return true;
    }

    private static IResult BadRequest() => Results.BadRequest(new { error = "invalid_equipment_command" });
    private static IResult Failure(EquipmentProgressionRuleException error) => error.Code switch
    {
        "character_not_owned" => Results.StatusCode(403),
        "item_not_found" => Results.NotFound(new { error = error.Code }),
        "invalid_equipment_command_identity" or "unsupported_equipment_rule_version" or "invalid_equipment_expectation"
            or "invalid_item_expectation" or "invalid_equipment_level" or "invalid_ore_stack_count"
            or "invalid_ore_expectation" or "duplicate_item_id" or "invalid_token_stack_count"
            or "invalid_token_expectation" or "invalid_craft_target" => Results.BadRequest(new { error = error.Code }),
        _ => Results.Conflict(new { error = error.Code })
    };

    private static IResult Response(ItemRepositoryCommitResult result) => Results.Json(ItemApiContractMapper.ToContract(result),
        statusCode: result.Status switch
        {
            ItemRepositoryCommitStatus.Committed or ItemRepositoryCommitStatus.AlreadyCommitted => 200,
            ItemRepositoryCommitStatus.NotFound => 404,
            ItemRepositoryCommitStatus.InternalError => 500,
            _ => 409
        });

    private static async Task<(T? Request, IResult? Failure)> ReadAsync<T>(HttpContext context, CancellationToken ct)
    {
        if (!context.Request.HasJsonContentType()) return (default, Results.StatusCode(415));
        if (context.Request.ContentLength > MaximumBodyBytes) return (default, Results.StatusCode(413));
        using MemoryStream bytes = new();
        byte[] buffer = new byte[4096];
        while (true)
        {
            int remaining = MaximumBodyBytes - (int)bytes.Length;
            int count = await context.Request.Body.ReadAsync(buffer.AsMemory(0, Math.Min(buffer.Length, remaining + 1)), ct);
            if (count == 0) break;
            if (count > remaining) return (default, Results.StatusCode(413));
            bytes.Write(buffer, 0, count);
        }
        try
        {
            JsonSerializerOptions options = context.RequestServices.GetRequiredService<IOptions<JsonOptions>>().Value.SerializerOptions;
            T? value = JsonSerializer.Deserialize<T>(bytes.ToArray(), options);
            return value is null ? (default, BadRequest()) : (value, null);
        }
        catch (JsonException) { return (default, BadRequest()); }
    }
}
