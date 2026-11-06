#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Online/RPGServiceStatusSubsystem.h"
#include "Engine/GameInstance.h"
#include "Manager/HttpWebManager.h"
#include "Components/TextBlock.h"
#include "HAL/PlatformTime.h"
#include "RPGOnlineLobbyLayout.h"
#include "Components/ScrollBox.h"

void URPGOnlineLobbyWidget::BuildServiceStatusContent(UVerticalBox& Header)
{
	auto* Bounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OnlineLobbyServiceBounds"));
	Bounds->SetMaxDesiredHeight(180.f);
	Header.AddChildToVerticalBox(Bounds);
	ServiceStatusContainer = Bounds;
	auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("OnlineLobbyServiceScroll"));
	Bounds->SetContent(Scroll);
	auto* Rows = WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Rows);
	ServiceStatusText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("OnlineLobbyServiceStatus"), FText::GetEmpty());
}

void URPGOnlineLobbyWidget::BindServiceStatus()
{
	ServiceStatus = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGServiceStatusSubsystem>() : nullptr;
	if (ServiceStatus) { ServiceStatus->OnChanged.RemoveAll(this); ServiceStatus->OnChanged.AddUObject(this, &ThisClass::HandleServiceStatusChanged); }
	if (Web) { Web->OnBackendServiceDraining.RemoveAll(this); Web->OnBackendServiceDraining.AddUObject(this, &ThisClass::HandleServiceDraining); }
	PollServiceStatus();
}
void URPGOnlineLobbyWidget::UnbindServiceStatus()
{
	if (ServiceStatus) ServiceStatus->OnChanged.RemoveAll(this);
	if (Web) Web->OnBackendServiceDraining.RemoveAll(this);
	ServiceStatus = nullptr;
}
void URPGOnlineLobbyWidget::PollServiceStatus() { if (ServiceStatus && Web) ServiceStatus->Refresh(Web->GetApiUrl()); }
bool URPGOnlineLobbyWidget::IsServiceAdmissionBlocked(FName Name) const
{
	return ServiceStatus && Web && ServiceStatus->MatchesBackend(Web->GetApiUrl())
		&& ServiceStatus->GetState().IsDraining() && FRPGServiceStatusState::IsNewAdmissionAction(Name);
}
void URPGOnlineLobbyWidget::HandleServiceDraining(const FString& ApiUrl)
{
	if (ServiceStatus && Web && Web->GetApiUrl() == ApiUrl)
	{ ServiceStatus->ObserveDraining(ApiUrl); ServiceStatus->Refresh(ApiUrl, true); }
}
void URPGOnlineLobbyWidget::HandleServiceStatusChanged()
{
	if (bTutorialEntryActive && IsServiceAdmissionBlocked(TEXT("StartTutorialButton"))) StopTutorialEntry();
	RefreshPresentation();
}
void URPGOnlineLobbyWidget::RefreshServicePresentation()
{
	if (!ServiceStatusText) return;
	const FText Text = ServiceStatus && Web && ServiceStatus->MatchesBackend(Web->GetApiUrl())
		? ServiceStatus->GetState().Text(FPlatformTime::Seconds()) : FText::GetEmpty();
	// UTextBlock, never rich text/markup. Keep separate from transient request errors.
	ServiceStatusText->SetText(Text);
	ServiceStatusText->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (ServiceStatusContainer) ServiceStatusContainer->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}
