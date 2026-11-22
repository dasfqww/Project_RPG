#include "Input/RPGPanelShortcutConfig.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "RPGPanelShortcutConfig"

bool URPGPanelShortcutConfig::BuildShortcutMap(TMap<FInputChord, ERPGPanelShortcutAction>& OutShortcuts, FText& OutError) const
{
	OutShortcuts.Reset();
	OutError = FText::GetEmpty();

	TMap<FInputChord, ERPGPanelShortcutAction> Validated;
	const UEnum* Actions = StaticEnum<ERPGPanelShortcutAction>();
	for (int32 Index = 0; Index < Shortcuts.Num(); ++Index)
	{
		const FRPGPanelShortcut& Shortcut = Shortcuts[Index];
		const FInputChord& Chord = Shortcut.Chord;
		if (!Chord.Key.IsValid() || Chord.Key.GetMenuCategory() != EKeys::NAME_KeyboardCategory
			|| Chord.Key.IsModifierKey() || !Chord.Key.IsDigital() || Chord.Key == EKeys::AnyKey
			|| !(Chord.bAlt || Chord.bCtrl || Chord.bShift || Chord.bCmd))
		{
			OutError = FText::Format(LOCTEXT("InvalidChord", "Shortcut row {0} needs a keyboard key with at least one modifier."), Index + 1);
			return false;
		}
		const int64 ActionValue = static_cast<int64>(Shortcut.Action);
		if (!Actions->IsValidEnumValue(ActionValue) || ActionValue == Actions->GetMaxEnumValue())
		{
			OutError = FText::Format(LOCTEXT("InvalidAction", "Shortcut row {0} has an unknown panel."), Index + 1);
			return false;
		}
		if (Chord.bAlt && (Chord.Key == EKeys::Enter || Chord.Key == EKeys::Tab || Chord.Key == EKeys::F4))
		{
			OutError = FText::Format(LOCTEXT("ReservedChord", "Shortcut row {0} uses a reserved keyboard combination."), Index + 1);
			return false;
		}
		if (Validated.Contains(Chord))
		{
			OutError = FText::Format(LOCTEXT("DuplicateChord", "Shortcut row {0} repeats {1}."), Index + 1, Chord.GetInputText());
			return false;
		}
		Validated.Add(Chord, Shortcut.Action);
	}

	OutShortcuts = MoveTemp(Validated);
	return true;
}

#if WITH_EDITOR
EDataValidationResult URPGPanelShortcutConfig::IsDataValid(FDataValidationContext& Context) const
{
	TMap<FInputChord, ERPGPanelShortcutAction> ShortcutsByChord;
	FText Error;
	if (!BuildShortcutMap(ShortcutsByChord, Error))
	{
		Context.AddError(Error);
		return EDataValidationResult::Invalid;
	}
	return CombineDataValidationResults(Super::IsDataValid(Context), EDataValidationResult::Valid);
}
#endif

#undef LOCTEXT_NAMESPACE
