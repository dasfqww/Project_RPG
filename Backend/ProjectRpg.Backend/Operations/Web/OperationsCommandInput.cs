using System.Text.Json;
using Microsoft.AspNetCore.Antiforgery;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Backend.Operations.Web;

internal static class OperationsCommandInput
{
    internal static async Task<Result<T>> ReadAsync<T>(HttpContext context, IAntiforgery csrf) where T : class
    {
        if (!context.Request.HasJsonContentType() || !OperationsInput.KnownQuery(context)) return Result<T>.Fail(Failures.InvalidQuery);
        await csrf.ValidateRequestAsync(context);
        byte[] bytes = new byte[32 * 1024 + 1];
        int total = 0, read;
        while (total < bytes.Length && (read = await context.Request.Body.ReadAsync(bytes.AsMemory(total), context.RequestAborted)) > 0) total += read;
        if (total == bytes.Length) throw new BadHttpRequestException("Operations body limit exceeded.", 413);
        try
        {
            T? value = JsonSerializer.Deserialize<T>(bytes.AsSpan(0, total), OperationsHttp.Json);
            return value is null ? Result<T>.Fail(Failures.InvalidQuery) : Result<T>.Success(value);
        }
        catch (JsonException) { return Result<T>.Fail(Failures.InvalidQuery); }
    }
}
