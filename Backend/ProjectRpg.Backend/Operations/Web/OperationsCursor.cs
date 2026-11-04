using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.AspNetCore.DataProtection;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Backend.Operations.Web;

public sealed record CursorState<T>(T? Position, DateTimeOffset IssuedAt, DateTimeOffset ExpiresAt);
public sealed class OperationsCursor(IDataProtectionProvider protection, OperationsOptions options, TimeProvider clock)
{
    private readonly IDataProtector protector = protection.CreateProtector("Operations.Cursor.v1", options.EnvironmentId);
    private sealed record Envelope<T>(int Version, string OperatorId, string Query, T Position, DateTimeOffset IssuedAt, DateTimeOffset ExpiresAt);

    public Result<CursorState<T>> Read<T>(string? token, string actor, string query)
    {
        DateTimeOffset now = clock.GetUtcNow();
        if (string.IsNullOrEmpty(token)) return Result<CursorState<T>>.Success(new(default, now, now.AddMinutes(15)));
        if (token.Length > 8192) return Result<CursorState<T>>.Fail(Failures.InvalidCursor);
        try
        {
            Envelope<T>? value = JsonSerializer.Deserialize<Envelope<T>>(protector.Unprotect(token));
            if (value is null || value.Version != 1 || value.OperatorId != actor || value.Query != query || value.Position is null
                || value.IssuedAt > now || value.ExpiresAt - value.IssuedAt != TimeSpan.FromMinutes(15))
                return Result<CursorState<T>>.Fail(Failures.InvalidCursor);
            if (value.ExpiresAt <= now) return Result<CursorState<T>>.Fail(Failures.CursorExpired);
            return Result<CursorState<T>>.Success(new(value.Position, value.IssuedAt, value.ExpiresAt));
        }
        catch (Exception error) when (error is CryptographicException or JsonException or FormatException)
        { return Result<CursorState<T>>.Fail(Failures.InvalidCursor); }
    }
    public string? Write<T>(T? position, CursorState<T> state, string actor, string query)
    {
        if (position is null) return null;
        string token = protector.Protect(JsonSerializer.Serialize(new Envelope<T>(1, actor, query, position, state.IssuedAt, state.ExpiresAt)));
        if (token.Length > 8192) throw new InvalidOperationException("Operations cursor exceeded its bounded contract.");
        return token;
    }
}
