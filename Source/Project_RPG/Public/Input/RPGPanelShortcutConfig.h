#pragma once

#include "Engine/DataAsset.h"
#include "Framework/Commands/InputChord.h"
#include "RPGPanelShortcutConfig.generated.h"

UENUM(BlueprintType)
enum class ERPGPanelShortcutAction : uint8
{
	Inventory,
	Equipment,
	Skills,
	RewardMail,
};

USTRUCT(BlueprintType)
struct FRPGPanelShortcut
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Shortcut")
	FInputChord Chord;

	UPROPERTY(EditAnywhere, Category = "Shortcut")
	ERPGPanelShortcutAction Action = ERPGPanelShortcutAction::Inventory;
};

/** Project-authored keyboard combinations for local player panels. */
UCLASS()
class PROJECT_RPG_API URPGPanelShortcutConfig final : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Shortcuts", meta = (TitleProperty = "Action"))
	TArray<FRPGPanelShortcut> Shortcuts;

	bool BuildShortcutMap(TMap<FInputChord, ERPGPanelShortcutAction>& OutShortcuts, FText& OutError) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
