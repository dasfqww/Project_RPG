#include "UI/Inventory/RPGItemCodex.h"

#include "Blueprint/WidgetTree.h"
#include "Components/NativeWidgetHost.h"
#include "Economy/RPGDungeonRewardDefinition.h"
#include "GameFramework/PlayerController.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Equipment/RPGEquipmentProgressionRules.h"
#include "Online/RPGDungeonDefinition.h"
#include "Styling/CoreStyle.h"
#include "UI/Lobby/RPGDungeonRewardPreview.h"
#include "UI/Lobby/RPGEquipmentGrowthModel.h"
#include "UI/RPGWidgetBase.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RPGItemCodex"

namespace
{
	const FName CodexHostName(TEXT("ItemCodexPanel"));
	const FName OreId(TEXT("GameItem.Material.Progression.Ore"));
	const FName TokenId(TEXT("GameItem.Material.Progression.Token"));

	FText DescribeUsage(const RPGItemCodex::FEntry& Entry, bool bCraftable)
	{
		if (Entry.Id.PrimaryAssetName == OreId)
			return LOCTEXT("OreUsage", "장비 강화 재료 · +1 광석 2개 / +2 4개 / +3 6개");
		if (Entry.Id.PrimaryAssetName == TokenId)
			return LOCTEXT("TokenUsage", "선택 제작 재료 · 증표 8개로 목표 장비 1종 제작");
		if (bCraftable)
			return LOCTEXT("CraftUsage", "선택 제작 · 증표 8개");
		return FText::GetEmpty();
	}

	FText DescribeEquipment(const RPGItemCodex::FEntry& Entry)
	{
		FRPGEquipmentProgressionState Growth;
		Growth.GrowthBandId = TEXT("FirstSlice");
		Growth.RuleVersion = 1;
		FRPGEquipmentProgressionContribution Stats;
		if (!RPGEquipmentProgression::TryPreview(Entry.Id, Entry.Version, Growth, Stats))
			return FText::GetEmpty();

		TArray<FText> Lines;
		if (Stats.Attack != 0.f)
			Lines.Add(FText::Format(LOCTEXT("Attack", "공격력 +{0}"), FText::AsNumber(Stats.Attack)));
		if (Stats.MaximumHealth != 0.f)
			Lines.Add(FText::Format(LOCTEXT("Health", "최대 체력 +{0}"), FText::AsNumber(Stats.MaximumHealth)));
		if (Stats.CooldownReductionPercent != 0.f)
			Lines.Add(FText::Format(LOCTEXT("Cooldown", "재사용 대기시간 감소 {0}%"), FText::AsNumber(Stats.CooldownReductionPercent)));
		if (Stats.DamageReductionPercent != 0.f)
			Lines.Add(FText::Format(LOCTEXT("DamageReduction", "받는 피해 감소 {0}%"), FText::AsNumber(Stats.DamageReductionPercent)));
		return FText::Join(LOCTEXT("StatSeparator", " · "), Lines);
	}

	class SItemCodexPanel final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SItemCodexPanel) {}
			SLATE_EVENT(FSimpleDelegate, OnClose)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			OnClose = Args._OnClose;
			Entries = RPGItemCodex::LoadEntries();
			const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
			ChildSlot
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, .66f))
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(16.f)
				[
					SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
					[
						SNew(SBox).WidthOverride(660.f).HeightOverride(530.f)
						[
							SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(.018f, .028f, .043f)).Padding(24.f)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(LOCTEXT("Title", "아이템 사전"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 25))
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(SButton).Text(LOCTEXT("Close", "닫기")).OnClicked(this, &SItemCodexPanel::Close)
									]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
								[
									SAssignNew(SearchBox, SSearchBox).HintText(LOCTEXT("Search", "아이템 이름 또는 장비·재료 검색"))
									.DelayChangeNotificationsWhileTyping(false)
									.OnTextChanged(this, &SItemCodexPanel::RefreshResults)
								]
								+ SVerticalBox::Slot().FillHeight(1.f)
								[
									SAssignNew(Results, SScrollBox)
								]
							]
						]
					]
				]
			];
			RefreshResults(FText::GetEmpty());
		}

		TSharedRef<SWidget> GetSearchFocus() const { return SearchBox.ToSharedRef(); }

		virtual FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent& Event) override
		{
			if (Event.GetKey() != EKeys::Escape) return FReply::Unhandled();
			return Event.IsRepeat() ? FReply::Handled() : Close();
		}

	private:
		FReply Close()
		{
			OnClose.ExecuteIfBound();
			return FReply::Handled();
		}

		void RefreshResults(const FText& Search)
		{
			if (!Results) return;
			Results->ClearChildren();
			Results->ScrollToStart();
			const FString Query = Search.ToString().TrimStartAndEnd();
			bool bHasMatches = false;
			for (const RPGItemCodex::FEntry& Entry : Entries)
			{
				if (!Entry.Matches(Query)) continue;
				bHasMatches = true;
				TArray<FText> Details = Entry.Sources;
				if (!Entry.Usage.IsEmpty()) Details.Add(Entry.Usage);
				Results->AddSlot().Padding(0.f, 0.f, 12.f, 8.f)
				[
					SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(.034f, .052f, .075f)).Padding(14.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
						[
							SNew(STextBlock).Text(Entry.Name).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
						[
							SNew(STextBlock).Text(Entry.Description).AutoWrapText(true)
							.Visibility(Entry.Description.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(FText::Join(FText::FromString(TEXT("\n")), Details)).AutoWrapText(true)
							.ColorAndOpacity(FLinearColor(.64f, .82f, .91f))
						]
					]
				];
			}
			if (!bHasMatches)
			{
				Results->AddSlot()
				[
					SNew(STextBlock).Text(Entries.IsEmpty() ? LOCTEXT("Unavailable", "아이템 정보를 불러올 수 없습니다.")
						: LOCTEXT("NoResults", "검색 결과가 없습니다."))
				];
			}
		}

		FSimpleDelegate OnClose;
		TArray<RPGItemCodex::FEntry> Entries;
		TSharedPtr<SScrollBox> Results;
		TSharedPtr<SSearchBox> SearchBox;
	};
}

