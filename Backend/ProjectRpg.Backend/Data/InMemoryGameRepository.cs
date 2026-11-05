using System.Collections.Concurrent;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository(TimeProvider? connectionClock = null) : IGameRepository, IDungeonPickupIntentRepository
{
    private readonly InMemoryItemRepository? _starterItems;
    private readonly InMemoryTransactionGate? _starterTransactionGate;

    public InMemoryGameRepository(
        InMemoryTransactionGate transactionGate,
        InMemoryItemRepository itemRepository, TimeProvider? operationsClock = null) : this(operationsClock)
    {
        _starterTransactionGate = transactionGate;
        _starterItems = itemRepository;
        if (!ReferenceEquals(itemRepository.TransactionSyncRoot, transactionGate.SyncRoot))
        {
            throw new ArgumentException("Character and item repositories must share a transaction gate.");
        }
    }

    private sealed record AccountRecord(
        SteamIdentity Identity,
        DateTimeOffset CreatedAt,
        DateTimeOffset LastLoginAt);

    private sealed record SessionRecord(string SteamId, DateTimeOffset ExpiresAt);

    private sealed record GameServerCredentialRecord(
        string ServerId,
        Guid DungeonSessionId,
        DateTimeOffset ExpiresAt);

    private sealed record JoinTicketRecord(
        string SteamId,
        Guid CharacterId,
        Guid DungeonSessionId,
        DateTimeOffset ExpiresAt);

    private sealed record CharacterLeaseRecord(
        Guid DungeonSessionId,
        DateTimeOffset ExpiresAt);

    private sealed record JoinTicketConsumptionRecord(
        string TokenHash, ConsumedJoinTicket Admission, DateTimeOffset ConsumedAt, DateTimeOffset RecoveryExpiresAt);

    private readonly Dictionary<(Guid SessionId, string ServerId, Guid RequestId), JoinTicketConsumptionRecord>
        _joinTicketConsumptions = new();

    private readonly ConcurrentDictionary<string, AccountRecord> _accounts = new();
    private readonly ConcurrentDictionary<string, SessionRecord> _sessions = new();
    private readonly ConcurrentDictionary<string, GameServerCredentialRecord>
        _gameServerCredentials = new();
    private readonly ConcurrentDictionary<string, JoinTicketRecord> _joinTickets = new();
    private readonly ConcurrentDictionary<Guid, Roster> _rosters = new();
    private readonly ConcurrentDictionary<Guid, GameCharacter> _characters = new();
    private readonly ConcurrentDictionary<Guid, IReadOnlyList<InventoryItem>> _inventories = new();
    private readonly Dictionary<Guid, DungeonSession> _dungeonSessions = new();
    private readonly Dictionary<Guid, DungeonRewardSettlement>
        _dungeonRewardSettlements = new();
    private readonly Dictionary<Guid, CharacterLeaseRecord> _characterLeases = new();
    private readonly Dictionary<(Guid SessionId, Guid CharacterId), DungeonAbandonment>
        _dungeonAbandonments = new();
    private readonly Dictionary<(Guid SessionId, Guid CharacterId), DungeonDisconnectionExpulsion>
        _dungeonDisconnectionExpulsions = new();
    private readonly object _characterGate = new();
    private readonly object _dungeonGate = new();

    public Task EnsureCreatedAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    public Task UpsertAccountAsync(
        SteamIdentity identity,
        DateTimeOffset authenticatedAt,
        CancellationToken cancellationToken)
    {

        lock (_dungeonGate)
        {
            EnsureAccountAllowed(identity.SteamId, authenticatedAt);
            _accounts.AddOrUpdate(
                identity.SteamId,
                _ => new AccountRecord(identity, authenticatedAt, authenticatedAt),
                (_, existing) => existing with
                {
                    Identity = identity,
                    LastLoginAt = authenticatedAt
                });
            lock (_characterGate)
            {
                if (!_rosters.Values.Any(roster =>
                    roster.SteamId == identity.SteamId
                    && roster.WorldId == "main"))
                {
                    Roster roster = new(
                        Guid.NewGuid(),
                        identity.SteamId,
                        "main",
                        authenticatedAt);
                    _rosters[roster.RosterId] = roster;
                }
            }
            return Task.CompletedTask;
        }
    }

    public Task StoreSessionAsync(
        string tokenHash,
        string steamId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {

        lock (_dungeonGate)
        {
            EnsureAccountAllowed(steamId, connectionClock?.GetUtcNow() ?? DateTimeOffset.UtcNow);
            _sessions[tokenHash] = new SessionRecord(steamId, expiresAt);
            return Task.CompletedTask;
        }
    }

    public Task<string?> ResolveSessionAsync(
        string tokenHash,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {

        lock (_dungeonGate)
        {
            if (!_sessions.TryGetValue(tokenHash, out SessionRecord? session))
            {
                return Task.FromResult<string?>(null);
            }

            if (!AccountAllowed(session.SteamId, now)) return Task.FromResult<string?>(null);
            if (session.ExpiresAt <= now)
            {
                _sessions.TryRemove(tokenHash, out _);
                return Task.FromResult<string?>(null);
            }

            return Task.FromResult<string?>(session.SteamId);
        }
    }

    public Task RevokeSessionAsync(string tokenHash, string steamId, CancellationToken cancellationToken)
    {

        lock (_dungeonGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (_sessions.TryGetValue(tokenHash, out SessionRecord? session) && session.SteamId == steamId)
                _sessions.TryRemove(new KeyValuePair<string, SessionRecord>(tokenHash, session));
            return Task.CompletedTask;
        }
    }

    public Task StoreGameServerCredentialAsync(
        string tokenHash,
        string serverId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            foreach ((string hash, GameServerCredentialRecord credential) in
                _gameServerCredentials.ToArray())
            {
                if (credential.DungeonSessionId == dungeonSessionId)
                {
                    _gameServerCredentials.TryRemove(hash, out _);
                }
            }

            _gameServerCredentials[tokenHash] = new GameServerCredentialRecord(
                serverId,
                dungeonSessionId,
                expiresAt);
        }

        return Task.CompletedTask;
    }

    public Task<GameServerCredential?> ResolveGameServerCredentialAsync(
        string tokenHash,
        DateTimeOffset now,
        TimeSpan postSessionGrace,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            if (!_gameServerCredentials.TryGetValue(
                    tokenHash,
                    out GameServerCredentialRecord? credential)
                || credential.ExpiresAt <= now
                || !_dungeonSessions.TryGetValue(
                    credential.DungeonSessionId,
                    out DungeonSession? session)
                || !string.Equals(
                    session.ServerId,
                    credential.ServerId,
                    StringComparison.Ordinal))
            {
                _gameServerCredentials.TryRemove(tokenHash, out _);
                return Task.FromResult<GameServerCredential?>(null);
            }

            bool isActive = session.State is (
                    DungeonSessionState.Loading
                    or DungeonSessionState.InProgress)
                && session.ExpiresAt > now;
            bool isWithinPostSessionGrace = session.State is (
                    DungeonSessionState.Cleared
                    or DungeonSessionState.Failed)
                && session.UpdatedAt >= now - postSessionGrace;
            bool isSettling = session.State == DungeonSessionState.SettlementPending
                && session.UpdatedAt >= now - postSessionGrace;
            bool isAbandonedClosed = session.State == DungeonSessionState.Closed
                && session.UpdatedAt >= now - postSessionGrace
                && _dungeonAbandonments.Values.Any(receipt =>
                    receipt.DungeonSessionId == session.DungeonSessionId
                    && receipt.SessionStateAtAbandonment == DungeonSessionState.Closed);
            bool isExpelledClosed = session.State == DungeonSessionState.Closed
                && session.UpdatedAt >= now - postSessionGrace
                && _dungeonDisconnectionExpulsions.Values.Any(receipt =>
                    receipt.DungeonSessionId == session.DungeonSessionId);
            if (!isActive && !isSettling && !isWithinPostSessionGrace && !isAbandonedClosed && !isExpelledClosed)
            {
                // A temporarily unreadable pending settlement can later complete. Do not
                // destroy its still-unexpired credential while the worker is retrying.
                if (session.State != DungeonSessionState.SettlementPending)
                    _gameServerCredentials.TryRemove(tokenHash, out _);
                return Task.FromResult<GameServerCredential?>(null);
            }

            return Task.FromResult<GameServerCredential?>(new(
                credential.ServerId,
                credential.DungeonSessionId,
                IsSecurityTelemetryOnly: !isActive));
        }
    }

    public Task<IReadOnlyList<GameCharacter>> GetCharactersAsync(
        string steamId,
        CancellationToken cancellationToken)
    {
        IReadOnlyList<GameCharacter> characters = _characters.Values
            .Where(character => character.SteamId == steamId)
            .OrderBy(character => character.CreatedAt)
            .ToArray();
        return Task.FromResult(characters);
    }

    public Task<IReadOnlyList<Roster>> GetRostersAsync(
        string steamId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        IReadOnlyList<Roster> rosters = _rosters.Values
            .Where(roster => roster.SteamId == steamId)
            .OrderBy(roster => roster.WorldId, StringComparer.Ordinal)
            .ToArray();
        return Task.FromResult(rosters);
    }

    public Task<GameCharacter> CreateCharacterAsync(
        string steamId,
        string name,
        DateTimeOffset createdAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_starterTransactionGate?.SyncRoot ?? _characterGate)
        {
            lock (_characterGate)
            {
                bool nameExists = _characters.Values.Any(character =>
                    string.Equals(character.Name, name, StringComparison.OrdinalIgnoreCase));
                if (nameExists)
                {
                    throw new DuplicateCharacterNameException(name);
                }

                Roster roster = _rosters.Values.SingleOrDefault(candidate =>
                    candidate.SteamId == steamId
                    && candidate.WorldId == "main")
                    ?? throw new InvalidOperationException(
                        "The account does not have a default roster.");
                GameCharacter character = new(
                    Guid.NewGuid(),
                    roster.RosterId,
                    steamId,
                    name,
                    createdAt);
                _starterItems?.SeedStarters(FirstSliceStarterEquipment.Create(character.CharacterId));
                _progression[character.CharacterId] = FirstSliceProgression.Create(character.CharacterId);
                _characters[character.CharacterId] = character;
                return Task.FromResult(character);
            }
        }
    }

    public Task<string?> GetCharacterOwnerAsync(
        Guid characterId,
        CancellationToken cancellationToken)
    {
        string? owner = _characters.TryGetValue(characterId, out GameCharacter? character)
            ? character.SteamId
            : null;
        return Task.FromResult(owner);
    }

    public Task<CharacterEconomyContext?> GetCharacterEconomyContextAsync(
        Guid characterId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        CharacterEconomyContext? context = _characters.TryGetValue(
            characterId,
            out GameCharacter? character)
            ? new CharacterEconomyContext(
                character.CharacterId,
                character.RosterId,
                character.SteamId)
            : null;
        return Task.FromResult(context);
    }

    public Task<DungeonSession?> GetDungeonSessionAsync(
        Guid dungeonSessionId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            _dungeonSessions.TryGetValue(
                dungeonSessionId,
                out DungeonSession? session);
            return Task.FromResult(session);
        }
    }

    public Task<DungeonMembershipSnapshot?> GetDungeonMembershipSnapshotAsync(
        Guid dungeonSessionId,
        string serverId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (string.IsNullOrWhiteSpace(serverId)) throw new DungeonSessionServerMismatchException(dungeonSessionId);
        lock (_dungeonGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!_dungeonSessions.TryGetValue(dungeonSessionId, out DungeonSession? session))
                return Task.FromResult<DungeonMembershipSnapshot?>(null);
            EnsureServerMatches(session, serverId);
            // Do not clean up leases or expire sessions from this diagnostic GET.
            return Task.FromResult<DungeonMembershipSnapshot?>(new(
                session.DungeonSessionId, serverId, session.State, session.UpdatedAt, session.ExpiresAt,
                session.Members.OrderBy(member => member.CharacterId).ToArray(),
                _dungeonAbandonments.Values.Where(receipt => receipt.DungeonSessionId == dungeonSessionId)
                    .OrderBy(receipt => receipt.CharacterId).ToArray(),
                _dungeonDisconnectionExpulsions.Values.Where(receipt => receipt.DungeonSessionId == dungeonSessionId)
                    .OrderBy(receipt => receipt.CharacterId).ToArray(), connectionClock?.GetUtcNow() ?? DateTimeOffset.UtcNow));
        }
    }

    public Task<DungeonSession?> GetActiveDungeonSessionForCharacterAsync(
        Guid characterId,
        string steamId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            if (!_characterLeases.TryGetValue(
                    characterId,
                    out CharacterLeaseRecord? lease)
                || lease.ExpiresAt <= now)
            {
                return Task.FromResult<DungeonSession?>(null);
            }

            if (!_dungeonSessions.TryGetValue(
                    lease.DungeonSessionId,
                    out DungeonSession? session)
                || session.State is not (
                    DungeonSessionState.Waiting
                    or DungeonSessionState.Loading
                    or DungeonSessionState.InProgress)
                || session.ExpiresAt <= now)
            {
                return Task.FromResult<DungeonSession?>(null);
            }

            if (!session.Members.Any(member =>
                    member.CharacterId == characterId
                    && string.Equals(
                        member.SteamId,
                        steamId,
                        StringComparison.Ordinal)
                    && member.LeaseExpiresAt > now))
            {
                return Task.FromResult<DungeonSession?>(null);
            }

            return Task.FromResult<DungeonSession?>(session);
        }
    }

    public Task<DungeonSession> CreateDungeonSessionAsync(
        string steamId,
        Guid characterId,
        string dungeonId,
        string difficulty,
        DateTimeOffset now,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            EnsureAccountAllowed(steamId, connectionClock?.GetUtcNow() ?? now);
            CleanupExpiredDungeonSessions(now);
            AcquireCharacterLease(characterId, Guid.Empty, now, expiresAt);

            Guid dungeonSessionId = Guid.NewGuid();
            _characterLeases[characterId] = new CharacterLeaseRecord(
                dungeonSessionId,
                expiresAt);
            DungeonSessionMember member = new(
                characterId,
                steamId,
                now,
                expiresAt,
                CharacterName: GetDungeonMemberCharacterName(characterId, steamId));
            DungeonSession session = new(
                dungeonSessionId,
                dungeonId,
                difficulty,
                DungeonSessionState.Waiting,
                null,
                null,
                now,
                now,
                expiresAt,
                [member], characterId, 1);
            _dungeonSessions[dungeonSessionId] = session;
            return Task.FromResult(session);
        }
    }

    public Task<DungeonSession> JoinDungeonSessionAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        int maxPartySize,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            EnsureAccountAllowed(steamId, connectionClock?.GetUtcNow() ?? now);
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            if (session.State != DungeonSessionState.Waiting || session.DepartureRequestedAt.HasValue)
            {
                throw new DungeonSessionNotJoinableException(dungeonSessionId);
            }

            if (session.Members.Any(member =>
                member.CharacterId == characterId
                && member.SteamId == steamId))
            {
                return Task.FromResult(session);
            }

            if (session.Members.Count >= maxPartySize)
            {
                throw new DungeonSessionFullException(dungeonSessionId);
            }

            AcquireCharacterLease(
                characterId,
                dungeonSessionId,
                now,
                leaseExpiresAt);
            _characterLeases[characterId] = new CharacterLeaseRecord(
                dungeonSessionId,
                leaseExpiresAt);
            DungeonSessionMember[] members = session.Members
                .Select(member => member with
                {
                    LeaseExpiresAt = leaseExpiresAt
                })
                .Append(new DungeonSessionMember(
                    characterId,
                    steamId,
                    now,
                    leaseExpiresAt,
                    CharacterName: GetDungeonMemberCharacterName(characterId, steamId)))
                .ToArray();
            foreach (DungeonSessionMember member in members)
            {
                _characterLeases[member.CharacterId] =
                    new CharacterLeaseRecord(
                        dungeonSessionId,
                        leaseExpiresAt);
            }
            session = DungeonDeparturePolicy.WithRoster(session, members, now) with { ExpiresAt = leaseExpiresAt };
            _dungeonSessions[dungeonSessionId] = session;
            return Task.FromResult(session);
        }
    }

    public Task<DungeonSession> LeaveDungeonSessionAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            if (session.State != DungeonSessionState.Waiting)
            {
                throw new DungeonSessionNotJoinableException(dungeonSessionId);
            }

            int removed = session.Members.Count(member =>
                member.CharacterId == characterId
                && member.SteamId == steamId);
            if (removed == 0)
            {
                throw new DungeonSessionMembershipException(dungeonSessionId);
            }

            DungeonSessionMember[] members = session.Members
                .Where(member => member.CharacterId != characterId)
                .ToArray();
            if (_characterLeases.TryGetValue(
                    characterId,
                    out CharacterLeaseRecord? lease)
                && lease.DungeonSessionId == dungeonSessionId)
            {
                _characterLeases.Remove(characterId);
            }

            session = DungeonDeparturePolicy.WithRoster(session, members, now) with
            {
                State = members.Length == 0
                    ? DungeonSessionState.Closed
                    : session.State,
                UpdatedAt = now,
                ExpiresAt = members.Length == 0 ? now : session.ExpiresAt
            };
            _dungeonSessions[dungeonSessionId] = session;
            return Task.FromResult(session);
        }
    }

    public Task<DungeonAbandonment> AbandonDungeonSessionAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            if (_dungeonAbandonments.TryGetValue((dungeonSessionId, characterId), out DungeonAbandonment? existing))
            {
                if (!string.Equals(existing.SteamId, steamId, StringComparison.Ordinal))
                    throw new DungeonSessionMembershipException(dungeonSessionId);
                return Task.FromResult(existing);
            }
            if (session.State is not (DungeonSessionState.Loading or DungeonSessionState.InProgress))
                throw new DungeonSessionStateConflictException(dungeonSessionId, session.State);
            if (!session.Members.Any(member => member.CharacterId == characterId && member.SteamId == steamId))
                throw new DungeonSessionMembershipException(dungeonSessionId);

            // The same gate protects reward recipient capture, heartbeat and admission.
            // Once settlement wins this race, its immutable recipients cannot be edited.
            DungeonSessionMember[] members = session.Members.Where(member => member.CharacterId != characterId).ToArray();
            if (_characterLeases.TryGetValue(characterId, out CharacterLeaseRecord? lease)
                && lease.DungeonSessionId == dungeonSessionId)
                _characterLeases.Remove(characterId);
            foreach ((string hash, JoinTicketRecord ticket) in _joinTickets)
                if (ticket.DungeonSessionId == dungeonSessionId && ticket.CharacterId == characterId)
                    _joinTickets.TryRemove(hash, out _);
            session = session with
            {
                Members = members,
                State = members.Length == 0 ? DungeonSessionState.Closed : session.State,
                UpdatedAt = now,
                ExpiresAt = members.Length == 0 ? now : session.ExpiresAt
            };
            DungeonAbandonment receipt = new(dungeonSessionId, characterId, steamId, now, session.State);
            _dungeonSessions[dungeonSessionId] = session;
            _dungeonAbandonments.Add((dungeonSessionId, characterId), receipt);
            return Task.FromResult(receipt);
        }
    }

    public Task<DateTimeOffset> RecordDungeonMemberDisconnectionAsync(
        Guid dungeonSessionId,
        string serverId,
        string steamId,
        Guid characterId,
        long connectionGeneration,
        DateTimeOffset now,
        CancellationToken cancellationToken,
        DateTimeOffset? observedAtUtc = null)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            now = connectionClock?.GetUtcNow() ?? now;
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            EnsureServerMatches(session, serverId);
            if (session.State is not (DungeonSessionState.Loading or DungeonSessionState.InProgress))
                throw new DungeonSessionStateConflictException(dungeonSessionId, session.State);
            DungeonSessionMember? member = session.Members.SingleOrDefault(value =>
                value.CharacterId == characterId && value.SteamId == steamId);
            if (member is null || connectionGeneration <= 0
                || member.ConnectionGeneration != connectionGeneration)
                throw new DungeonSessionMembershipException(dungeonSessionId);

            // Duplicate delivery cannot restart the ten-minute grace clock.
            DateTimeOffset disconnectedAt = member.DisconnectedAt
                ?? DungeonConnectionPolicy.ResolveObservation(observedAtUtc, member.JoinedAt, now);
            if (member.DisconnectedAt is null
                || member.DisconnectedGeneration != connectionGeneration)
                _dungeonSessions[dungeonSessionId] = session with
                {
                    Members = session.Members.Select(value => value.CharacterId == characterId
                        ? value with
                        {
                            DisconnectedAt = disconnectedAt,
                            DisconnectedGeneration = connectionGeneration
                        } : value).ToArray()
                };
            return Task.FromResult(disconnectedAt);
        }
    }

    public Task<IReadOnlyList<DungeonDisconnectionExpulsion>> ExpelDisconnectedDungeonMembersAsync(
        Guid dungeonSessionId,
        string serverId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            now = connectionClock?.GetUtcNow() ?? now;
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            EnsureServerMatches(session, serverId);
            if (session.State is DungeonSessionState.Loading or DungeonSessionState.InProgress)
            {
                DungeonSessionMember[] expired = session.Members
                    .Where(member => DungeonConnectionPolicy.HasReconnectGraceExpired(member.DisconnectedAt, now))
                    .ToArray();
                if (expired.Length > 0)
                {
                    HashSet<Guid> expiredIds = expired.Select(member => member.CharacterId).ToHashSet();
                    foreach (DungeonSessionMember member in expired)
                    {
                        DateTimeOffset disconnectedAt = member.DisconnectedAt!.Value;
                        _dungeonDisconnectionExpulsions.Add((dungeonSessionId, member.CharacterId), new(
                            dungeonSessionId, member.CharacterId, member.SteamId, disconnectedAt,
                            disconnectedAt + DungeonConnectionPolicy.UnexpectedDisconnectGrace, now));
                        if (_characterLeases.TryGetValue(member.CharacterId, out CharacterLeaseRecord? lease)
                            && lease.DungeonSessionId == dungeonSessionId)
                            _characterLeases.Remove(member.CharacterId);
                    }
                    foreach ((string hash, JoinTicketRecord ticket) in _joinTickets)
                        if (ticket.DungeonSessionId == dungeonSessionId && expiredIds.Contains(ticket.CharacterId))
                            _joinTickets.TryRemove(hash, out _);
                    DungeonSessionMember[] remaining = session.Members
                        .Where(member => !expiredIds.Contains(member.CharacterId)).ToArray();
                    _dungeonSessions[dungeonSessionId] = session with
                    {
                        Members = remaining,
                        State = remaining.Length == 0 ? DungeonSessionState.Closed : session.State,
                        UpdatedAt = now,
                        ExpiresAt = remaining.Length == 0 ? now : session.ExpiresAt
                    };
                }
            }
            return Task.FromResult<IReadOnlyList<DungeonDisconnectionExpulsion>>(
                _dungeonDisconnectionExpulsions.Values
                    .Where(receipt => receipt.DungeonSessionId == dungeonSessionId)
                    .OrderBy(receipt => receipt.CharacterId).ToArray());
        }
    }

    public Task<bool> ConfirmDungeonMemberAdmissionAsync(
        Guid dungeonSessionId,
        string serverId,
        string steamId,
        Guid characterId,
        long connectionGeneration,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            EnsureAccountAllowed(steamId, connectionClock?.GetUtcNow() ?? now);
            now = connectionClock?.GetUtcNow() ?? now;
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            EnsureServerMatches(session, serverId);
            if (session.State is not (DungeonSessionState.Loading or DungeonSessionState.InProgress))
                throw new DungeonSessionStateConflictException(dungeonSessionId, session.State);
            DungeonSessionMember? member = session.Members.SingleOrDefault(value =>
                value.CharacterId == characterId && value.SteamId == steamId);
            if (member is null || member.LeaseExpiresAt <= now || connectionGeneration <= 0
                || member.ConnectionGeneration != connectionGeneration)
                throw new DungeonSessionMembershipException(dungeonSessionId);

            // Replaying a confirmation for a connection that subsequently dropped
            // must not erase its new disconnect marker.
            if (member.ConfirmedConnectionGeneration == connectionGeneration)
                return Task.FromResult(member.DisconnectedAt is null);
            // A report for this very attempt is terminal even if it raced ahead
            // of the HTTP confirmation commit. A marker from an older attempt
            // may still be cleared by a successful new admission before grace.
            if (member.DisconnectedAt is not null
                && member.DisconnectedGeneration == connectionGeneration)
                return Task.FromResult(false);
            if (DungeonConnectionPolicy.HasReconnectGraceExpired(member.DisconnectedAt, now))
                return Task.FromResult(false);

            _dungeonSessions[dungeonSessionId] = session with
            {
                Members = session.Members.Select(value => value.CharacterId == characterId
                    ? value with
                    {
                        DisconnectedAt = null,
                        ConfirmedConnectionGeneration = connectionGeneration,
                        DisconnectedGeneration = 0
                    } : value).ToArray()
            };
            return Task.FromResult(true);
        }
    }

    public Task<DungeonSession> AssignDungeonServerAsync(
        Guid dungeonSessionId,
        string serverId,
        string serverAddress,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken,
        bool requireDeparture = false,
        int minimumPartySize = 1)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            if (session.State == DungeonSessionState.Loading)
            {
                EnsureServerMatches(session, serverId);
                if (!string.Equals(
                    session.ServerAddress,
                    serverAddress,
                    StringComparison.Ordinal))
                {
                    throw new DungeonSessionServerMismatchException(
                        dungeonSessionId);
                }
                session = ExtendDungeonLease(session, now, leaseExpiresAt);
                return Task.FromResult(session);
            }

            if (session.State != DungeonSessionState.Waiting)
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    session.State);
            }

            if (requireDeparture && !DungeonDeparturePolicy.CanAllocate(session, minimumPartySize, now))
                throw new DungeonDepartureConflictException("dungeon_departure_not_requested");
            session = ExtendDungeonLease(
                session with
                {
                    State = DungeonSessionState.Loading,
                    ServerId = serverId,
                    ServerAddress = serverAddress
                },
                now,
                leaseExpiresAt);
            return Task.FromResult(session);
        }
    }

    public Task<DungeonSession?> ClaimNextDungeonSessionAsync(
        string serverId,
        string serverAddress,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken,
        IReadOnlyList<DungeonContentSelection>? allowedSelections = null,
        bool requireDeparture = false,
        int minimumPartySize = 1)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession? session = _dungeonSessions.Values
                .Where(candidate =>
                    candidate.State == DungeonSessionState.Waiting
                    && candidate.Members.Count > 0
                    && (!requireDeparture || DungeonDeparturePolicy.CanAllocate(candidate, minimumPartySize, now))
                    && (allowedSelections is null || allowedSelections.Contains(
                        new DungeonContentSelection(candidate.DungeonId, candidate.Difficulty))))
                .OrderBy(candidate => candidate.CreatedAt)
                .ThenBy(candidate => candidate.DungeonSessionId)
                .FirstOrDefault();
            if (session is null)
            {
                return Task.FromResult<DungeonSession?>(null);
            }

            session = ExtendDungeonLease(
                session with
                {
                    State = DungeonSessionState.Loading,
                    ServerId = serverId,
                    ServerAddress = serverAddress
                },
                now,
                leaseExpiresAt);
            return Task.FromResult<DungeonSession?>(session);
        }
    }

    public Task<DungeonSession> StartDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            EnsureServerMatches(session, serverId);
            if (session.State is not (
                DungeonSessionState.Loading
                or DungeonSessionState.InProgress))
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    session.State);
            }

            session = ExtendDungeonLease(
                session with { State = DungeonSessionState.InProgress },
                now,
                leaseExpiresAt);
            return Task.FromResult(session);
        }
    }

    public Task<DungeonSession> HeartbeatDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            EnsureServerMatches(session, serverId);
            if (session.State is not (
                DungeonSessionState.Loading
                or DungeonSessionState.InProgress))
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    session.State);
            }

            session = ExtendDungeonLease(session, now, leaseExpiresAt);
            return Task.FromResult(session);
        }
    }

    public Task<DungeonSession> FinishDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DungeonSessionState outcome,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            EnsureServerMatches(session, serverId);
            if (outcome is not (
                    DungeonSessionState.Cleared
                    or DungeonSessionState.Failed))
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    session.State);
            }

            if (session.State == outcome)
            {
                return Task.FromResult(session);
            }

            if (session.State is not (
                    DungeonSessionState.Loading
                    or DungeonSessionState.InProgress))
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    session.State);
            }

            foreach (DungeonSessionMember member in session.Members)
            {
                _characterLeases.Remove(member.CharacterId);
            }

            session = session with
            {
                State = outcome,
                UpdatedAt = now,
                ExpiresAt = now
            };
            _dungeonSessions[dungeonSessionId] = session;
            return Task.FromResult(session);
        }
    }

    public Task<DungeonRewardSettlement> EnqueueDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string serverId,
        string rewardVersion,
        string commandFingerprint,
        IReadOnlyList<CurrencyChange> changes,
        IReadOnlyList<DungeonItemReward> itemRewards,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            EnsureServerMatches(session, serverId);

            if (_dungeonRewardSettlements.TryGetValue(
                dungeonSessionId,
                out DungeonRewardSettlement? existing))
            {
                if (!string.Equals(
                        existing.RewardVersion,
                        rewardVersion,
                        StringComparison.Ordinal)
                    || !string.Equals(
                        existing.CommandFingerprint,
                        commandFingerprint,
                        StringComparison.Ordinal))
                {
                    throw new DungeonRewardSettlementConflictException(
                        dungeonSessionId);
                }

                return Task.FromResult(existing);
            }

            if (session.State != DungeonSessionState.InProgress)
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    session.State);
            }

            // The host injects its authoritative clock. Read it under the same
            // gate as admission/disconnection, after any wait. Direct repository
            // callers without a clock retain the explicit-time test contract.
            now = connectionClock?.GetUtcNow() ?? now;
            Guid[] recipients = session.Members
                .Where(member => !DungeonConnectionPolicy.HasReconnectGraceExpired(member.DisconnectedAt, now))
                .Select(member => member.CharacterId).ToArray();
            DungeonRewardSettlement settlement = new(
                dungeonSessionId,
                serverId,
                rewardVersion,
                commandFingerprint,
                DungeonRewardSettlementState.Pending,
                recipients,
                changes.ToArray(),
                DungeonPersonalLootSelection.Capture(itemRewards),
                0,
                now,
                null,
                null,
                null,
                now,
                now,
                null);
            _dungeonRewardSettlements[dungeonSessionId] = settlement;
            _dungeonSessions[dungeonSessionId] = session with
            {
                State = DungeonSessionState.SettlementPending,
                UpdatedAt = now,
                ExpiresAt = now
            };
            return Task.FromResult(settlement);
        }
    }

    public Task<DungeonRewardSettlement?> ClaimNextDungeonRewardSettlementAsync(
        string workerId,
        DateTimeOffset now,
        DateTimeOffset processingExpiresAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            DungeonRewardSettlement? settlement = _dungeonRewardSettlements.Values
                .Where(candidate =>
                    (candidate.State == DungeonRewardSettlementState.Pending
                        && candidate.NextAttemptAt <= now)
                    || (candidate.State == DungeonRewardSettlementState.Processing
                        && candidate.ProcessingExpiresAt <= now))
                .OrderBy(candidate => candidate.NextAttemptAt)
                .ThenBy(candidate => candidate.CreatedAt)
                .FirstOrDefault();
            if (settlement is null)
            {
                return Task.FromResult<DungeonRewardSettlement?>(null);
            }

            settlement = settlement with
            {
                State = DungeonRewardSettlementState.Processing,
                AttemptCount = settlement.AttemptCount + 1,
                WorkerId = workerId,
                ProcessingExpiresAt = processingExpiresAt,
                UpdatedAt = now
            };
            _dungeonRewardSettlements[settlement.DungeonSessionId] = settlement;
            return Task.FromResult<DungeonRewardSettlement?>(settlement);
        }
    }

    public Task CompleteDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            DungeonRewardSettlement settlement =
                GetOwnedProcessingSettlement(dungeonSessionId, workerId);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            foreach (Guid characterId in settlement.CharacterIds)
            {
                _characterLeases.Remove(characterId);
            }

            _dungeonRewardSettlements[dungeonSessionId] = settlement with
            {
                State = DungeonRewardSettlementState.Completed,
                WorkerId = null,
                ProcessingExpiresAt = null,
                LastError = null,
                UpdatedAt = now,
                CompletedAt = now
            };
            _dungeonSessions[dungeonSessionId] = session with
            {
                State = DungeonSessionState.Cleared,
                Members = session.Members.Select(member =>
                    member with { LeaseExpiresAt = now }).ToArray(),
                UpdatedAt = now,
                ExpiresAt = now
            };
            return Task.CompletedTask;
        }
    }

    public Task FailDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        string error,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            DungeonRewardSettlement settlement =
                GetOwnedProcessingSettlement(dungeonSessionId, workerId);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            foreach (Guid characterId in settlement.CharacterIds)
            {
                _characterLeases.Remove(characterId);
            }

            _dungeonRewardSettlements[dungeonSessionId] = settlement with
            {
                State = DungeonRewardSettlementState.Failed,
                WorkerId = null,
                ProcessingExpiresAt = null,
                LastError = error,
                UpdatedAt = now,
                CompletedAt = now
            };
            _dungeonSessions[dungeonSessionId] = session with
            {
                State = DungeonSessionState.Failed,
                Members = session.Members.Select(member =>
                    member with { LeaseExpiresAt = now }).ToArray(),
                UpdatedAt = now,
                ExpiresAt = now
            };
            return Task.CompletedTask;
        }
    }

    public Task RequeueDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        string error,
        DateTimeOffset now,
        DateTimeOffset nextAttemptAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            DungeonRewardSettlement settlement =
                GetOwnedProcessingSettlement(dungeonSessionId, workerId);
            _dungeonRewardSettlements[dungeonSessionId] = settlement with
            {
                State = DungeonRewardSettlementState.Pending,
                NextAttemptAt = nextAttemptAt,
                WorkerId = null,
                ProcessingExpiresAt = null,
                LastError = error,
                UpdatedAt = now
            };
            return Task.CompletedTask;
        }
    }

    public Task<bool> IsActiveDungeonSessionMemberAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            bool isActive = _dungeonSessions.TryGetValue(
                    dungeonSessionId,
                    out DungeonSession? session)
                && session.State is (
                    DungeonSessionState.Loading
                    or DungeonSessionState.InProgress)
                && session.ServerId is not null
                && session.Members.Any(member =>
                    member.CharacterId == characterId
                    && member.SteamId == steamId
                    && member.LeaseExpiresAt > now)
                && _characterLeases.TryGetValue(
                    characterId,
                    out CharacterLeaseRecord? lease)
                && lease.DungeonSessionId == dungeonSessionId
                && lease.ExpiresAt > now;
            return Task.FromResult(isActive);
        }
    }

    public Task<bool> IsAuthorizedGameServerSessionMemberAsync(
        Guid dungeonSessionId,
        string serverId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            return Task.FromResult(IsAuthorizedGameServerSessionMember(
                dungeonSessionId, serverId, steamId, characterId, now));
        }
    }

    // Caller holds _dungeonGate, including a legacy inventory replacement.
    private bool IsAuthorizedGameServerSessionMember(Guid dungeonSessionId, string serverId,
        string steamId, Guid characterId, DateTimeOffset now) => _dungeonSessions.TryGetValue(
                    dungeonSessionId,
                    out DungeonSession? session)
                && session.State is (
                    DungeonSessionState.Loading
                    or DungeonSessionState.InProgress)
                && string.Equals(
                    session.ServerId,
                    serverId,
                    StringComparison.Ordinal)
                && session.ExpiresAt > now
                && session.Members.Any(member =>
                    member.CharacterId == characterId
                    && member.SteamId == steamId
                    && member.LeaseExpiresAt > now)
                && _characterLeases.TryGetValue(
                    characterId,
                    out CharacterLeaseRecord? lease)
                && lease.DungeonSessionId == dungeonSessionId
                && lease.ExpiresAt > now;

    public Task<bool> AreAuthorizedSecurityTelemetryMembersAsync(
        Guid dungeonSessionId,
        string serverId,
        IReadOnlyList<SecurityTelemetryMember> members,
        DateTimeOffset now,
        TimeSpan postSessionGrace,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            if (members.Count == 0
                || !_dungeonSessions.TryGetValue(
                    dungeonSessionId,
                    out DungeonSession? session)
                || !string.Equals(
                    session.ServerId,
                    serverId,
                    StringComparison.Ordinal))
            {
                return Task.FromResult(false);
            }

            bool isActive = session.State is (
                    DungeonSessionState.Loading
                    or DungeonSessionState.InProgress)
                && session.ExpiresAt > now;
            bool isWithinPostSessionGrace = session.State is (
                    DungeonSessionState.Cleared
                    or DungeonSessionState.Failed)
                && session.UpdatedAt >= now - postSessionGrace;
            if (!isActive && !isWithinPostSessionGrace)
            {
                return Task.FromResult(false);
            }

            bool allAuthorized = members
                .Distinct()
                .All(requested =>
                {
                    DungeonSessionMember? member = session.Members
                        .FirstOrDefault(value =>
                            value.CharacterId == requested.CharacterId
                            && value.SteamId == requested.SteamId);
                    if (member is null)
                    {
                        return false;
                    }
                    if (isWithinPostSessionGrace)
                    {
                        return true;
                    }
                    return member.LeaseExpiresAt > now
                        && _characterLeases.TryGetValue(
                            requested.CharacterId,
                            out CharacterLeaseRecord? lease)
                        && lease.DungeonSessionId == dungeonSessionId
                        && lease.ExpiresAt > now;
                });
            return Task.FromResult(allAuthorized);
        }
    }

    public Task<JoinTicketIssueResult> StoreJoinTicketAsync(
        string tokenHash,
        string steamId,
        Guid characterId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            DateTimeOffset now = connectionClock?.GetUtcNow() ?? DateTimeOffset.UtcNow;
            EnsureAccountAllowed(steamId, now);
            if (_dungeonDisconnectionExpulsions.TryGetValue((dungeonSessionId, characterId), out var expulsion)
                && expulsion.SteamId == steamId)
                return Task.FromResult(JoinTicketIssueResult.ReconnectGraceExpired);

            if (!_dungeonSessions.TryGetValue(dungeonSessionId, out DungeonSession? session)
                || session.State is not (DungeonSessionState.Loading or DungeonSessionState.InProgress)
                || session.ServerId is null || session.ExpiresAt <= now || expiresAt <= now)
                return Task.FromResult(JoinTicketIssueResult.NotActiveMember);

            DungeonSessionMember? member = session.Members.FirstOrDefault(member =>
                member.CharacterId == characterId && member.SteamId == steamId);
            if (member is null) return Task.FromResult(JoinTicketIssueResult.NotActiveMember);
            if (DungeonConnectionPolicy.HasReconnectGraceExpired(member.DisconnectedAt, now))
                return Task.FromResult(JoinTicketIssueResult.ReconnectGraceExpired);
            if (member.LeaseExpiresAt <= now
                || !_characterLeases.TryGetValue(characterId, out CharacterLeaseRecord? lease)
                || lease.DungeonSessionId != dungeonSessionId || lease.ExpiresAt <= now)
                return Task.FromResult(JoinTicketIssueResult.NotActiveMember);

            foreach ((string expiredHash, JoinTicketRecord expiredTicket) in _joinTickets)
            {
                if (expiredTicket.ExpiresAt <= now)
                {
                    _joinTickets.TryRemove(expiredHash, out _);
                }
            }

            _joinTickets[tokenHash] = new JoinTicketRecord(
                steamId,
                characterId,
                dungeonSessionId,
                expiresAt);
            return Task.FromResult(JoinTicketIssueResult.Issued);
        }
    }

    public Task<ConsumedJoinTicket?> ConsumeJoinTicketAsync(
        string tokenHash,
        string serverId,
        Guid dungeonSessionId,
        DateTimeOffset now,
        CancellationToken cancellationToken,
        Guid? consumptionRequestId = null)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (consumptionRequestId == Guid.Empty) return Task.FromResult<ConsumedJoinTicket?>(null);
        lock (_dungeonGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            // Admission waits on this same gate as disconnection and settlement.
            // Re-read the authoritative clock after any wait, not when the HTTP
            // request first arrived.
            now = connectionClock?.GetUtcNow() ?? now;
            string? account = _joinTickets.GetValueOrDefault(tokenHash)?.SteamId
                ?? _joinTicketConsumptions.Values.FirstOrDefault(r => r.TokenHash == tokenHash)?.Admission.SteamId;
            if (account is null || !AccountAllowed(account, now)) return Task.FromResult<ConsumedJoinTicket?>(null);
            CleanupExpiredDungeonSessions(now);
            // Keep evidence even after the admission-replay window. Erasing it
            // would turn a late lookup into an unsafe "never committed" guess.
            if (_joinTicketConsumptionCancellations.Values.Any(value => value.TicketHash == tokenHash)
                || (consumptionRequestId is Guid fencedId
                    && _joinTicketConsumptionCancellations.ContainsKey((dungeonSessionId, serverId, fencedId))))
                return Task.FromResult<ConsumedJoinTicket?>(null);
            if (consumptionRequestId is Guid requestId
                && _joinTicketConsumptions.TryGetValue((dungeonSessionId, serverId, requestId), out var receipt))
            {
                // This is result recovery, not another admission or confirmation.
                // Never revive a superseded generation or clear a disconnect marker.
                bool current = receipt.TokenHash == tokenHash && receipt.RecoveryExpiresAt > now
                    && _dungeonSessions.TryGetValue(dungeonSessionId, out var currentSession)
                    && currentSession.ServerId == serverId && currentSession.ExpiresAt > now
                    && currentSession.State is DungeonSessionState.Loading or DungeonSessionState.InProgress
                    && currentSession.Members.Any(member => member.CharacterId == receipt.Admission.CharacterId
                        && member.SteamId == receipt.Admission.SteamId
                        && member.ConnectionGeneration == receipt.Admission.ConnectionGeneration
                        && member.LeaseExpiresAt > now
                        && !DungeonConnectionPolicy.HasReconnectGraceExpired(member.DisconnectedAt, now))
                    && _characterLeases.TryGetValue(receipt.Admission.CharacterId, out var currentLease)
                    && currentLease.DungeonSessionId == dungeonSessionId && currentLease.ExpiresAt > now;
                return Task.FromResult(current ? receipt.Admission : null);
            }
            if (!_joinTickets.TryGetValue(tokenHash, out JoinTicketRecord? ticket)
                || ticket.ExpiresAt <= now
                || ticket.DungeonSessionId != dungeonSessionId
                || !_dungeonSessions.TryGetValue(
                    ticket.DungeonSessionId,
                    out DungeonSession? session)
                || session.ServerId != serverId
                || session.State is not (
                    DungeonSessionState.Loading
                    or DungeonSessionState.InProgress))
            {
                return Task.FromResult<ConsumedJoinTicket?>(null);
            }

            DungeonSessionMember? admitted = session.Members.FirstOrDefault(member =>
                    member.CharacterId == ticket.CharacterId
                    && member.SteamId == ticket.SteamId
                    && member.LeaseExpiresAt > now
                    && !DungeonConnectionPolicy.HasReconnectGraceExpired(member.DisconnectedAt, now));
            if (admitted is null || admitted.ConnectionGeneration == long.MaxValue
                || !_characterLeases.TryGetValue(ticket.CharacterId, out var lease)
                || lease.DungeonSessionId != dungeonSessionId || lease.ExpiresAt <= now
                || _joinTicketConsumptions.Values.Any(stored => stored.TokenHash == tokenHash))
            {
                return Task.FromResult<ConsumedJoinTicket?>(null);
            }

            _joinTickets.TryRemove(tokenHash, out _);
            long connectionGeneration = admitted.ConnectionGeneration + 1;
            _dungeonSessions[dungeonSessionId] = session with
            {
                Members = session.Members.Select(member => member.CharacterId == ticket.CharacterId
                    ? member with { ConnectionGeneration = connectionGeneration } : member).ToArray()
            };
            ConsumedJoinTicket result = new(
                ticket.DungeonSessionId,
                ticket.CharacterId,
                ticket.SteamId,
                connectionGeneration);
            if (consumptionRequestId is Guid id)
                _joinTicketConsumptions.Add((dungeonSessionId, serverId, id),
                    new(tokenHash, result, now, now + JoinTicketConsumptionRecovery.Retention));
            return Task.FromResult<ConsumedJoinTicket?>(result);
        }
    }

    public Task<JoinTicketConsumptionEvidence?> ReadJoinTicketConsumptionEvidenceAsync(
        string tokenHash, string serverId, Guid dungeonSessionId, Guid consumptionRequestId,
        string expectedSteamId, DateTimeOffset now, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            now = connectionClock?.GetUtcNow() ?? now;
            // Do not run expiry cleanup or mutate membership in this read path.
            if (consumptionRequestId == Guid.Empty
                || !_dungeonSessions.TryGetValue(dungeonSessionId, out var session)
                || session.ServerId != serverId || session.ExpiresAt <= now
                || session.State is not (DungeonSessionState.Loading or DungeonSessionState.InProgress)
                || !_joinTicketConsumptions.TryGetValue((dungeonSessionId, serverId, consumptionRequestId), out var receipt)
                || receipt.TokenHash != tokenHash || receipt.Admission.SteamId != expectedSteamId)
                return Task.FromResult<JoinTicketConsumptionEvidence?>(null);
            return Task.FromResult<JoinTicketConsumptionEvidence?>(new(dungeonSessionId, serverId,
                consumptionRequestId, receipt.Admission.CharacterId, receipt.Admission.SteamId,
                receipt.Admission.ConnectionGeneration, receipt.ConsumedAt));
        }
    }

    private void CleanupExpiredDungeonSessions(DateTimeOffset now)
    {
        foreach ((Guid sessionId, DungeonSession session) in _dungeonSessions.ToArray())
        {
            if (session.ExpiresAt <= now
                && session.State is (
                    DungeonSessionState.Waiting
                    or DungeonSessionState.Loading
                    or DungeonSessionState.InProgress))
            {
                _dungeonSessions[sessionId] = session with
                {
                    State = DungeonSessionState.Closed,
                    UpdatedAt = now,
                    ExpiresAt = now
                };
            }
        }

        foreach ((Guid characterId, CharacterLeaseRecord lease) in
            _characterLeases.ToArray())
        {
            bool sessionEnded = !_dungeonSessions.TryGetValue(
                    lease.DungeonSessionId,
                    out DungeonSession? session)
                || session.State is (
                    DungeonSessionState.Cleared
                    or DungeonSessionState.Failed
                    or DungeonSessionState.Closed);
            if (lease.ExpiresAt <= now || sessionEnded)
            {
                _characterLeases.Remove(characterId);
            }
        }
    }

    private void AcquireCharacterLease(
        Guid characterId,
        Guid requestedSessionId,
        DateTimeOffset now,
        DateTimeOffset expiresAt)
    {
        if (_characterLeases.TryGetValue(
                characterId,
                out CharacterLeaseRecord? lease)
            && lease.ExpiresAt > now
            && lease.DungeonSessionId != requestedSessionId)
        {
            throw new CharacterSessionConflictException(characterId);
        }

        if (lease is not null && lease.ExpiresAt <= now)
        {
            _characterLeases.Remove(characterId);
        }
    }

    private DungeonSession GetRequiredDungeonSession(Guid dungeonSessionId)
    {
        return _dungeonSessions.TryGetValue(
            dungeonSessionId,
            out DungeonSession? session)
                ? session
                : throw new DungeonSessionNotFoundException(dungeonSessionId);
    }

    private DungeonRewardSettlement GetOwnedProcessingSettlement(
        Guid dungeonSessionId,
        string workerId)
    {
        if (!_dungeonRewardSettlements.TryGetValue(
                dungeonSessionId,
                out DungeonRewardSettlement? settlement)
            || settlement.State != DungeonRewardSettlementState.Processing
            || !string.Equals(
                settlement.WorkerId,
                workerId,
                StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                $"Dungeon reward settlement '{dungeonSessionId}' is not owned by worker '{workerId}'.");
        }

        return settlement;
    }

    private static void EnsureServerMatches(
        DungeonSession session,
        string serverId)
    {
        if (!string.Equals(session.ServerId, serverId, StringComparison.Ordinal))
        {
            throw new DungeonSessionServerMismatchException(
                session.DungeonSessionId);
        }
    }

    private DungeonSession ExtendDungeonLease(
        DungeonSession session,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt)
    {
        DungeonSessionMember[] members = session.Members
            .Select(member => member with { LeaseExpiresAt = leaseExpiresAt })
            .ToArray();
        foreach (DungeonSessionMember member in members)
        {
            _characterLeases[member.CharacterId] = new CharacterLeaseRecord(
                session.DungeonSessionId,
                leaseExpiresAt);
        }

        session = session with
        {
            Members = members,
            UpdatedAt = now,
            ExpiresAt = leaseExpiresAt
        };
        _dungeonSessions[session.DungeonSessionId] = session;
        return session;
    }


    public Task<IReadOnlyList<InventoryItem>> LoadInventoryAsync(
        Guid characterId,
        CancellationToken cancellationToken)
    {
        IReadOnlyList<InventoryItem> inventory = _inventories.TryGetValue(
            characterId, out IReadOnlyList<InventoryItem>? items)
            ? items
            : Array.Empty<InventoryItem>();
        return Task.FromResult(inventory);
    }

    public Task SaveInventoryAsync(
        Guid characterId,
        IReadOnlyList<InventoryItem> inventory,
        DateTimeOffset updatedAt,
        CancellationToken cancellationToken,
        GameServerCredential? requiredServer = null)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            if (requiredServer is not null && (requiredServer.IsSecurityTelemetryOnly
                || !_characters.TryGetValue(characterId, out GameCharacter? character)
                || !IsAuthorizedGameServerSessionMember(requiredServer.DungeonSessionId,
                    requiredServer.ServerId, character.SteamId, characterId, updatedAt)))
                throw new DungeonSessionMembershipException(requiredServer.DungeonSessionId);
            _inventories[characterId] = inventory.OrderBy(item => item.SlotIndex).ToArray();
        }
        return Task.CompletedTask;
    }
}
