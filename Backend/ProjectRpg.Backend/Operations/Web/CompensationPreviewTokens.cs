using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.AspNetCore.DataProtection;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Compensation;

namespace ProjectRpg.Backend.Operations.Web;

public sealed class CompensationPreviewTokens(IDataProtectionProvider protection, OperationsOptions options)
{
    private readonly IDataProtector protector = protection.CreateProtector("Operations.CompensationPreview.v1", options.EnvironmentId);
    public string Protect(CompensationPreview value) => protector.Protect(JsonSerializer.Serialize(value));
    public Result<CompensationPreview> Read(string? token)
    {
        if (token is not { Length: > 0 and <= 8192 }) return Result<CompensationPreview>.Fail(CompensationFailures.PreviewInvalid);
        try
        {
            var value = JsonSerializer.Deserialize<CompensationPreview>(protector.Unprotect(token));
            return value is not null && value.Intent is not null && value.OperatorId == options.OperatorId
                ? Result<CompensationPreview>.Success(value) : Result<CompensationPreview>.Fail(CompensationFailures.PreviewInvalid);
        }
        catch (Exception e) when (e is CryptographicException or JsonException or FormatException)
        { return Result<CompensationPreview>.Fail(CompensationFailures.PreviewInvalid); }
    }
}
