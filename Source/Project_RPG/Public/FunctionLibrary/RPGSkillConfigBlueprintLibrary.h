#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "Skill/RPGSkillTargetingTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "RPGSkillConfigBlueprintLibrary.generated.h"

/**
 * Type-safe construction boundary for skill policy configuration.
 *
 * FInstancedStruct is intentionally kept at the data-asset boundary. Gameplay
 * code, editor automation, and Blueprints construct it through these helpers so
 * callers cannot accidentally pair a policy with an unrelated struct type.
 */
UCLASS()
class PROJECT_RPG_API URPGSkillConfigBlueprintLibrary
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Config|Execution")
	static FInstancedStruct MakeInstantExecutionConfig(
		const FRPGSkillInstantExecutionConfig& Config);

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Config|Execution")
	static FInstancedStruct MakeChargeExecutionConfig(
		const FRPGSkillChargeExecutionConfig& Config);

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Config|Execution")
	static FInstancedStruct MakeHoldingExecutionConfig(
		const FRPGSkillHoldingExecutionConfig& Config);

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Config|Execution")
	static FInstancedStruct MakeComboExecutionConfig(
		const FRPGSkillComboExecutionConfig& Config);

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Config|Execution")
	static FInstancedStruct MakeCastingExecutionConfig(
		const FRPGSkillCastingExecutionConfig& Config);

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Config|Execution")
	static FInstancedStruct MakeChainExecutionConfig(
		const FRPGSkillChainExecutionConfig& Config);

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Config|Targeting")
	static FInstancedStruct MakeCameraDirectionTargetingConfig(
		const FRPGSkillCameraDirectionTargetingConfig& Config);

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Config|Targeting")
	static FInstancedStruct MakeSoftTargetingConfig(
		const FRPGSkillSoftTargetingConfig& Config);

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Config|Targeting")
	static FInstancedStruct MakeGroundPointTargetingConfig(
		const FRPGSkillGroundPointTargetingConfig& Config);
};
