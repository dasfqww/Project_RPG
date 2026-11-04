-- Immutable operations success receipts. Effects and case key are committed in the same item transaction.
CREATE TABLE IF NOT EXISTS ops_actions (
    request_id UUID PRIMARY KEY,
    operation VARCHAR(64) NOT NULL,
    input_hash BYTEA NOT NULL CHECK (octet_length(input_hash) = 32),
    canonical_input JSONB NOT NULL,
    operator_id TEXT NOT NULL,
    case_id UUID NOT NULL,
    target_scope VARCHAR(32) NOT NULL CHECK (target_scope = 'Character'),
    target_id UUID NOT NULL REFERENCES characters(character_id),
    template_version INTEGER NOT NULL CHECK (template_version = 1),
    reason VARCHAR(512) NOT NULL CHECK (length(reason) >= 10),
    committed_at TIMESTAMPTZ NOT NULL,
    result_json JSONB NOT NULL,
    origin_http_request_id UUID NOT NULL,
    origin_trace_id VARCHAR(32) NULL
);
CREATE INDEX IF NOT EXISTS ix_ops_actions_target_time ON ops_actions(target_scope, target_id, committed_at DESC, request_id DESC);
CREATE TABLE IF NOT EXISTS ops_compensation_cases (
    case_id UUID NOT NULL,
    target_scope VARCHAR(32) NOT NULL CHECK (target_scope = 'Character'),
    target_id UUID NOT NULL REFERENCES characters(character_id),
    action_request_id UUID NOT NULL UNIQUE REFERENCES ops_actions(request_id),
    PRIMARY KEY (case_id, target_scope, target_id)
);
