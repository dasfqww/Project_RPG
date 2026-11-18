-- Keep the source date even after the character or original dungeon is retired.
CREATE TABLE IF NOT EXISTS roster_achievement_facts (
    roster_id UUID NOT NULL REFERENCES rosters(roster_id) ON DELETE CASCADE,
    kind TEXT NOT NULL, subject TEXT NOT NULL, difficulty TEXT NOT NULL,
    source_id UUID NOT NULL, earned_at TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (roster_id, kind, subject, difficulty, source_id)
);
CREATE INDEX IF NOT EXISTS ix_roster_achievement_threshold
    ON roster_achievement_facts(roster_id, kind, subject, difficulty, earned_at, source_id);

CREATE OR REPLACE VIEW achievement_dungeon_evidence AS
SELECT DISTINCT c.roster_id, s.dungeon_id AS subject, s.difficulty, r.dungeon_session_id AS source_id, r.created_at AS earned_at
FROM dungeon_reward_settlements r
JOIN dungeon_sessions s USING (dungeon_session_id)
CROSS JOIN LATERAL jsonb_array_elements_text(r.character_ids) recipient(id)
JOIN characters c ON c.character_id = recipient.id::uuid;

CREATE OR REPLACE VIEW achievement_chapter_evidence AS
SELECT c.roster_id, cmd.character_id, cmd.request_id, chapter.id AS subject, cmd.committed_at AS earned_at
FROM character_narrative_commands cmd
JOIN characters c USING (character_id)
CROSS JOIN LATERAL jsonb_array_elements_text(cmd.result->'completedChapters') chapter(id)
WHERE NOT EXISTS (SELECT FROM jsonb_array_elements(cmd.result->'storySkips') skip
                  WHERE skip->>'chapterId' = chapter.id);

CREATE OR REPLACE FUNCTION record_dungeon_achievement_facts() RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN
    INSERT INTO roster_achievement_facts
    SELECT roster_id, 'DungeonClear', subject, difficulty, source_id, earned_at
    FROM achievement_dungeon_evidence WHERE source_id = NEW.dungeon_session_id
    ORDER BY roster_id ON CONFLICT DO NOTHING;
    RETURN NEW;
END $$;

CREATE OR REPLACE FUNCTION record_chapter_achievement_facts() RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN
    INSERT INTO roster_achievement_facts AS f
    SELECT roster_id, 'ChapterCompleted', subject, '', roster_id, earned_at
    FROM achievement_chapter_evidence WHERE character_id = NEW.character_id AND request_id = NEW.request_id
    ORDER BY subject
    ON CONFLICT (roster_id, kind, subject, difficulty, source_id)
    DO UPDATE SET earned_at = EXCLUDED.earned_at WHERE EXCLUDED.earned_at < f.earned_at;
    RETURN NEW;
END $$;

-- Development initialization replays the catalog. Install and backfill atomically once.
DO $$
BEGIN
    IF NOT EXISTS (SELECT FROM pg_trigger WHERE tgrelid = 'dungeon_reward_settlements'::regclass
                   AND tgname = 'achievement_dungeon_insert') THEN
        CREATE TRIGGER achievement_dungeon_insert AFTER INSERT ON dungeon_reward_settlements
            FOR EACH ROW EXECUTE FUNCTION record_dungeon_achievement_facts();
        INSERT INTO roster_achievement_facts
        SELECT roster_id, 'DungeonClear', subject, difficulty, source_id, earned_at FROM achievement_dungeon_evidence
        ON CONFLICT DO NOTHING;
    END IF;
    IF NOT EXISTS (SELECT FROM pg_trigger WHERE tgrelid = 'character_narrative_commands'::regclass
                   AND tgname = 'achievement_chapter_insert') THEN
        CREATE TRIGGER achievement_chapter_insert AFTER INSERT ON character_narrative_commands
            FOR EACH ROW EXECUTE FUNCTION record_chapter_achievement_facts();
        INSERT INTO roster_achievement_facts AS f
        SELECT roster_id, 'ChapterCompleted', subject, '', roster_id, MIN(earned_at)
        FROM achievement_chapter_evidence GROUP BY roster_id, subject
        ON CONFLICT (roster_id, kind, subject, difficulty, source_id)
        DO UPDATE SET earned_at = LEAST(f.earned_at, EXCLUDED.earned_at);
    END IF;
END $$;
