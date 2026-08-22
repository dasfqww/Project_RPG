using System.Net.Http.Json;
using System.Text.Json.Serialization;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Authentication;

public interface ISteamTicketValidator
{
    Task<SteamIdentity> ValidateAsync(string ticketHex, CancellationToken cancellationToken);
}

public sealed class SteamTicketRejectedException(string message) : Exception(message);
public sealed class SteamAuthenticationUnavailableException(string message) : Exception(message);

public sealed class SteamTicketValidator(
    HttpClient httpClient,
    IOptions<SteamOptions> options,
    IHostEnvironment environment,
    ILogger<SteamTicketValidator> logger) : ISteamTicketValidator
{
    private sealed record SteamResponseEnvelope(
        [property: JsonPropertyName("response")] SteamResponse Response);

    private sealed record SteamResponse(
        [property: JsonPropertyName("params")] SteamResponseParameters? Parameters,
        [property: JsonPropertyName("error")] SteamResponseError? Error);

    private sealed record SteamResponseParameters(
        [property: JsonPropertyName("result")] string Result,
        [property: JsonPropertyName("steamid")] string SteamId,
        [property: JsonPropertyName("ownersteamid")] string OwnerSteamId,
        [property: JsonPropertyName("vacbanned")] bool VacBanned,
        [property: JsonPropertyName("publisherbanned")] bool PublisherBanned);

    private sealed record SteamResponseError(
        [property: JsonPropertyName("errorcode")] int ErrorCode,
        [property: JsonPropertyName("errordesc")] string ErrorDescription);

    private readonly SteamOptions _options = options.Value;

    public async Task<SteamIdentity> ValidateAsync(
        string ticketHex,
        CancellationToken cancellationToken)
    {
        string normalizedTicket = ticketHex.Trim();

        if (environment.IsDevelopment() && _options.AllowDevelopmentAuthentication
            && normalizedTicket.StartsWith("dev:", StringComparison.Ordinal))
        {
            string steamId = normalizedTicket[4..];
            if (!ulong.TryParse(steamId, out _))
            {
                throw new SteamTicketRejectedException("Development Steam ID is invalid.");
            }

            return new SteamIdentity(steamId, steamId, false, false);
        }

        ValidateTicketHex(normalizedTicket);
        if (_options.AppId == 0 || string.IsNullOrWhiteSpace(_options.PublisherKey))
        {
            throw new SteamAuthenticationUnavailableException(
                "Steam AppID or publisher API key is not configured.");
        }

        string query = string.Join('&',
            $"key={Uri.EscapeDataString(_options.PublisherKey)}",
            $"appid={_options.AppId}",
            $"ticket={Uri.EscapeDataString(normalizedTicket)}",
            $"identity={Uri.EscapeDataString(_options.TicketIdentity)}");
        string separator = _options.AuthenticateUserTicketUrl.Contains('?', StringComparison.Ordinal)
            ? "&"
            : "?";
        string requestUrl = _options.AuthenticateUserTicketUrl + separator + query;

        using HttpResponseMessage response = await httpClient.GetAsync(requestUrl, cancellationToken);
        if (!response.IsSuccessStatusCode)
        {
            logger.LogWarning(
                "Steam ticket authentication returned HTTP {StatusCode}.",
                (int)response.StatusCode);
            throw new SteamAuthenticationUnavailableException(
                "Steam ticket authentication service is unavailable.");
        }

        SteamResponseEnvelope? envelope = await response.Content
            .ReadFromJsonAsync<SteamResponseEnvelope>(cancellationToken);
        SteamResponseParameters? parameters = envelope?.Response.Parameters;
        if (parameters is null
            || !string.Equals(parameters.Result, "OK", StringComparison.OrdinalIgnoreCase)
            || string.IsNullOrWhiteSpace(parameters.SteamId))
        {
            string error = envelope?.Response.Error?.ErrorDescription
                ?? "Steam rejected the authentication ticket.";
            throw new SteamTicketRejectedException(error);
        }

        if (parameters.PublisherBanned)
        {
            throw new SteamTicketRejectedException("The Steam account is publisher banned.");
        }

        return new SteamIdentity(
            parameters.SteamId,
            string.IsNullOrWhiteSpace(parameters.OwnerSteamId)
                ? parameters.SteamId
                : parameters.OwnerSteamId,
            parameters.VacBanned,
            parameters.PublisherBanned);
    }

    private static void ValidateTicketHex(string ticketHex)
    {
        if (ticketHex.Length is < 2 or > 16384 || ticketHex.Length % 2 != 0)
        {
            throw new SteamTicketRejectedException("Steam ticket has an invalid length.");
        }

        foreach (char character in ticketHex)
        {
            if (!Uri.IsHexDigit(character))
            {
                throw new SteamTicketRejectedException("Steam ticket must be hexadecimal.");
            }
        }
    }
}
