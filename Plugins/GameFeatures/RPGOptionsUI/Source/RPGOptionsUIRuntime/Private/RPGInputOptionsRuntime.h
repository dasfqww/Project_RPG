#pragma once

#include "UI/Option/RPGInputOptionsRuntime.h"

class FRPGInputOptionsRuntime final : public IRPGInputOptionsRuntime
{
public:
	virtual void BuildRows(const ARPGPlayerController* Controller, const UDataAsset_InputConfig* PreviewConfig,
		TArray<FRPGInputOptionBinding>& OutRows) const override;
	virtual FKey GetAdditionalBindingKey(const ARPGPlayerController* Controller,
		const FRPGInputOptionBinding& Binding) const override;
	virtual bool MatchesFilter(const FRPGInputOptionBinding& Binding, const FText& SearchText,
		ERPGInputOptionDeviceFilter DeviceFilter, bool bChangedOnly) const override;
	virtual void BuildDefaultRestoration(const ARPGPlayerController* Controller,
		TArray<FRPGInputOptionBinding>& OutBindings) const override;
	virtual bool TryRestoreDefaults(ARPGPlayerController* Controller,
		const TArray<FRPGInputOptionBinding>& ExpectedBindings, FText& OutError) const override;
	virtual bool TryApply(ARPGPlayerController* Controller, FGameplayTag InputTag,
		FKey DefaultKey, FKey NewKey, FText& OutError) const override;
	virtual bool TryApplyBinding(ARPGPlayerController* Controller, const FRPGInputOptionBinding& ExpectedBinding,
		FKey NewKey, FText& OutError) const override;
};
