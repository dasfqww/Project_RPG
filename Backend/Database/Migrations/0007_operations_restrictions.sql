-- Extend immutable action receipts without rewriting applied compensation history.
ALTER TABLE ops_actions DROP CONSTRAINT IF EXISTS ops_actions_target_scope_check;
ALTER TABLE ops_actions ALTER COLUMN case_id DROP NOT NULL;
ALTER TABLE ops_actions ALTER COLUMN target_id DROP NOT NULL;
ALTER TABLE ops_actions ALTER COLUMN template_version DROP NOT NULL;
ALTER TABLE ops_actions ADD COLUMN IF NOT EXISTS target_account_id VARCHAR(20) REFERENCES accounts(steam_id);
DO $$ BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname='ops_actions_target_shape') THEN
        ALTER TABLE ops_actions ADD CONSTRAINT ops_actions_target_shape CHECK (
            (target_scope='Character' AND target_id IS NOT NULL AND target_account_id IS NULL AND case_id IS NOT NULL AND template_version IS NOT NULL AND template_version=1)
            OR (target_scope='Account' AND target_id IS NULL AND target_account_id IS NOT NULL AND case_id IS NULL AND template_version IS NULL));
    END IF;
END $$;
CREATE INDEX IF NOT EXISTS ix_ops_actions_account_time ON ops_actions(target_account_id,committed_at DESC,request_id DESC)
    WHERE target_scope='Account';
CREATE TABLE IF NOT EXISTS ops_account_restrictions (
    account_id VARCHAR(20) PRIMARY KEY REFERENCES accounts(steam_id),
    revision BIGINT NOT NULL CHECK (revision>=1),
    state VARCHAR(16) NOT NULL CHECK (state IN ('Active','Released')),
    starts_at TIMESTAMPTZ NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL CHECK (expires_at>starts_at),
    reason VARCHAR(512) NOT NULL CHECK (length(reason)>=10),
    action_request_id UUID NOT NULL REFERENCES ops_actions(request_id)
);
