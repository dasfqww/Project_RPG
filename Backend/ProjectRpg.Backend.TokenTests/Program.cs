using System.Text.Json;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Contracts;

try
{
    FixedClock clock = new() { Now = new DateTimeOffset(2026, 10, 8, 0, 0, 0, TimeSpan.Zero) };
    AccessTokenService service = new(clock);
    int passed = 0;
    void Test(string label, Action body)
    {
        body();
        Console.WriteLine($"PASS Token: {label}");
        passed++;
    }

    Test("issued and expiry timestamps come from one server clock sample", () =>
    {
        IssuedAccessToken token = service.Issue(TimeSpan.FromMinutes(60));
        Require(clock.Reads == 1 && token.IssuedAt == clock.Now
            && token.ExpiresAt - token.IssuedAt == TimeSpan.FromMinutes(60));
    });

    Test("configured player lifetimes retain exact relative metadata", () =>
    {
        foreach (int minutes in new[] { 5, 60, 24 * 60 })
        {
            int reads = clock.Reads;
            IssuedAccessToken token = service.Issue(TimeSpan.FromMinutes(minutes));
            Require(clock.Reads == reads + 1 && token.ExpiresAt - token.IssuedAt == TimeSpan.FromMinutes(minutes));
        }
    });

    Test("server and join-ticket lifetimes are unchanged", () =>
    {
        foreach (TimeSpan lifetime in new[] { TimeSpan.FromMinutes(720), TimeSpan.FromSeconds(60) })
        {
            IssuedAccessToken token = service.Issue(lifetime);
            Require(token.ExpiresAt - token.IssuedAt == lifetime);
        }
    });

    Test("opaque token randomness, header safety and SHA256 hashing are unchanged", () =>
    {
        IssuedAccessToken first = service.Issue(TimeSpan.FromMinutes(60));
        IssuedAccessToken second = service.Issue(TimeSpan.FromMinutes(60));
        Require(first.Token != second.Token && first.Token.Length == 43);
        Require(first.Token.All(character => char.IsAsciiLetterOrDigit(character) || character is '-' or '_'));
        Require(first.TokenHash.Length == 64 && first.TokenHash == service.Hash(first.Token));
    });

    Test("authentication JSON adds issuedAt without changing existing fields or exposing hashes", () =>
    {
        IssuedAccessToken token = service.Issue(TimeSpan.FromMinutes(60));
        SteamTicketResponse response = new(token.Token, token.ExpiresAt, "76561198000000001", token.IssuedAt);
        JsonSerializerOptions options = new(JsonSerializerDefaults.Web);
        string body = JsonSerializer.Serialize(response, options);
        using JsonDocument json = JsonDocument.Parse(body);
        Require(json.RootElement.EnumerateObject().Count() == 4);
        Require(json.RootElement.GetProperty("issuedAt").GetDateTimeOffset() == token.IssuedAt);
        Require(json.RootElement.GetProperty("expiresAt").GetDateTimeOffset() == token.ExpiresAt);
        Require(json.RootElement.GetProperty("accessToken").GetString() == token.Token);
        Require(json.RootElement.GetProperty("steamId").GetString() == response.SteamId);
        Require(!json.RootElement.TryGetProperty("tokenHash", out _));
        Require(JsonSerializer.Deserialize<SteamTicketResponse>(body, options) == response);
    });

    Test("absolute clock offsets do not change the lifetime represented by metadata", () =>
    {
        foreach (int year in new[] { 2000, 2026, 2200 })
        {
            clock.Now = new DateTimeOffset(year, 1, 1, 0, 0, 0, TimeSpan.Zero);
            IssuedAccessToken token = service.Issue(TimeSpan.FromMinutes(60));
            Require(token.IssuedAt == clock.Now && token.ExpiresAt - token.IssuedAt == TimeSpan.FromMinutes(60));
        }
    });
    Console.WriteLine($"PASS Token suite: {passed} tests; no database or OS clock changes.");
    return 0;
}
catch (Exception exception)
{
    // Credentials and serialized authentication bodies never appear in test output.
    Console.Error.WriteLine($"FAIL Token: {exception.GetType().Name}");
    return 1;
}

static void Require(bool condition)
{
    if (!condition) throw new InvalidOperationException("Token metadata assertion failed.");
}

sealed class FixedClock : TimeProvider
{
    public DateTimeOffset Now { get; set; }
    public int Reads { get; private set; }
    public override DateTimeOffset GetUtcNow() { Reads++; return Now; }
}
