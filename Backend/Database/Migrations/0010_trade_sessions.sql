CREATE TABLE IF NOT EXISTS trade_sessions (
    trade_id UUID PRIMARY KEY,
    initiator_character UUID NOT NULL REFERENCES characters(character_id),
    invitee_character UUID NOT NULL REFERENCES characters(character_id),
    initiator_account VARCHAR(20) NOT NULL REFERENCES accounts(steam_id),
    invitee_account VARCHAR(20) NOT NULL REFERENCES accounts(steam_id),
    expires_at TIMESTAMPTZ NOT NULL,
    closed BOOLEAN NOT NULL,
    revision BIGINT NOT NULL CHECK (revision > 0),
    payload JSONB NOT NULL CHECK (jsonb_typeof(payload)='object'),
    CHECK (initiator_character <> invitee_character AND initiator_account <> invitee_account)
);
CREATE INDEX IF NOT EXISTS ix_trade_sessions_first ON trade_sessions(initiator_character, expires_at) WHERE NOT closed;
CREATE INDEX IF NOT EXISTS ix_trade_sessions_second ON trade_sessions(invitee_character, expires_at) WHERE NOT closed;
CREATE TABLE IF NOT EXISTS trade_session_receipts (
    request_id UUID PRIMARY KEY,
    trade_id UUID NOT NULL REFERENCES trade_sessions(trade_id),
    actor_account VARCHAR(20) NOT NULL,
    actor_character UUID NOT NULL,
    fingerprint VARCHAR(64) NOT NULL CHECK (fingerprint ~ '^[0-9a-f]{64}$'),
    result JSONB NOT NULL CHECK (jsonb_typeof(result)='object'),
    committed_at TIMESTAMPTZ NOT NULL
);
