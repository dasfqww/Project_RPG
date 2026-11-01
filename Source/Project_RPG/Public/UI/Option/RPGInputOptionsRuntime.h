#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
#include "RPGInputOptionsRuntime.generated.h"

UENUM(BlueprintType)
enum class ERPGInputOptionDeviceFilter : uint8
{
	All UMETA(DisplayName = "All Devices"),
	KeyboardMouse UMETA(DisplayName = "Keyboard / Mouse"),
	Gamepad UMETA(DisplayName = "Gamepad")
};

class ARPGPlayerController;
class UDataAsset_InputConfig;
class UInputAction;
class UInputMappingContext;
class ULocalPlayer;
class UWorld;

/** Invalidation only. Listeners schedule a read after the request; never query or mutate in this callback. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRPGInputOptionMappingsChanged, ULocalPlayer*, UWorld*);

/** Identifies one authored key binding, including separate directional and device bindings. */
struct FRPGInputOptionBinding
{
	FGameplayTag InputTag;
	FText DisplayName;
	FKey DefaultKey;
	FKey CurrentKey;
	bool bCanRemap = false;
	/** Weak authored identities prevent a displayed binding from targeting a replacement configuration/action. */
	TWeakObjectPtr<const UDataAsset_InputConfig> SourceConfig;
	TWeakObjectPtr<const UInputAction> Action;
	/** Additional rows retain their provider and both mapping identities, including when they expire. */
	bool bIsAdditionalMapping = false;
	TWeakObjectPtr<const UObject> AdditionalInputProvider;
	TWeakObjectPtr<const UInputMappingContext> SourceMappingContext;
	TWeakObjectPtr<const UInputMappingContext> AppliedMappingContext;
};

/** Options policy is supplied by RPGOptionsUIRuntime; live mappings stay with their controller or feature owner. */
class PROJECT_RPG_API IRPGInputOptionsRuntime : public IModularFeature
{
public:
	static FName GetFeatureName();
	static IRPGInputOptionsRuntime* Get();

	/** Native player/world notification. Access does not load or activate the options implementation. */
	static FOnRPGInputOptionMappingsChanged& OnMappingsChanged();
	static void NotifyMappingsChanged(ULocalPlayer* Player, UWorld* World);

	virtual void BuildRows(const ARPGPlayerController* Controller, const UDataAsset_InputConfig* PreviewConfig,
		TArray<FRPGInputOptionBinding>& OutRows) const = 0;
	/** Read-only lookup requires the displayed additional provider/source/applied/action identities to remain active. */
	virtual FKey GetAdditionalBindingKey(const ARPGPlayerController* Controller,
		const FRPGInputOptionBinding& Binding) const = 0;
	/** Presentation filters never modify mappings or saved preferences. */
	virtual bool MatchesFilter(const FRPGInputOptionBinding& Binding, const FText& SearchText,
		ERPGInputOptionDeviceFilter DeviceFilter, bool bChangedOnly) const = 0;
	/** Changed editable primary and feature bindings, independently of presentation filters. */
	virtual void BuildDefaultRestoration(const ARPGPlayerController* Controller,
		TArray<FRPGInputOptionBinding>& OutBindings) const = 0;
	virtual bool TryRestoreDefaults(ARPGPlayerController* Controller,
		const TArray<FRPGInputOptionBinding>& ExpectedBindings, FText& OutError) const = 0;
	virtual bool TryApply(ARPGPlayerController* Controller, FGameplayTag InputTag,
		FKey DefaultKey, FKey NewKey, FText& OutError) const = 0;
	/** Apply only while a displayed authored binding still has its source/action/current key.
	 * Identity-free legacy rows retain TryApply semantics.
	 */
	virtual bool TryApplyBinding(ARPGPlayerController* Controller, const FRPGInputOptionBinding& ExpectedBinding,
		FKey NewKey, FText& OutError) const = 0;
};
