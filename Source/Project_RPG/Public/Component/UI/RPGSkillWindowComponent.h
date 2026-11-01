#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "RPGSkillWindowComponent.generated.h"

class UDataAsset_InputConfig;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class URPGSkillWindow;
class FRPGWindowInputState;

/** Owns the local skill-window presentation and its input lifetime. */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGSkillWindowComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGSkillWindowComponent();

	void BindInput(UEnhancedInputComponent* Input, const UDataAsset_InputConfig* Config);
	bool RemapFallbackKey(FKey Key);
	FKey GetToggleKey() const;
	TArray<FKey> GetToggleKeys() const;
	void RefreshCloseKeys();
	void SetWindowInputState(const TSharedRef<FRPGWindowInputState>& State);
	URPGSkillWindow* GetWindow() const { return Window; }

	UFUNCTION(BlueprintCallable, Category = "RPG|Skill|UI")
	void ToggleWindow();

	UFUNCTION(BlueprintCallable, Category = "RPG|Skill|UI")
	bool ShowWindow();

	UFUNCTION(BlueprintCallable, Category = "RPG|Skill|UI")
	void CloseWindow();

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|UI")
	bool IsWindowOpen() const { return bWindowOpen; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void UnbindInput();
	void ReleaseHeldAbilityInputs();
	void FocusWindow();

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Skill|UI")
	TSoftClassPtr<URPGSkillWindow> WindowClass;

	/** Used when the input config has no InputTag.ShowSkillWindow action. */
	UPROPERTY(EditDefaultsOnly, Category = "RPG|Skill|UI")
	FKey DefaultToggleKey = EKeys::K;

	UPROPERTY(Transient)
	TObjectPtr<URPGSkillWindow> Window;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ToggleAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> FallbackMappingContext;

	TWeakObjectPtr<UEnhancedInputComponent> BoundInput;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
	TSharedPtr<FRPGWindowInputState> WindowInputState;
	uint32 InputBindingHandle = 0;
	bool bWindowOpen = false;
};
