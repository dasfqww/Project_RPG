#pragma once

#include "AssetTypeActions_Base.h"

class FAssetTypeActions_RPGSkillConfig : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions 인터페이스 구현
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_RPGSkillConfig", "Skill Config"); }
	virtual FColor GetTypeColor() const override { return FColor(255, 128, 0); } // 주황색
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
};
