#include "RPGEquipmentStatsPanel.h"

#include "Ability/RPGGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Algo/Compare.h"
#include "Attribute/RPGAttributeSet.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Character/RPGPlayer.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/NativeWidgetHost.h"
#include "Components/SizeBox.h"
#include "PlayerClass/RPGClassIdentityStats.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RPGEquipmentStats"

namespace
{
	constexpr float PanelWidth = 304.f;
	constexpr float PanelGap = 12.f;
	constexpr FLinearColor Accent(0.85f, 0.70f, 0.47f);

	enum class EStatTab : uint8 { Offense, Defense, Utility };

	FText Number(float Value)
	{
		FNumberFormattingOptions Format;
		Format.SetMaximumFractionalDigits(2);
		return FMath::IsFinite(Value) ? FText::AsNumber(Value, &Format) : LOCTEXT("Unavailable", "—");
	}

	FText Percent(float Fraction)
	{
		FNumberFormattingOptions Format;
		Format.SetMaximumFractionalDigits(2);
		return FMath::IsFinite(Fraction) ? FText::AsPercent(Fraction, &Format) : LOCTEXT("Unavailable", "—");
	}

	FText Multiplier(float Value)
	{
		return FMath::IsFinite(Value)
			? FText::Format(LOCTEXT("Multiplier", "{0}배"), Number(Value))
			: LOCTEXT("Unavailable", "—");
	}

	class SRPGEquipmentStats final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRPGEquipmentStats) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, UUserWidget* InWindow)
		{
			Window = InWindow;
			ChildSlot
			[
				SNew(SBox).WidthOverride(PanelWidth)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.065f, 0.048f, 0.036f))
					.Padding(FMargin(14.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
						[
							SNew(STextBlock).Text(LOCTEXT("Title", "능력치")).ColorAndOpacity(Accent)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
							[ MakeTab(EStatTab::Offense, LOCTEXT("Offense", "공격")) ]
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
							[ MakeTab(EStatTab::Defense, LOCTEXT("Defense", "방어")) ]
							+ SHorizontalBox::Slot().FillWidth(1.f)
							[ MakeTab(EStatTab::Utility, LOCTEXT("Utility", "전투 보조")) ]
						]
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SAssignNew(Scroll, SScrollBox)
							+ SScrollBox::Slot()[SAssignNew(Rows, SVerticalBox)]
						]
					]
				]
			];
			Refresh();
		}

		void Tick(const FGeometry& Geometry, const double Time, const float DeltaSeconds) override
		{
			SCompoundWidget::Tick(Geometry, Time, DeltaSeconds);
			if (Time < NextRefresh) return;
			NextRefresh = Time + 0.1;
			Refresh();
		}

	private:
		TSharedRef<SWidget> MakeTab(EStatTab Tab, const FText& Label)
		{
			return SNew(SCheckBox)
				.Style(&FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
				.Padding(FMargin(4.f, 7.f))
				.HAlign(HAlign_Center)
				.IsChecked_Lambda([this, Tab] { return CurrentTab == Tab ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Tab](ECheckBoxState)
				{
					if (CurrentTab == Tab) return;
					CurrentTab = Tab;
					Scroll->ScrollToStart();
					Refresh();
				})
				[
					SNew(STextBlock).Text(Label)
					.ColorAndOpacity_Lambda([this, Tab] { return FSlateColor(CurrentTab == Tab ? Accent : FLinearColor::White); })
				];
		}

		void Refresh()
		{
			TArray<FRPGIdentityStatRow, TInlineAllocator<8>> Display;
			TArray<FRPGIdentityStatRow> Identity;
			FText IdentityTitle;
			const auto* Owner = Window.Get();
			const auto* Player = Owner ? Cast<ARPGPlayer>(Owner->GetOwningPlayerPawn()) : nullptr;
			const auto* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
			const auto* Attributes = ASC ? ASC->GetSet<URPGAttributeSet>() : nullptr;
			if (Attributes)
			{
				const auto Add = [&Display](const FText& Label, const FText& Value, const FText& Tooltip = FText::GetEmpty())
				{
					Display.Add({Label, Value, Tooltip});
				};
				switch (CurrentTab)
				{
				case EStatTab::Offense:
					Add(LOCTEXT("Attack", "공격력"), Number(Attributes->GetAttack()));
					Add(LOCTEXT("CritChance", "치명타 확률"), Percent(FMath::Clamp(Attributes->GetCriticalChance(), 0.f, 1.f)));
					Add(LOCTEXT("CritDamage", "치명타 피해"), Percent(Attributes->GetCriticalDamage()));
					Add(LOCTEXT("AttackRate", "공격 속도 배율"), Multiplier(Attributes->GetAttackSpeed()));
					Add(LOCTEXT("EquipmentAttackRate", "무기 공격 속도 보정"), Percent(Attributes->GetAttackSpeedPercent() / 100.f));
					break;
				case EStatTab::Defense:
					Add(LOCTEXT("MaxHealth", "최대 체력"), Number(Attributes->GetMaxHealth()));
					Add(LOCTEXT("DefenseValue", "방어력"), Number(Attributes->GetDefense()));
					Add(LOCTEXT("Mitigation", "받는 피해 감소"), Percent(FMath::Clamp(Attributes->GetDamageReductionPercent(), 0.f, 90.f) / 100.f));
					break;
				case EStatTab::Utility:
					Add(LOCTEXT("MaxMana", "최대 마나"), Number(Attributes->GetMaxMana()));
					Add(LOCTEXT("MoveSpeed", "이동 속도 배율"), Multiplier(Attributes->GetMoveSpeed() *
						FMath::Max(0.f, 1.f + Attributes->GetMoveSpeedPercent() / 100.f)),
						LOCTEXT("MoveSpeedTip", "이동 속도에 적용되는 능력치 배율입니다."));
					Add(LOCTEXT("Cooldown", "재사용 대기시간 감소"), Percent(FMath::Clamp(Attributes->GetCooldownReductionPercent(), 0.f, 50.f) / 100.f));
					Add(LOCTEXT("Stagger", "무력화량 증가"), Percent(FMath::Clamp(Attributes->GetStaggerContributionBonusPercent(), 0.f, 100.f) / 100.f));
					const auto* Skills = Player->GetPlayerSkillComponent();
					const auto* Class = Skills ? Skills->GetPlayerClassDefinition() : nullptr;
					const auto* Ability = Class ? Class->IdentityAbility.GetDefaultObject() : nullptr;
					if (const auto* Provider = Cast<IRPGClassIdentityStats>(Ability))
						Provider->ReadIdentityStats(*ASC, IdentityTitle, Identity);
					break;
				}
			}
			if (Display.IsEmpty())
				Display.Add({LOCTEXT("Waiting", "능력치를 불러오는 중"), {}, {}});

			if (!Identity.IsEmpty())
			{
				Display.Add({IdentityTitle, {}, {}});
				Display.Append(Identity);
			}
			const auto Equal = [](const FRPGIdentityStatRow& A, const FRPGIdentityStatRow& B)
			{
				return A.Label.EqualTo(B.Label) && A.Value.EqualTo(B.Value) && A.Tooltip.EqualTo(B.Tooltip);
			};
			if (Displayed.Num() == Display.Num() &&
				Algo::Compare(Displayed, Display, Equal)) return;
			Displayed = MoveTemp(Display);
			Rows->ClearChildren();
			for (const FRPGIdentityStatRow& Row : Displayed)
			{
				const bool bHeading = Row.Value.IsEmpty();
				Rows->AddSlot().AutoHeight().Padding(0.f, bHeading ? 12.f : 0.f, 3.f, 1.f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(bHeading ? FLinearColor::Transparent : FLinearColor(0.10f, 0.077f, 0.056f))
					.Padding(FMargin(8.f, 8.f)).ToolTipText(Row.Tooltip)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[ SNew(STextBlock).Text(Row.Label).ColorAndOpacity(bHeading ? Accent : FLinearColor::White) ]
						+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
						[ SNew(STextBlock).Text(Row.Value).ColorAndOpacity(Accent) ]
					]
				];
			}
		}

		TWeakObjectPtr<UUserWidget> Window;
		TSharedPtr<SScrollBox> Scroll;
		TSharedPtr<SVerticalBox> Rows;
		TArray<FRPGIdentityStatRow, TInlineAllocator<8>> Displayed;
		double NextRefresh = 0.0;
		EStatTab CurrentTab = EStatTab::Offense;
	};
}