bool RPGItemCodex::FEntry::Matches(const FString& Query) const
{
	return Query.IsEmpty() || Name.ToString().Contains(Query) || Category.ToString().Contains(Query);
}

TArray<RPGItemCodex::FEntry> RPGItemCodex::LoadEntries()
{
	const FSoftObjectPath SelectionPath(TEXT("/Game/System/Dungeons/Progression/DA_Dungeon_FirstBoss.DA_Dungeon_FirstBoss"));
	const auto* Dungeon = Cast<URPGDungeonDefinition>(SelectionPath.TryLoad());
	const FText DungeonName = Dungeon && !Dungeon->DisplayName.IsEmpty() ? Dungeon->DisplayName : LOCTEXT("FirstBoss", "첫 보스");
	const auto CraftTargets = RPGEquipmentGrowthUI::CraftTargets();
	TArray<FEntry> Entries;
	for (const ERPGGameDifficulty Difficulty : {ERPGGameDifficulty::Normal, ERPGGameDifficulty::Hard})
	{
		const auto* Reward = RPGDungeonRewardPreview::FindReward(TEXT("Progression.FirstBoss"), Difficulty);
		if (!Reward) continue;
		const FText DifficultyName = Difficulty == ERPGGameDifficulty::Normal ? LOCTEXT("Normal", "일반") : LOCTEXT("Hard", "고난도");
		for (const FRPGDungeonItemRewardEntry& RewardItem : Reward->ItemRewards)
		{
			const URPGItemDefinition& Definition = *RewardItem.ItemDefinition;
			const FPrimaryAssetId Id = Definition.GetPrimaryAssetId();
			const int32 Version = Definition.GetDefinitionVersion();
			auto* Entry = Entries.FindByPredicate([&](const FEntry& Item) { return Item.Id == Id && Item.Version == Version; });
			if (!Entry)
			{
				Entry = &Entries.AddDefaulted_GetRef();
				Entry->Id = Id;
				Entry->Version = Version;
				const auto* Recipe = CraftTargets.FindByPredicate([&](const auto& Target)
					{ return Target.Key.Equals(Id.PrimaryAssetName.ToString(), ESearchCase::CaseSensitive); });
				Entry->Name = Definition.DisplayName;
				if (Entry->Name.IsEmpty())
					Entry->Name = Recipe ? Recipe->Value : FText::FromName(Id.PrimaryAssetName);
				Entry->Category = Definition.ItemCategory == EItemCategory::Equip || Recipe
					? LOCTEXT("Equipment", "장비") : LOCTEXT("Material", "재료");
				TArray<FText> Description;
				const FText Stats = DescribeEquipment(*Entry);
				if (!Stats.IsEmpty()) Description.Add(Stats);
				if (!Definition.Description.IsEmpty()) Description.Add(Definition.Description);
				Entry->Description = FText::Join(FText::FromString(TEXT("\n")), Description);
				Entry->Usage = DescribeUsage(*Entry, Recipe != nullptr);
				if (Id.PrimaryAssetName == TokenId)
					Entry->Sources.Add(LOCTEXT("DismantleSource", "목표 장비 +0 분해 · 증표 1개"));
			}

			const FText Amount = FText::AsNumber(RewardItem.Quantity);
			FText Source = FText::Format(LOCTEXT("Guaranteed", "{0} · {1} 클리어당 {2}개 확정"), DungeonName, DifficultyName, Amount);
			if (RewardItem.PersonalLootWeight > 0)
			{
				FNumberFormattingOptions PercentFormat;
				PercentFormat.SetMaximumFractionalDigits(2);
				Source = FText::Format(LOCTEXT("PersonalLoot", "{0} · {1} 개인 추첨 {2} · {3}개"), DungeonName, DifficultyName,
					FText::AsPercent(RewardItem.PersonalLootWeight / 10000.0, &PercentFormat), Amount);
			}
			Entry->Sources.Add(Source);
		}
	}
	return Entries;
}

URPGWidgetBase* RPGItemCodex::CreateWindow(APlayerController& Owner, FSimpleDelegate OnClose, TSharedPtr<SWidget>& OutSearch)
{
	auto* Window = CreateWidget<URPGWidgetBase>(&Owner);
	if (!Window || !Window->WidgetTree) return nullptr;
	auto* Host = Window->WidgetTree->ConstructWidget<UNativeWidgetHost>(UNativeWidgetHost::StaticClass(), CodexHostName);
	const auto Panel = SNew(SItemCodexPanel).OnClose(MoveTemp(OnClose));
	Host->SetContent(Panel);
	Window->WidgetTree->RootWidget = Host;
	OutSearch = Panel->GetSearchFocus();
	return Window;
}

bool RPGItemCodex::IsWindow(const UUserWidget& Widget)
{
	return Widget.WidgetTree && Widget.WidgetTree->RootWidget && Widget.WidgetTree->RootWidget->GetFName() == CodexHostName;
}

#undef LOCTEXT_NAMESPACE
