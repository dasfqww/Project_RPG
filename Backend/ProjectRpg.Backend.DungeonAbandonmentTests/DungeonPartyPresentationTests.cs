using System.Text.Json;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Domain;

internal static class DungeonPartyPresentationTests
{
    public static void Run()
    {
        int checks = 0;
        DateTimeOffset first = new(2026, 10, 25, 0, 0, 0, TimeSpan.Zero);
        Guid sessionId = Guid.NewGuid();
        var member = new DungeonSessionMember(Guid.NewGuid(), "76561198000000901", first,
            first.AddHours(1), first, ConnectionGeneration: long.MaxValue,
            ConfirmedConnectionGeneration: long.MaxValue, DisconnectedGeneration: long.MaxValue,
            CharacterName: "테스트 용사");
        var connected = new DungeonSessionMember(Guid.NewGuid(), "76561198000000902", first,
            first.AddHours(1), CharacterName: "다른 용사");
        var snapshot = new DungeonMembershipSnapshot(sessionId, "party-presentation-fixture",
            DungeonSessionState.InProgress, first, first.AddHours(1), [member, connected], [], []);
        DateTimeOffset observed = first.AddMinutes(3);
        var response = DungeonMembershipSnapshotApiEndpoints.ToResponse(snapshot, observed);
        Check(response.ObservedAt == observed, "response uses supplied server observation, not client wall time");
        var clockSnapshot = snapshot with { ObservedAt = first.AddMinutes(4) };
        Check(DungeonMembershipSnapshotApiEndpoints.ToResponse(clockSnapshot, DateTimeOffset.UtcNow).ObservedAt == clockSnapshot.ObservedAt,
            "repository connection-policy clock overrides wall-clock fallback");
        Check(response.Members[0].CharacterName == member.CharacterName, "authoritative character name is additive");
        Check(response.Members[0].DisconnectedAt == first, "original disconnect timestamp is preserved");
        Check(response.Members[0].ReconnectExpiresAt == first + DungeonConnectionPolicy.UnexpectedDisconnectGrace,
            "display deadline uses the existing server grace policy");
        Check(response.Members[0].ConnectionGeneration == long.MaxValue.ToString(System.Globalization.CultureInfo.InvariantCulture),
            "generation does not lose precision through JSON numbers");
        Check(response.Members[0].DisconnectedGeneration == response.Members[0].ConnectionGeneration,
            "disconnect generation also retains exact int64 precision");
        Check(response.Members[1].DisconnectedAt is null && response.Members[1].ReconnectExpiresAt is null,
            "connected records do not invent a disconnect or timer");
        Check(response.Members[1].ConnectionGeneration == "0", "legacy generation remains explicitly zero");
        Check(response.Members[1].DisconnectedGeneration is null, "connected members have no disconnect generation");
        Check(snapshot.Members.Count == 2 && snapshot.Members[0] == member && snapshot.State == DungeonSessionState.InProgress,
            "mapping does not change membership or expire the session");
        var afterDeadline = DungeonMembershipSnapshotApiEndpoints.ToResponse(snapshot, first.AddMinutes(20));
        Check(afterDeadline.Members.Count == 2 && afterDeadline.DisconnectionExpulsions.Count == 0,
            "elapsed display time is not an expulsion transaction or receipt");
        var oversized = snapshot with { Members = [member with { CharacterName = new string('x', 2000) }] };
        Check(DungeonMembershipSnapshotApiEndpoints.ToResponse(oversized, observed).Members[0].CharacterName is null,
            "presentation name cannot inflate the existing bounded read body");
        var reconnecting = snapshot with
        {
            Members = [member with { ConnectionGeneration = 2, ConfirmedConnectionGeneration = 1, DisconnectedGeneration = 1 }]
        };
        var beforeConfirmation = DungeonMembershipSnapshotApiEndpoints.ToResponse(reconnecting, observed).Members[0];
        Check(beforeConfirmation.ConnectionGeneration == "2" && beforeConfirmation.DisconnectedGeneration == "1",
            "ticket consumption does not reattribute an old disconnect to the new connection");
        var disconnectedAgain = reconnecting with
        {
            Members = [reconnecting.Members[0] with { ConfirmedConnectionGeneration = 2, DisconnectedGeneration = 2 }]
        };
        Check(DungeonMembershipSnapshotApiEndpoints.ToResponse(disconnectedAgain, observed).Members[0].DisconnectedGeneration == "2",
            "the new connection's own disconnect remains visible");
        var receipt = new DungeonAbandonment(sessionId, Guid.NewGuid(), "76561198000000903", first, DungeonSessionState.InProgress);
        var extreme = snapshot with
        {
            Members = [member with { DisconnectedAt = DateTimeOffset.MaxValue }], Abandonments = [receipt]
        };
        var extremeResponse = DungeonMembershipSnapshotApiEndpoints.ToResponse(extreme, observed);
        Check(extremeResponse.Members.Count == 1 && extremeResponse.Members[0].ReconnectExpiresAt is null,
            "unrepresentable optional display deadline does not fail the required membership read");
        Check(extremeResponse.Members[0].DisconnectedAt == DateTimeOffset.MaxValue
            && extremeResponse.Abandonments.Single().CharacterId == receipt.CharacterId,
            "deadline overflow neither rewrites stored evidence nor hides immutable receipts");
        var offset = snapshot with { Members = [member with { DisconnectedAt = first.ToOffset(TimeSpan.FromHours(9)) }] };
        Check(DungeonMembershipSnapshotApiEndpoints.ToResponse(offset, observed).Members[0].ReconnectExpiresAt
            is { Offset: var deadlineOffset } && deadlineOffset == TimeSpan.Zero,
            "display deadline is emitted in UTC");
        using var document = JsonDocument.Parse(JsonSerializer.Serialize(response, new JsonSerializerOptions(JsonSerializerDefaults.Web)));
        Check(document.RootElement.GetProperty("members")[0].GetProperty("connectionGeneration").ValueKind == JsonValueKind.String,
            "wire generation is an exact string");
        Check(document.RootElement.GetProperty("members")[0].GetProperty("disconnectedGeneration").ValueKind == JsonValueKind.String,
            "wire disconnect generation is also an exact string");
        Check(document.RootElement.TryGetProperty("observedAt", out _), "wire observation is additive");
        Console.WriteLine($"PASS: party presentation mapping ({checks} focused checks; no HTTP/DB/expiry mutation).");

        void Check(bool condition, string boundary)
        {
            if (!condition) throw new InvalidOperationException($"Party presentation failed: {boundary}.");
            checks++;
        }
    }
}