void RPGEquipmentStatsPanel::Attach(UUserWidget& EquipmentWindow)
{
	auto* Tree = EquipmentWindow.WidgetTree.Get();
	if (!Tree || !Tree->FindWidget(TEXT("EquipmentSilhouetteLayers")) ||
		Tree->FindWidget(TEXT("EquipmentStatsHost"))) return;

	auto* Root = Cast<USizeBox>(Tree->FindWidget(TEXT("RootSizeBox")));
	auto* ExistingWindow = Root ? Root->GetChildAt(0) : nullptr;
	if (!ExistingWindow) return;

	const bool bHasWidth = Root->IsWidthOverride();
	const float OriginalWidth = Root->GetWidthOverride();
	auto* Layout = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("EquipmentStatsLayout"));
	auto* Host = Tree->ConstructWidget<UNativeWidgetHost>(UNativeWidgetHost::StaticClass(), TEXT("EquipmentStatsHost"));
	Host->SetContent(SNew(SRPGEquipmentStats, &EquipmentWindow));

	Root->SetContent(Layout);
	Layout->AddChildToHorizontalBox(ExistingWindow)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	auto* StatsSlot = Layout->AddChildToHorizontalBox(Host);
	StatsSlot->SetPadding(FMargin(PanelGap, 0.f, 0.f, 0.f));
	if (bHasWidth)
		Root->SetWidthOverride(OriginalWidth + PanelWidth + PanelGap);
}

#undef LOCTEXT_NAMESPACE
