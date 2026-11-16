-- Successful and terminally rejected shop requests share one immutable request identity.
-- The payload carries the exact original command; request replay never re-evaluates a completed outcome.
CREATE TABLE IF NOT EXISTS shop_receipts (
    request_id UUID PRIMARY KEY,
    account_id VARCHAR(64) NOT NULL,
    character_id UUID NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
    fingerprint VARCHAR(64) NOT NULL CHECK (fingerprint ~ '^[0-9a-f]{64}$'),
    command JSONB NOT NULL CHECK (jsonb_typeof(command) = 'object'),
    result JSONB NOT NULL CHECK (jsonb_typeof(result) = 'object'),
    committed_at TIMESTAMPTZ NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_shop_receipts_owner ON shop_receipts(account_id, character_id, committed_at);
