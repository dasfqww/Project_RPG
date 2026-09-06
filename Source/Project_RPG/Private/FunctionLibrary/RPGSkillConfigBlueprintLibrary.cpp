#include "FunctionLibrary/RPGSkillConfigBlueprintLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillConfigBlueprintLibrary)

FInstancedStruct
URPGSkillConfigBlueprintLibrary::MakeInstantExecutionConfig(
	const FRPGSkillInstantExecutionConfig& Config)
{
	return FInstancedStruct::Make(Config);
}

FInstancedStruct
URPGSkillConfigBlueprintLibrary::MakeChargeExecutionConfig(
	const FRPGSkillChargeExecutionConfig& Config)
{
	return FInstancedStruct::Make(Config);
}

FInstancedStruct
URPGSkillConfigBlueprintLibrary::MakeHoldingExecutionConfig(
	const FRPGSkillHoldingExecutionConfig& Config)
{
	return FInstancedStruct::Make(Config);
}

FInstancedStruct
URPGSkillConfigBlueprintLibrary::MakeComboExecutionConfig(
	const FRPGSkillComboExecutionConfig& Config)
{
	return FInstancedStruct::Make(Config);
}

FInstancedStruct
URPGSkillConfigBlueprintLibrary::MakeCastingExecutionConfig(
	const FRPGSkillCastingExecutionConfig& Config)
{
	return FInstancedStruct::Make(Config);
}

FInstancedStruct
URPGSkillConfigBlueprintLibrary::MakeChainExecutionConfig(
	const FRPGSkillChainExecutionConfig& Config)
{
	return FInstancedStruct::Make(Config);
}

FInstancedStruct
URPGSkillConfigBlueprintLibrary::MakeCameraDirectionTargetingConfig(
	const FRPGSkillCameraDirectionTargetingConfig& Config)
{
	return FInstancedStruct::Make(Config);
}

FInstancedStruct
URPGSkillConfigBlueprintLibrary::MakeSoftTargetingConfig(
	const FRPGSkillSoftTargetingConfig& Config)
{
	return FInstancedStruct::Make(Config);
}

FInstancedStruct
URPGSkillConfigBlueprintLibrary::MakeGroundPointTargetingConfig(
	const FRPGSkillGroundPointTargetingConfig& Config)
{
	return FInstancedStruct::Make(Config);
}
