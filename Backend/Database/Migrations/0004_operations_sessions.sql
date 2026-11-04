CREATE TABLE IF NOT EXISTS ops_operator_sessions (
    key_hash BYTEA PRIMARY KEY CHECK (octet_length(key_hash) = 32),
    operator_id TEXT NOT NULL,
    protected_ticket BYTEA NOT NULL,
    auth_epoch TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL,
    last_seen_at TIMESTAMPTZ NOT NULL,
    absolute_expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ,
    CHECK (absolute_expires_at > created_at)
);
CREATE INDEX IF NOT EXISTS ix_ops_sessions_expiry ON ops_operator_sessions(absolute_expires_at);
CREATE INDEX IF NOT EXISTS ix_ops_characters_lookup ON characters(steam_id, created_at, character_id);
CREATE INDEX IF NOT EXISTS ix_ops_items_page ON item_records(owner_type, owner_id, container_type, item_id);
