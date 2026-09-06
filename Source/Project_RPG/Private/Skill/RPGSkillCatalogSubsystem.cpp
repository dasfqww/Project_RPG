#include "Skill/RPGSkillCatalogSubsystem.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Skill/RPGSkillDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillCatalogSubsystem)

void URPGSkillCatalogSubsystem::Deinitialize()
{
	PendingCallbacks.Reset();
	PendingAssetIds.Reset();
	LoadHandle.Reset();
	CachedDefinitions.Reset();
	bLoadInProgress = false;
	bCatalogReady = false;
	Super::Deinitialize();
}

void URPGSkillCatalogSubsystem::RequestSkillDefinitions(
	FSimpleDelegate OnReady)
{
	if (bCatalogReady)
	{
		OnReady.ExecuteIfBound();
		return;
	}

	if (OnReady.IsBound())
	{
		PendingCallbacks.Add(MoveTemp(OnReady));
	}
	if (bLoadInProgress)
	{
		return;
	}

	bLoadInProgress = true;
	UAssetManager& AssetManager = UAssetManager::Get();
	AssetManager.GetPrimaryAssetIdList(
		URPGSkillDefinition::PrimaryAssetType,
		PendingAssetIds);
	PendingAssetIds.Sort(
		[](const FPrimaryAssetId& Left, const FPrimaryAssetId& Right)
		{
			return Left.ToString() < Right.ToString();
		});

	if (PendingAssetIds.IsEmpty())
	{
		FinishRequest();
		return;
	}

	LoadHandle = AssetManager.LoadPrimaryAssets(
		PendingAssetIds,
		TArray<FName>(),
		FStreamableDelegate::CreateUObject(
			this,
			&ThisClass::HandleDefinitionsLoaded));
}

TArray<URPGSkillDefinition*>
URPGSkillCatalogSubsystem::GetSkillDefinitions() const
{
	TArray<URPGSkillDefinition*> Result;
	Result.Reserve(CachedDefinitions.Num());
	for (URPGSkillDefinition* Definition : CachedDefinitions)
	{
		if (IsValid(Definition))
		{
			Result.Add(Definition);
		}
	}
	return Result;
}

URPGSkillDefinition* URPGSkillCatalogSubsystem::FindSkillDefinition(
	const FGameplayTag SkillTag) const
{
	if (!SkillTag.IsValid())
	{
		return nullptr;
	}

	for (URPGSkillDefinition* Definition : CachedDefinitions)
	{
		if (IsValid(Definition) && Definition->SkillTag == SkillTag)
		{
			return Definition;
		}
	}
	return nullptr;
}

void URPGSkillCatalogSubsystem::HandleDefinitionsLoaded()
{
	CachedDefinitions.Reset();
	TSet<FGameplayTag> RegisteredTags;
	const UAssetManager& AssetManager = UAssetManager::Get();

	for (const FPrimaryAssetId& AssetId : PendingAssetIds)
	{
		URPGSkillDefinition* Definition =
			AssetManager.GetPrimaryAssetObject<URPGSkillDefinition>(AssetId);
		if (!IsValid(Definition))
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Skill catalog could not load %s."),
				*AssetId.ToString());
			continue;
		}
		if (!Definition->SkillTag.IsValid())
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Skill catalog ignored %s because SkillTag is invalid."),
				*Definition->GetPathName());
			continue;
		}
		if (RegisteredTags.Contains(Definition->SkillTag))
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Skill catalog ignored duplicate tag %s on %s."),
				*Definition->SkillTag.ToString(),
				*Definition->GetPathName());
			continue;
		}

		RegisteredTags.Add(Definition->SkillTag);
		CachedDefinitions.Add(Definition);
	}

	CachedDefinitions.Sort(
		[](const URPGSkillDefinition& Left, const URPGSkillDefinition& Right)
		{
			return Left.SkillTag.ToString() < Right.SkillTag.ToString();
		});
	FinishRequest();
}

void URPGSkillCatalogSubsystem::FinishRequest()
{
	bLoadInProgress = false;
	bCatalogReady = true;
	PendingAssetIds.Reset();
	LoadHandle.Reset();

	TArray<FSimpleDelegate> Callbacks = MoveTemp(PendingCallbacks);
	PendingCallbacks.Reset();
	for (FSimpleDelegate& Callback : Callbacks)
	{
		Callback.ExecuteIfBound();
	}
}
