ALTER TABLE ops_actions DROP CONSTRAINT IF EXISTS ops_actions_target_shape;
ALTER TABLE ops_actions ADD CONSTRAINT ops_actions_target_shape CHECK (
    (target_scope='Character' AND target_id IS NOT NULL AND target_account_id IS NULL AND case_id IS NOT NULL AND template_version IS NOT NULL AND template_version=1)
    OR (target_scope='Account' AND target_id IS NULL AND target_account_id IS NOT NULL AND case_id IS NULL AND template_version IS NULL)
    OR (target_scope='Service' AND target_id IS NULL AND target_account_id IS NULL AND case_id IS NULL AND template_version IS NULL));
CREATE TABLE IF NOT EXISTS ops_service_control (
    id SMALLINT PRIMARY KEY CHECK (id=1),
    revision BIGINT NOT NULL CHECK (revision>=0),
    mode VARCHAR(8) NOT NULL CHECK (mode IN ('Open','Drain')),
    notice_text VARCHAR(1000) NOT NULL,
    notice_starts_at TIMESTAMPTZ,
    notice_ends_at TIMESTAMPTZ,
    updated_at TIMESTAMPTZ,
    action_request_id UUID REFERENCES ops_actions(request_id),
    CHECK (notice_starts_at IS NULL OR notice_ends_at IS NULL OR notice_starts_at<notice_ends_at),
    CHECK ((revision=0 AND updated_at IS NULL AND action_request_id IS NULL AND mode='Open' AND notice_text='')
        OR (revision>0 AND updated_at IS NOT NULL AND action_request_id IS NOT NULL))
);
INSERT INTO ops_service_control(id,revision,mode,notice_text) VALUES(1,0,'Open','') ON CONFLICT(id) DO NOTHING;
