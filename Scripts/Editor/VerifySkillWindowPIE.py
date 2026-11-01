"""Verify authored skill widgets in a fresh, authoritative PIE session.

Run from the editor's Cmd input:
    py "C:/UE5/Project_RPG/Scripts/Editor/VerifySkillWindowPIE.py"

Creates only a temporary window, uses real button handlers, restores the skill
level/SP, and never saves assets. This does not simulate mouse input or RPC
transport; use the network runner separately for replication coverage.
"""

import time

import unreal


def _named_widget(owner, name):
    # BP variables are public, but native BindWidget properties can be protected.
    # Resolve the latter through the instance's duplicated tree, not the asset.
    for tree_name in ("WidgetTree", "WidgetTree_0", "WidgetTree_1"):
        widget = unreal.find_object(None, owner.get_path_name() + "." + tree_name + "." + name)
        if widget:
            return widget
    raise RuntimeError("Missing runtime widget: " + owner.get_path_name() + "." + name)


def _check(condition, message):
    if not condition:
        raise AssertionError(message)


def verify_skill_window_pie(keep_open=False):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    _check(world is not None, "Start PIE before running this check")
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    _check(controller is not None and pawn is not None, "PIE needs a possessed player")
    _check(pawn.get_local_role() == unreal.NetRole.ROLE_AUTHORITY,
           "This UI check requires standalone/authority PIE, not a predicted client")
    widget_class = unreal.load_class(None, "/Game/Blueprints/UI/Skill/WBP_SkillWindow.WBP_SkillWindow_C")
    # Create is BlueprintInternalUseOnly, so it has no direct Python glue.
    window = unreal.get_default_object(unreal.WidgetLibrary).call_method(
        "Create", args=(controller, widget_class, controller))
    window.add_to_viewport(100)
    window.activate_widget()
    tick_state = {"handle": None, "deadline": time.monotonic() + 30.0}

    def inspect(_delta):
        model = window.get_editor_property("skill_view_model")
        slots = list(model.get_editor_property("skill_slots"))
        prefix = window.get_path_name() + "."
        rows = [row for row in unreal.WidgetLibrary.get_all_widgets_of_class(
            world, unreal.URPGSkillListSlot, False) if row.get_path_name().startswith(prefix)]
        if not slots or len(rows) != len(slots):
            if time.monotonic() < tick_state["deadline"]:
                return
            unreal.unregister_slate_post_tick_callback(tick_state["handle"])
            window.remove_from_parent()
            unreal.log_error("[RPG_SKILL_UI] FAILED: catalog/list entries did not become ready")
            return

        unreal.unregister_slate_post_tick_callback(tick_state["handle"])
        adjuster = None
        target = None
        baseline_level = None
        baseline_sp = model.get_editor_property("remaining_sp")
        try:
            for row in rows:
                item = unreal.UserObjectListEntryLibrary.get_list_item_object(row)
                _check(item in slots, "Row must use the window's existing slot model")
                _check(str(_named_widget(row, "SkillNameText").get_text()) ==
                       str(item.get_editor_property("skill_name")), "Skill name still shows a placeholder")
            row = rows[0]
            target = unreal.UserObjectListEntryLibrary.get_list_item_object(row)
            adjusters = [widget for widget in unreal.WidgetLibrary.get_all_widgets_of_class(
                world, unreal.RPGSkillLevelAdjuster, False)
                if widget.get_path_name().startswith(row.get_path_name() + ".")]
            _check(len(adjusters) == 1, "Row needs exactly one level adjuster")
            adjuster = adjusters[0]
            _check(adjuster.get_editor_property("main_view_model") == model, "Adjuster has the wrong main model")
            _check(adjuster.get_editor_property("target_slot_view_model") == target, "Adjuster has the wrong slot model")
            level_text = _named_widget(adjuster, "CurrentSkillLevelText")
            points_text = _named_widget(window, "SkillPointsText")
            increase = _named_widget(adjuster, "Btn_IncreaseLevel")
            decrease = _named_widget(adjuster, "Btn_DecreaseLevel")
            baseline_level = target.get_editor_property("skill_level")
            # The handlers honor Shift (max/min). Start at level 1 so even an
            # unexpected held modifier cannot refund below the initial state.
            _check(baseline_level == 1, "Use fresh PIE with the first tested skill at level 1")
            initial_cost = target.get_editor_property("next_level_cost")
            _check(increase.get_is_enabled() and initial_cost > 0, "Fixture needs an affordable level-up")

            def check_points():
                expected = "SP {} / {}".format(model.get_editor_property("remaining_sp"),
                                               model.get_editor_property("total_sp"))
                _check(str(points_text.get_text()) == expected, "SP display is out of sync")

            check_points()
            adjuster.call_method("OnIncreaseLevelClicked")
            _check(target.get_editor_property("skill_level") == baseline_level + 1, "Button handler did not level up")
            _check(str(level_text.get_text()) == str(baseline_level + 1), "Displayed level did not update")
            _check(model.get_editor_property("remaining_sp") == baseline_sp - initial_cost, "Wrong SP deduction")
            _check(decrease.get_is_enabled(), "Refund button should be enabled")
            check_points()

            for _ in range(32):
                if not increase.get_is_enabled():
                    break
                previous_level = target.get_editor_property("skill_level")
                adjuster.call_method("OnIncreaseLevelClicked")
                _check(target.get_editor_property("skill_level") == previous_level + 1, "Enabled button made no progress")
                _check(str(level_text.get_text()) == str(previous_level + 1), "Level display stopped updating")
                check_points()
            _check(not increase.get_is_enabled(), "Level-up button must stop at the cap/budget")
            next_cost = target.get_editor_property("next_level_cost")
            _check(next_cost == 0 or next_cost > model.get_editor_property("remaining_sp"),
                   "Button disabled while a level-up remains affordable")

            while target.get_editor_property("skill_level") > baseline_level:
                previous_level = target.get_editor_property("skill_level")
                adjuster.call_method("OnDecreaseLevelClicked")
                _check(target.get_editor_property("skill_level") == previous_level - 1, "Refund handler made no progress")
            _check(model.get_editor_property("remaining_sp") == baseline_sp, "SP was not fully restored")
            _check(str(level_text.get_text()) == str(baseline_level), "Restored level is not displayed")
            check_points()
            unreal.log("[RPG_SKILL_UI] PASS Names={} ButtonLevel=1 SPDisplay=1 BudgetOrCap=1 FullRefund=1".format(len(rows)))
        except Exception as error:
            unreal.log_error("[RPG_SKILL_UI] FAILED: " + str(error))
        finally:
            # Also restore after a failed assertion; never leave test SP spent.
            if adjuster is not None and baseline_level is not None:
                for _ in range(32):
                    if target.get_editor_property("skill_level") <= baseline_level:
                        break
                    previous_level = target.get_editor_property("skill_level")
                    adjuster.call_method("OnDecreaseLevelClicked")
                    if target.get_editor_property("skill_level") >= previous_level:
                        unreal.log_error("[RPG_SKILL_UI] RESTORE_FAILED: refund handler made no progress")
                        break
            if not keep_open:
                window.remove_from_parent()

    tick_state["handle"] = unreal.register_slate_post_tick_callback(inspect)
    unreal.log("[RPG_SKILL_UI] Waiting for real list entries")


verify_skill_window_pie()
