#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class APlayerController;
class URPGSkillDefinition;

/** Read-only authored-asset verification, after the transient tripod scenario. */
class FRPGSkillAssetCooldownProbe
{
public:
	enum class EResult : uint8 { Waiting, Passed, Failed };
	EResult Tick(APlayerController& Controller);
	const FString& GetFailure() const { return Failure; }

private:
	TStrongObjectPtr<URPGSkillDefinition> Definition;
	FString Failure;
	float ExpectedDuration = 0.0f;
	int16 ExpectedPredictionKey = 0;
	uint8 ClientPhase = 0;
	bool bServerSawActivation = false;
	bool bServerSawExpiry = false;
};
