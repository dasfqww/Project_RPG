using System.Security.Claims;
using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.DataProtection;
using Microsoft.AspNetCore.WebUtilities;
using ProjectRpg.Backend.Operations.Hosting;

namespace ProjectRpg.Backend.Operations.Infrastructure.Authentication;

public sealed class OperationsTicketStore(IOperatorSessionRepository repository, IDataProtectionProvider protection,
    OperationsOptions options, TimeProvider clock, IHttpContextAccessor context) : ITicketStore
{
    private readonly IDataProtector protector = protection.CreateProtector("Operations.ServerTicket.v1", options.EnvironmentId);
    private CancellationToken Ct => context.HttpContext?.RequestAborted ?? CancellationToken.None;

    public async Task<string> StoreAsync(AuthenticationTicket ticket)
    {
        if (ticket.Principal.FindFirstValue(ClaimTypes.NameIdentifier) != options.OperatorId)
            throw new InvalidOperationException("An unverified operator cannot create a session.");
        DateTimeOffset now = clock.GetUtcNow();
        ticket.Properties.IssuedUtc = now;
        ticket.Properties.ExpiresUtc = now.AddHours(8);
        ticket.Properties.IsPersistent = false;
        string key = WebEncoders.Base64UrlEncode(RandomNumberGenerator.GetBytes(32));
        await repository.StoreAsync(new(Hash(key), options.OperatorId, protector.Protect(TicketSerializer.Default.Serialize(ticket)),
            options.AuthEpoch, now, now, now.AddHours(8)), Ct);
        return key;
    }
    public Task RenewAsync(string key, AuthenticationTicket ticket) =>
        throw new InvalidOperationException("Operations absolute sessions do not slide or renew.");
    public async Task<AuthenticationTicket?> RetrieveAsync(string key)
    {
        if (key.Length != 43) return null;
        OperatorSession? stored = await repository.TouchAsync(Hash(key), options.OperatorId, options.AuthEpoch, clock.GetUtcNow(), Ct);
        if (stored is null) return null;
        try { return TicketSerializer.Default.Deserialize(protector.Unprotect(stored.ProtectedTicket)); }
        catch (CryptographicException) { return null; }
    }
    public Task RemoveAsync(string key) => repository.RevokeAsync(Hash(key), clock.GetUtcNow(), Ct);
    private static byte[] Hash(string key) => SHA256.HashData(Encoding.UTF8.GetBytes(key));
}
