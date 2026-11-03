-- Story state and operation receipts commit together. No combat progression/grant writes.
CREATE TABLE IF NOT EXISTS character_narrative (
    character_id UUID PRIMARY KEY REFERENCES characters(character_id) ON DELETE CASCADE,
    state JSONB NOT NULL CHECK (jsonb_typeof(state) = 'object'),
    updated_at TIMESTAMPTZ NOT NULL
);

CREATE TABLE IF NOT EXISTS character_narrative_commands (
    character_id UUID NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
    request_id UUID NOT NULL,
    fingerprint VARCHAR(64) NOT NULL CHECK (fingerprint ~ '^[0-9a-f]{64}$'),
    result JSONB NOT NULL CHECK (jsonb_typeof(result) = 'object'),
    committed_at TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (character_id, request_id)
);
