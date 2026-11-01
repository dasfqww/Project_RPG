#include "UI/Content/RPGDungeonPartyCombatWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

TSharedRef<SWidget> URPGDungeonPartyCombatWidget::RebuildWidget()
{
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		auto* Width = WidgetTree->ConstructWidget<USizeBox>();
		Width->SetWidthOverride(300.f);
		auto* Position = Canvas->AddChildToCanvas(Width);
		Position->SetAnchors(FAnchors(0.f, .18f));
		Position->SetOffsets(FMargin(24.f, 0.f, 0.f, 0.f));
		Position->SetAutoSize(true);
		auto* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetBrushColor(FLinearColor(.025f, .04f, .06f, .88f));
		Card->SetPadding(FMargin(12.f));
		Width->SetContent(Card);
		auto* Content = WidgetTree->ConstructWidget<UVerticalBox>();
		Card->SetContent(Content);
		Header = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DungeonPartyCombatHeader"));
		Header->SetAutoWrapText(true);
		auto HeaderFont = Header->GetFont(); HeaderFont.Size = 16; Header->SetFont(HeaderFont);
		Content->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		Rows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DungeonPartyCombatRows"));
		Content->AddChildToVerticalBox(Rows);
		NoticeFeed = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DungeonPartyCombatNotices"));
		NoticeFeed->SetAutoWrapText(true);
		auto NoticeFont = NoticeFeed->GetFont(); NoticeFont.Size = 12; NoticeFeed->SetFont(NoticeFont);
		NoticeFeed->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, .88f, .65f)));
		Content->AddChildToVerticalBox(NoticeFeed)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		ScopeNote = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DungeonPartyCombatScopeNote"));
		ScopeNote->SetAutoWrapText(true);
		ScopeNote->SetText(NSLOCTEXT("RPGPartyCombat", "MembershipScopeNote", "서버 확인 정보를 표시합니다. 유예 경과만으로 탈퇴·보상 제외를 확정하지 않습니다."));
		auto NoteFont = ScopeNote->GetFont(); NoteFont.Size = 12; ScopeNote->SetFont(NoteFont);
		Content->AddChildToVerticalBox(ScopeNote);
		RefreshMembers();
	}
	return Super::RebuildWidget();
}

void URPGDungeonPartyCombatWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bPresentationConstructed = true;
	RefreshMembers();
	OnPartyPresentationUpdated();
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void URPGDungeonPartyCombatWidget::NativeDestruct()
{
	bPresentationConstructed = false;
	Members.Reset(); Notices.Reset(); DisplayServerWorldTime = 0.0; bLastKnownRoster = true;
	Super::NativeDestruct();
}

void URPGDungeonPartyCombatWidget::SetPartySnapshot(const FRPGDungeonPartyCombatSnapshot& Snapshot, double ServerWorldTime)
{
	Members = Snapshot.Members;
	Notices = Snapshot.Notices;
	DisplayServerWorldTime = ServerWorldTime;
	bLastKnownRoster = !Snapshot.bHasMembershipSnapshot || Snapshot.bRosterStale || Snapshot.bHasUnconfirmedMembers;
	RefreshMembers();
	if (bPresentationConstructed)
	{
		OnPartyPresentationUpdated();
		SetIsFocusable(false);
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

FText URPGDungeonPartyCombatWidget::GetPartyHeaderText() const
{
	return FText::Format(bLastKnownRoster
		? NSLOCTEXT("RPGPartyCombat", "CachedRosterHeader", "파티 전투 정보 {0}명 · 마지막 확인 포함")
		: NSLOCTEXT("RPGPartyCombat", "MembershipRosterHeader", "파티 전투 정보 {0}명 · 서버 조회 기준"), FText::AsNumber(Members.Num()));
}

TArray<FRPGDungeonPartyNotice> URPGDungeonPartyCombatWidget::GetActivePartyNotices() const
{
	TArray<FRPGDungeonPartyNotice> Active;
	if (Members.IsEmpty()) return Active;
	for (int32 Index = Notices.Num() - 1; Index >= 0 && Active.Num() < FRPGDungeonPartyCombatPresentation::MaxNotices; --Index)
	{
		if (FRPGDungeonPartyCombatPresentation::IsNoticeVisible(Notices[Index], DisplayServerWorldTime)) Active.Add(Notices[Index]);
	}
	return Active;
}

FText URPGDungeonPartyCombatWidget::GetPartyMemberText(const FRPGDungeonPartyCombatMember& Member) const
{
	return FRPGDungeonPartyCombatPresentation::MemberText(Member, DisplayServerWorldTime);
}

FText URPGDungeonPartyCombatWidget::GetPartyNoticeText(const FRPGDungeonPartyNotice& Notice) const
{
	return !Members.IsEmpty() && FRPGDungeonPartyCombatPresentation::IsNoticeVisible(Notice, DisplayServerWorldTime)
		? FRPGDungeonPartyCombatPresentation::NoticeText(Notice) : FText::GetEmpty();
}

float URPGDungeonPartyCombatWidget::GetPartyHealthFraction(const FRPGDungeonPartyCombatMember& Member) const
{
	return FRPGDungeonPartyCombatPresentation::HealthFraction(Member);
}

void URPGDungeonPartyCombatWidget::RefreshMembers()
{
	if (!Rows || !Header) return;
	Header->SetText(GetPartyHeaderText());
	if (MemberLabels.Num() != Members.Num())
	{
		Rows->ClearChildren();
		MemberLabels.Reset(); HealthBars.Reset();
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
			Label->SetAutoWrapText(true);
			auto Font = Label->GetFont(); Font.Size = 14; Label->SetFont(Font);
			Rows->AddChildToVerticalBox(Label)->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
			auto* Height = WidgetTree->ConstructWidget<USizeBox>(); Height->SetHeightOverride(8.f);
			auto* Bar = WidgetTree->ConstructWidget<UProgressBar>(); Height->SetContent(Bar);
			Rows->AddChildToVerticalBox(Height)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
			MemberLabels.Add(Label); HealthBars.Add(Bar);
		}
	}
	for (int32 Index = 0; Index < Members.Num(); ++Index)
	{
		const auto& Member = Members[Index];
		MemberLabels[Index]->SetText(GetPartyMemberText(Member));
		const bool bIncapacitated = Member.State == ERPGDungeonPartyCombatState::Incapacitated;
		const bool bReconnecting = Member.Presence == ERPGDungeonPartyPresence::ReconnectGrace;
		MemberLabels[Index]->SetColorAndOpacity(FSlateColor(bReconnecting ? FLinearColor(1.f, .84f, .48f)
			: bIncapacitated ? FLinearColor(1.f, .63f, .56f) : FLinearColor::White));
		HealthBars[Index]->SetPercent(GetPartyHealthFraction(Member));
		HealthBars[Index]->SetFillColorAndOpacity(bIncapacitated ? FLinearColor(.65f, .15f, .12f) : FLinearColor(.13f, .65f, .36f));
		// Collapse the height wrapper too; unknown health is not an empty/dead bar.
		HealthBars[Index]->GetParent()->SetVisibility(Member.bHasHealth && !bReconnecting ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (NoticeFeed)
	{
		TArray<FText> Lines;
		for (const auto& Notice : GetActivePartyNotices())
		{
			const FText Line = GetPartyNoticeText(Notice);
			if (!Line.IsEmpty()) Lines.Add(Line);
		}
		NoticeFeed->SetText(FText::Join(FText::FromString(TEXT("\n")), Lines));
		NoticeFeed->SetVisibility(Lines.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}
