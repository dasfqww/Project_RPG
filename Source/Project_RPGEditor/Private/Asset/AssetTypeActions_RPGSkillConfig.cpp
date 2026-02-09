#include "Asset/AssetTypeActions_RPGSkillConfig.h"
#include "DataAsset/RPGSkillConfig.h"

UClass* FAssetTypeActions_RPGSkillConfig::GetSupportedClass() const
{
	return URPGSkillConfig::StaticClass();
}

uint32 FAssetTypeActions_RPGSkillConfig::GetCategories()
{
	return EAssetTypeCategories::Gameplay;
}