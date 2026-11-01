"""PIE-only boss rehearsal: invincible players and gradual server HP reduction.

Run before/while playing bossmap: py "<project>/Scripts/Editor/TestRaidBossProgression.py"
Default: -10 boss HP/second after five seconds; pauses for invulnerability and
summon objectives. Defeat summons normally. No assets/settings are saved.
Stop early: py unreal.raid_boss_progression.stop()
main(auto_clear_summons=True) also simulates summon defeats for phase/UI checks;
neither mode validates player skill damage, balance, or player survivability.
"""


def main(hp_per_second=10.0, auto_clear_summons=False):
    import json
    import math
    import pathlib
    import time
    import unreal

    assert math.isfinite(hp_per_second) and hp_per_second > 0
    previous = getattr(unreal, "raid_boss_progression", None)
    if previous:
        previous.stop()
    output = pathlib.Path(unreal.Paths.project_saved_dir()) / "Editor/RaidAnimationAssembly/invincible-progression.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    tag = unreal.GameplayTag()
    assert tag.import_text('(TagName="Shared.Status.Invincible")')
    immunity_class = unreal.load_class(None, "/Script/Project_RPG.RPGBossPhaseInvulnerabilityEffect")
    health_class = unreal.load_class(None, "/Script/Project_RPG.RPGBossTestDamageEffect")
    assert immunity_class and health_class, "Editor build lacks existing fixture effects"

    health_attribute = unreal.GameplayAttribute()
    assert health_attribute.import_text('(Attribute="/Script/Project_RPG.RPGAttributeSet:CurrentHealth")')

    def health(actor):
        value, found = unreal.AbilitySystemLibrary.get_float_attribute(actor, health_attribute)
        assert found, "Actor does not expose GAS CurrentHealth"
        return value

    def asc(actor):
        return unreal.AbilitySystemLibrary.get_ability_system_component(actor)

    def protected(actor):
        return asc(actor).has_matching_gameplay_tag(tag)

    def set_health(actor, value):
        assert actor.has_authority() and "UEDPIE_" in actor.get_world().get_path_name()
        value = max(0.0, min(health(actor), value))
        modifier = unreal.GameplayModifierInfo()
        assert modifier.import_text(
            '(Attribute=(Attribute="/Script/Project_RPG.RPGAttributeSet:CurrentHealth"),'
            'ModifierOp=Additive,ModifierMagnitude=(MagnitudeCalculationType=ScalableFloat,'
            'ScalableFloatMagnitude=(Value=' + str(value - health(actor)) + ')))')
        # Existing transient test class, restored synchronously before another tick.
        effect = unreal.get_default_object(health_class)
        old_modifiers = list(effect.get_editor_property("modifiers"))
        old_executions = list(effect.get_editor_property("executions"))
        try:
            effect.set_editor_property("executions", [])
            effect.set_editor_property("modifiers", [modifier])
            component = asc(actor)
            component.apply_gameplay_effect_to_self(health_class, 1.0, component.make_effect_context())
        finally:
            effect.set_editor_property("modifiers", old_modifiers)
            effect.set_editor_property("executions", old_executions)
        assert abs(health(actor) - value) < .01, "Test health GE did not apply"

    class Rehearsal:
        def __init__(self):
            self.started = time.monotonic()
            self.next_tick = 0.0
            self.drain_at = float("inf")
            self.wave_at = None
            self.handles = {}
            self.boss = None
            self.seen_pie = False
            self.last_state = None
            self.callback = None
            self.report = {"mode": "PIE server health fixture", "auto_clear_summons": auto_clear_summons, "events": []}

        def publish(self, event, **data):
            self.report["events"].append({"event": event, "seconds": round(time.monotonic() - self.started, 2), **data})
            output.write_text(json.dumps(self.report, ensure_ascii=False, indent=2), encoding="utf-8")
            unreal.log("RAID_BOSS_REHEARSAL " + event)

        def stop(self, reason="stopped"):
            if self.callback is not None:
                unreal.unregister_slate_post_tick_callback(self.callback)
                self.callback = None
            removed = 0
            for component, handle in self.handles.values():
                try:
                    if component.remove_active_gameplay_effect(handle):
                        removed += 1
                except Exception:
                    pass  # PIE actor/ASC may already have been destroyed.
            self.handles.clear()
            self.publish(reason, removed_player_effects=removed)

        def tick(self, delta):
            now = time.monotonic()
            if now < self.next_tick:
                return
            self.next_tick = now + .25
            try:
                if now - self.started > 180:
                    self.stop("rehearsal_timeout")
                    return
                worlds = unreal.EditorLevelLibrary.get_pie_worlds(True)
                if not worlds:
                    if self.seen_pie or now - self.started > 120:
                        self.stop("PIE_ended_or_start_timeout")
                    return
                self.seen_pie = True
                states = []
                for world in worlds:
                    state = unreal.GameplayStatics.get_game_state(world)
                    if isinstance(state, unreal.RPGBossBattleGameState):
                        snapshot = state.get_battle_snapshot()
                        if snapshot.boss:
                            states.append((world, state, snapshot))
                servers = [s for s in states if s[2].boss.has_authority()]
                if not servers:
                    return
                assert len(servers) == 1, "Expected one authoritative boss world"
                world, game_state, snapshot = servers[0]
                boss = snapshot.boss
                if snapshot.battle_state != unreal.BossBattleState.IN_PROGRESS:
                    if self.boss:
                        self.stop("battle_ended_" + str(snapshot.battle_state))
                    return
                for player in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RPGPlayer):
                    key = player.get_path_name()
                    if key not in self.handles:
                        assert health(player) > 0, "Start with living players (retry first)"
                        component = asc(player)
                        spec = component.make_outgoing_spec(immunity_class, 1.0, component.make_effect_context())
                        spec = unreal.AbilitySystemLibrary.add_granted_tag(spec, tag)
                        handle = component.apply_gameplay_effect_spec_to_self(spec)
                        self.handles[key] = (component, handle)
                        assert protected(player), "Invincibility tag was not granted"
                        self.publish("player_protected", player=key, health=health(player))
                if not self.handles:
                    return
                if self.boss is None:
                    self.boss = boss
                    self.drain_at = now + 5
                    self.publish("started", boss=boss.get_path_name(), health=health(boss))
                assert boss == self.boss, "Boss changed; reinstall rehearsal after retry"
                rows = []
                for w, _, snap in states:
                    objective = snap.boss.boss_summon_objective_component.get_objective_state()
                    rows.append({"world": w.get_path_name(), "authority": snap.boss.has_authority(),
                                 "boss_hp": health(snap.boss), "phase": snap.boss.boss_ai_component.get_boss_state().phase_index,
                                 "objective": [objective.get_editor_property("bActive"), objective.defeated_count, objective.required_count],
                                 "players": [{"hp": health(p), "invincible": protected(p)} for p in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.RPGPlayer)],
                                 "labels": [str(widget.get_summon_objective_status_text()) for widget in unreal.WidgetLibrary.get_all_widgets_of_class(w, unreal.RPGBossBattleInfoWidget, False) if widget.is_in_viewport()]})
                signature = json.dumps(rows, sort_keys=True)
                if signature != self.last_state:
                    self.last_state = signature
                    self.publish("state", worlds=rows)
                objective = boss.boss_summon_objective_component.get_objective_state()
                if objective.get_editor_property("bActive"):
                    self.drain_at = now + 1
                    if self.wave_at is None:
                        self.wave_at = now
                        self.publish("drain_paused_for_summons")
                    if auto_clear_summons and now - self.wave_at >= 4:
                        registry = boss.get_component_by_class(unreal.RPGBossSummonComponent)
                        guardians = [r.pawn for r in registry.get_summons()
                                     if r.encounter_id == objective.encounter_id and r.phase_index == objective.phase_index
                                     and r.sequence == objective.sequence and r.get_editor_property("bInitialized")
                                     and r.pawn and health(r.pawn) > 0]
                        if not guardians:
                            return  # Wave startup/completion can still be pending.
                        set_health(guardians[0], 0)
                        self.wave_at = now
                        self.publish("summon_defeat_fixture")
                    return
                self.wave_at = None
                if protected(boss):
                    self.drain_at = now + 1
                    return
                if now >= self.drain_at:
                    self.drain_at = now + 1
                    set_health(boss, health(boss) - hp_per_second)
                    self.publish("boss_hp_reduced", health=health(boss))
            except Exception as error:
                self.publish("FAILED", error=str(error))
                self.stop("aborted")

    rehearsal = Rehearsal()
    unreal.raid_boss_progression = rehearsal
    rehearsal.callback = unreal.register_slate_post_tick_callback(rehearsal.tick)
    rehearsal.publish("installed")
    return rehearsal


if __name__ == "__main__":
    main()
