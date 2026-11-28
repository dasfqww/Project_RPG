#include "UI/Lobby/RPGDungeonRewardPreview.h"

#include "Economy/RPGDungeonRewardDefinition.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Online/RPGDungeonServerDefinition.h"

#define LOCTEXT_NAMESPACE "RPGDungeonRewardPreview"

namespace
{
	constexpr const TCHAR* FirstBossId = TEXT("Progression.FirstBoss");

	FText FormatChance(int32 BasisPoints)
	{
		FNumberFormattingOptions Formatting;
		Formatting.SetMaximumFractionalDigits(2);
		return FText::AsPercent(BasisPoints / 10000.0, &Formatting);
	}
}

const URPGDungeonRewardDefinition* RPGDungeonRewardPreview::FindReward(const FString& DungeonId, ERPGGameDifficulty Difficulty)
{
	static const FSoftObjectPath FirstBoss(TEXT("/Game/System/Dungeons/Progression/DA_DungeonServer_FirstBoss.DA_DungeonServer_FirstBoss"));
	static const FSoftObjectPath RaidSwordMan(TEXT("/Game/System/Dungeons/DA_DungeonServer_RaidSwordMan.DA_DungeonServer_RaidSwordMan"));
	const FSoftObjectPath* Path = nullptr;
	if (DungeonId.Equals(FirstBossId, ESearchCase::CaseSensitive))
		Path = &FirstBoss;
	else if (DungeonId.Equals(TEXT("Dungeon.RaidSwordMan"), ESearchCase::CaseSensitive))
		Path = &RaidSwordMan;
	if (!Path)
		return nullptr;

	const auto* Dungeon = Cast<URPGDungeonServerDefinition>(Path->ResolveObject());
	if (!Dungeon)
		Dungeon = Cast<URPGDungeonServerDefinition>(Path->TryLoad());
	if (!Dungeon || !Dungeon->DungeonId.Equals(DungeonId, ESearchCase::CaseSensitive))
		return nullptr;

	const auto* Reward = Dungeon->ClearRewardsByDifficulty.Find(Difficulty);
	FString Version, Error;
	TArray<FRPGCurrencyChange> Currency;
	TArray<FRPGDungeonItemReward> Items;
	return Reward && *Reward && (*Reward)->BuildSettlement(Version, Currency, Items, Error) ? Reward->Get() : nullptr;
}

FText RPGDungeonRewardPreview::DescribeClearRewards(const FString& DungeonId, ERPGGameDifficulty Difficulty)
{
	const auto* Reward = FindReward(DungeonId, Difficulty);
	if (!Reward)
		return FText::GetEmpty();

	TArray<FText> Guaranteed, Personal;
	int32 TotalPersonalWeight = 0;
	for (const auto& Currency : Reward->CurrencyChanges)
	{
		const FText Name = Currency.CurrencyCode == FName(TEXT("gold"))
			? LOCTEXT("Gold", "골드") : FText::FromName(Currency.CurrencyCode);
		Guaranteed.Add(FText::Format(LOCTEXT("CurrencyQuantity", "{0} {1}"), Name, FText::AsNumber(Currency.Delta)));
	}
	for (const auto& Item : Reward->ItemRewards)
	{
		const FText& Name = Item.ItemDefinition->DisplayName;
		const FText Quantity = FText::Format(LOCTEXT("ItemQuantity", "{0} ×{1}"),
			Name.IsEmpty() ? FText::FromName(Item.ItemDefinition->GetPrimaryAssetId().PrimaryAssetName) : Name,
			FText::AsNumber(Item.Quantity));
		if (Item.PersonalLootWeight > 0)
		{
			TotalPersonalWeight += Item.PersonalLootWeight;
			Personal.Add(FText::Format(LOCTEXT("ItemChance", "{0} ({1})"), Quantity, FormatChance(Item.PersonalLootWeight)));
		}
		else
		{
			Guaranteed.Add(Quantity);
		}
	}

	TArray<FText> Lines;
	const FText Separator = LOCTEXT("RewardSeparator", " · ");
	if (!Guaranteed.IsEmpty())
		Lines.Add(FText::Format(LOCTEXT("Guaranteed", "클리어 확정 보상: {0}"), FText::Join(Separator, Guaranteed)));
	if (!Personal.IsEmpty())
		Lines.Add(FText::Format(LOCTEXT("Personal", "개인 추첨: {0} 확률로 최대 1종\n{1}"),
			FormatChance(TotalPersonalWeight), FText::Join(Separator, Personal)));
	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

FText RPGDungeonRewardPreview::DescribeFirstBossMaterial(FName Material, const FText& DungeonName)
{
	TArray<FText> Sources;
	FText MaterialName;
	for (const auto Difficulty : {ERPGGameDifficulty::Normal, ERPGGameDifficulty::Hard})
	{
		const auto* Reward = FindReward(FirstBossId, Difficulty);
		if (!Reward)
			continue;

		int64 Quantity = 0;
		for (const auto& Item : Reward->ItemRewards)
		{
			if (Item.PersonalLootWeight == 0 && Item.ItemDefinition->GetPrimaryAssetId().PrimaryAssetName == Material
				&& Item.ItemDefinition->DefinitionVersion == 1)
			{
				MaterialName = Item.ItemDefinition->DisplayName;
				Quantity += Item.Quantity;
			}
		}
		if (Quantity > 0)
		{
			const FText DifficultyName = Difficulty == ERPGGameDifficulty::Normal
				? LOCTEXT("Normal", "일반") : LOCTEXT("Hard", "고난도");
			Sources.Add(FText::Format(LOCTEXT("PerClear", "{0} 클리어당 {1}개 확정"),
				DifficultyName, FText::AsNumber(Quantity)));
		}
	}
	if (Sources.IsEmpty())
		return FText::GetEmpty();

	return FText::Format(LOCTEXT("MaterialSource", "{0} 획득처: {1} · {2}"),
		MaterialName.IsEmpty() ? FText::FromName(Material) : MaterialName, DungeonName,
		FText::Join(LOCTEXT("SourceSeparator", " / "), Sources));
}

#undef LOCTEXT_NAMESPACE
