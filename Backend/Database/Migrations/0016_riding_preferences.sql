-- Replace only the v15 operation/outcome/child shape checks, preserving all identities and existing receipts.
DO $$ DECLARE constraint_name text; BEGIN
    FOR constraint_name IN SELECT conname FROM pg_constraint
      WHERE conrelid='riding_command_receipts'::regclass AND contype='c'
        AND (pg_get_constraintdef(oid) LIKE '%operation%' OR pg_get_constraintdef(oid) LIKE '%outcome%')
    LOOP EXECUTE format('ALTER TABLE riding_command_receipts DROP CONSTRAINT %I', constraint_name); END LOOP;
END $$;
ALTER TABLE riding_command_receipts ADD CONSTRAINT ck_riding_operation_outcome CHECK (
    (operation='Riding.Register' AND outcome IN ('Registered','AlreadyOwned'))
    OR (operation='Riding.Preferences' AND outcome='PreferencesSaved'));
ALTER TABLE riding_command_receipts ADD CONSTRAINT ck_riding_child CHECK ((outcome='Registered')=(child_item_request_id IS NOT NULL));
CREATE TABLE IF NOT EXISTS riding_character_preferences (
    character_id UUID PRIMARY KEY REFERENCES characters(character_id) ON DELETE CASCADE,
    revision BIGINT NOT NULL CHECK (revision>=1),
    selected_mount_id VARCHAR(64),
    shortcut_mode VARCHAR(32) NOT NULL CHECK (shortcut_mode IN ('Selected','Specific','RandomFavorites')),
    shortcut_mount_id VARCHAR(64),
    CHECK ((shortcut_mode='Specific')=(shortcut_mount_id IS NOT NULL))
);
CREATE TABLE IF NOT EXISTS riding_character_favorites (
    character_id UUID NOT NULL REFERENCES riding_character_preferences(character_id) ON DELETE CASCADE,
    mount_id VARCHAR(64) NOT NULL,
    PRIMARY KEY (character_id,mount_id)
);
