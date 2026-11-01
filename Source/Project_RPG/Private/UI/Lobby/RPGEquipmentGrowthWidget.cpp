#include "UI/Lobby/RPGEquipmentGrowthWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Progression/RPGEquipmentMutationSubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyLayout.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "UI/RPGConfirmationInput.h"

#define LOCTEXT_NAMESPACE "RPGEquipmentGrowth"

void URPGEquipmentGrowthWidget::SetLobby(URPGOnlineLobbyWidget* InLobby) { Lobby=InLobby; }

TSharedRef<SWidget> URPGEquipmentGrowthWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto* Root=WidgetTree->ConstructWidget<UVerticalBox>(); WidgetTree->RootWidget=Root;
		RPGOnlineLobbyLayout::Text(*WidgetTree,*Root,TEXT("EquipmentGrowthHint"),LOCTEXT("Hint","이번 구간은 +3까지 확정 강화입니다. 장비를 바꿀 때 강화 단계를 무료로 옮길 수 있습니다."));
		StatusText=RPGOnlineLobbyLayout::Text(*WidgetTree,*Root,TEXT("EquipmentGrowthStatus"),FText::GetEmpty());
		ReloadButton=RPGOnlineLobbyLayout::Action(*WidgetTree,*Root,TEXT("ReloadEquipmentButton"),LOCTEXT("Read","장비 새로고침"));
		OperationBox=WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),TEXT("EquipmentGrowthOperation"));
		OperationBox->AddOption(LOCTEXT("UpgradeMode","한 단계 강화").ToString()); OperationBox->AddOption(LOCTEXT("TransferMode","강화 무료 이전").ToString());
		OperationBox->SetSelectedIndex(0); RPGOnlineLobbyLayout::Control(*WidgetTree,*Root,*OperationBox);
		RPGOnlineLobbyLayout::Text(*WidgetTree,*Root,TEXT("EquipmentSourceLabel"),LOCTEXT("SourceLabel","강화할 장비 · 이전할 원본"));
		SourceBox=WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),TEXT("EquipmentGrowthSource"));
		RPGOnlineLobbyLayout::Control(*WidgetTree,*Root,*SourceBox);
		RPGOnlineLobbyLayout::Text(*WidgetTree,*Root,TEXT("EquipmentTargetLabel"),LOCTEXT("TargetLabel","강화를 받을 장비 · 같은 부위의 +0 장비"));
		TargetBox=WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),TEXT("EquipmentGrowthTarget"));
		RPGOnlineLobbyLayout::Control(*WidgetTree,*Root,*TargetBox);
		PreviewText=RPGOnlineLobbyLayout::Text(*WidgetTree,*Root,TEXT("EquipmentGrowthPreview"),FText::GetEmpty());
		PrepareButton=RPGOnlineLobbyLayout::Action(*WidgetTree,*Root,TEXT("PrepareEquipmentButton"),LOCTEXT("Prepare","비용과 변경 내용 확인"));
		ConfirmButton=RPGOnlineLobbyLayout::Action(*WidgetTree,*Root,TEXT("ConfirmEquipmentButton"),LOCTEXT("Confirm","확인한 내용으로 실행"));
		CancelButton=RPGOnlineLobbyLayout::Action(*WidgetTree,*Root,TEXT("CancelEquipmentButton"),LOCTEXT("Cancel","확인 취소"));
		RetryButton=RPGOnlineLobbyLayout::Action(*WidgetTree,*Root,TEXT("RetryEquipmentButton"),LOCTEXT("Retry","이전 작업 결과 재확인"));
	}
	return Super::RebuildWidget();
}

void URPGEquipmentGrowthWidget::NativeConstruct()
{
	Super::NativeConstruct(); bAttached=true;
	if (ReloadButton) ReloadButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Reload);
	if (PrepareButton) PrepareButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Prepare);
	if (ConfirmButton) ConfirmButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Confirm);
	if (CancelButton) CancelButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Cancel);
	if (RetryButton) RetryButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Retry);
	if (OperationBox) OperationBox->OnSelectionChanged.AddUniqueDynamic(this,&ThisClass::SelectedOperation);
	if (SourceBox) SourceBox->OnSelectionChanged.AddUniqueDynamic(this,&ThisClass::SelectedSource);
	if (TargetBox) TargetBox->OnSelectionChanged.AddUniqueDynamic(this,&ThisClass::SelectedTarget);
	RefreshContext();
	if (Mutations.IsValid()) Mutations->OnEquipmentMutationsChanged.AddUniqueDynamic(this,&ThisClass::HandleChanged);
}

void URPGEquipmentGrowthWidget::NativeDestruct()
{
	bAttached=bScreenOpen=bConfirming=bOpened=false;
	if (Mutations.IsValid()) Mutations->OnEquipmentMutationsChanged.RemoveDynamic(this,&ThisClass::HandleChanged);
	if (ReloadButton) ReloadButton->OnClicked.RemoveAll(this);
	if (PrepareButton) PrepareButton->OnClicked.RemoveAll(this);
	if (ConfirmButton) ConfirmButton->OnClicked.RemoveAll(this);
	if (CancelButton) CancelButton->OnClicked.RemoveAll(this);
	if (RetryButton) RetryButton->OnClicked.RemoveAll(this);
	if (OperationBox) OperationBox->OnSelectionChanged.RemoveAll(this);
	if (SourceBox) SourceBox->OnSelectionChanged.RemoveAll(this);
	if (TargetBox) TargetBox->OnSelectionChanged.RemoveAll(this);
	Mutations.Reset(); Scope={}; Options.Reset(); Confirmation={};
	Super::NativeDestruct();
}

bool URPGEquipmentGrowthWidget::ReadScope(FRPGEquipmentMutationScope& Out) const
{
	const auto* Owner=Lobby.Get(); FString Endpoint,SteamId; FGuid Character;
	return bAttached && IsValid(Owner) && GetWorld() && !GetWorld()->bIsTearingDown
		&& Owner->GetOwningPlayer() && Owner->GetOwningPlayer()==GetOwningPlayer()
		&& Owner->Web && Owner->Flow && Owner->HasOwnedSelection()
		&& FGuid::Parse(Owner->Flow->GetSelectedCharacterId(),Character)
		&& Owner->Web->GetPlayerEquipmentContext(Endpoint,SteamId)
		&& FRPGEquipmentMutationScope::Create(Endpoint,SteamId,Character,Out);
}
bool URPGEquipmentGrowthWidget::IsCurrentContext() const
{
	FRPGEquipmentMutationScope Current;
	return ReadScope(Current) && Current==Scope && ContextWorld.Get()==GetWorld()
		&& ContextController.Get()==GetOwningPlayer() && Mutations.IsValid();
}
FRPGEquipmentMutationView URPGEquipmentGrowthWidget::View() const
{
	if (IsCurrentContext()) return Mutations->GetState(Scope.CharacterId);
	FRPGEquipmentMutationView Result; Result.State=ERPGEquipmentMutationState::Unavailable; return Result;
}
bool URPGEquipmentGrowthWidget::IsBusy() const
{
	const auto* Service=GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGEquipmentMutationSubsystem>() : nullptr;
	return Service && (Service->IsRequestInFlight() || Service->IsItemsReadInFlight());
}
bool URPGEquipmentGrowthWidget::HasUnresolvedCommand() const
{
	const auto Current=View();
	return Current.State==ERPGEquipmentMutationState::StorageBlocked
		|| (Current.RequestId.IsValid() && (Current.State==ERPGEquipmentMutationState::AwaitingServer
			|| Current.State==ERPGEquipmentMutationState::RecoveryRequired || Current.State==ERPGEquipmentMutationState::AuthenticationRequired));
}
bool URPGEquipmentGrowthWidget::CanRequest(bool bWrite,bool bRetry) const
{
	return bScreenOpen && IsCurrentContext() && !IsBusy() && Lobby->CanUseEquipmentGrowth(bWrite,bRetry);
}

void URPGEquipmentGrowthWidget::RefreshContext()
{
	if (bRefreshing) return; TGuardValue<bool> Guard(bRefreshing,true);
	auto* Service=GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGEquipmentMutationSubsystem>() : nullptr;
	if (Mutations.Get()!=Service)
	{
		if (Mutations.IsValid()) Mutations->OnEquipmentMutationsChanged.RemoveDynamic(this,&ThisClass::HandleChanged);
		Mutations=Service;
		if (bAttached && Service) Service->OnEquipmentMutationsChanged.AddUniqueDynamic(this,&ThisClass::HandleChanged);
	}
	FRPGEquipmentMutationScope Current; const bool Valid=ReadScope(Current);
	if (!Valid || !(Current==Scope) || ContextWorld.Get()!=GetWorld() || ContextController.Get()!=GetOwningPlayer())
	{
		Scope=Valid ? Current : FRPGEquipmentMutationScope(); ContextWorld=GetWorld(); ContextController=GetOwningPlayer();
		SourceId=TargetId=LastRefreshRequest=FGuid(); bOpened=bConfirming=false; Confirmation={}; LocalMessage=FText::GetEmpty();
	}
	// Restore the local intent on character entry, even while this panel is hidden.
	// This is a journal read only: entering the lobby never sends a payment/retry.
	if (Valid && Mutations.IsValid() && !bOpened) { bOpened=true; Mutations->OpenCharacter(Scope.CharacterId); }
	const auto CurrentView=View();
	RebuildChoices(CurrentView);
	if (bConfirming && !Confirmation.HasSameIntent(Preview())) { bConfirming=false; LocalMessage=LOCTEXT("Stale","장비 정보가 바뀌어 확인을 취소했습니다. 변경 내용을 다시 확인하세요."); }
	Draw();
}

void URPGEquipmentGrowthWidget::RebuildChoices(const FRPGEquipmentMutationView& Current)
{
	const auto Next=RPGEquipmentGrowthUI::Choices(Current,FDateTime::UtcNow());
	bool Same=Next.Num()==Options.Num();
	for (int32 I=0;Same && I<Next.Num();++I) Same=Next[I].ItemId==Options[I].ItemId && Next[I].Label.EqualTo(Options[I].Label);
	if (Same) return;
	TGuardValue<bool> Guard(bUpdating,true); Options=Next;
	if (!SourceBox || !TargetBox) return;
	SourceBox->ClearOptions(); TargetBox->ClearOptions();
	for (int32 I=0;I<Options.Num();++I)
	{
		const FString Label=FString::Printf(TEXT("%d. %s"),I+1,*Options[I].Label.ToString()); SourceBox->AddOption(Label); TargetBox->AddOption(Label);
	}
	const int32 SourceIndex=Options.IndexOfByPredicate([this](const auto& Item) { return Item.ItemId==SourceId; });
	const int32 TargetIndex=Options.IndexOfByPredicate([this](const auto& Item) { return Item.ItemId==TargetId; });
	if (SourceIndex!=INDEX_NONE) SourceBox->SetSelectedIndex(SourceIndex); else if (Current.ItemsLoadState==ERPGInventoryProjectionLoadState::Ready) SourceId.Invalidate();
	if (TargetIndex!=INDEX_NONE) TargetBox->SetSelectedIndex(TargetIndex); else if (Current.ItemsLoadState==ERPGInventoryProjectionLoadState::Ready) TargetId.Invalidate();
}

FRPGEquipmentGrowthPreview URPGEquipmentGrowthWidget::Preview() const
{
	const auto Current=View();
	return OperationBox && OperationBox->GetSelectedIndex()==1
		? RPGEquipmentGrowthUI::Transfer(Current,SourceId,TargetId,FDateTime::UtcNow())
		: RPGEquipmentGrowthUI::Upgrade(Current,SourceId,FDateTime::UtcNow());
}
void URPGEquipmentGrowthWidget::Draw()
{
	const auto Current=View(); const auto Next=Preview(); const bool Editable=CanRequest(true) && RPGEquipmentGrowthUI::CanStartNew(Current);
	if (StatusText) StatusText->SetText(LocalMessage.IsEmpty() ? RPGEquipmentGrowthUI::Status(Current) : LocalMessage);
	if (PreviewText) PreviewText->SetText(bConfirming ? FText::Format(LOCTEXT("ConfirmPreview","{0}\n아래 확인을 누르면 이 내용으로 실행합니다."),Confirmation.Text) : Next.Text);
	if (OperationBox) OperationBox->SetIsEnabled(Editable && !bConfirming);
	if (SourceBox) SourceBox->SetIsEnabled(Editable && !bConfirming);
	if (TargetBox)
	{
		TargetBox->SetIsEnabled(Editable && !bConfirming);
		RPGOnlineLobbyLayout::SetControlVisibility(*TargetBox,OperationBox && OperationBox->GetSelectedIndex()==1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (auto* Label=WidgetTree->FindWidget(TEXT("EquipmentTargetLabel"))) Label->SetVisibility(TargetBox->GetVisibility());
	}
	if (ReloadButton) ReloadButton->SetIsEnabled(CanRequest(false));
	if (PrepareButton) PrepareButton->SetIsEnabled(Editable && Next.bValid && !bConfirming);
	if (ConfirmButton) { ConfirmButton->SetIsEnabled(Editable && bConfirming && Confirmation.HasSameIntent(Next)); RPGOnlineLobbyLayout::SetControlVisibility(*ConfirmButton,bConfirming ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (CancelButton) RPGOnlineLobbyLayout::SetControlVisibility(*CancelButton,bConfirming ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (RetryButton) { RetryButton->SetIsEnabled(RPGEquipmentGrowthUI::CanRetry(Current) && CanRequest(true,true)); RPGOnlineLobbyLayout::SetControlVisibility(*RetryButton,HasUnresolvedCommand() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
}

void URPGEquipmentGrowthWidget::SetScreenOpen(bool bOpen)
{ bScreenOpen=bOpen; if (!bOpen) { bConfirming=false; Confirmation={}; } Draw(); }
void URPGEquipmentGrowthWidget::EnsureOpened()
{
	if (!bOpened && CanRequest(false)) { bOpened=true; Mutations->OpenCharacter(Scope.CharacterId); }
}
void URPGEquipmentGrowthWidget::OpenScreen()
{
	bScreenOpen=true; RefreshContext(); EnsureOpened();
	if (CanRequest(false) && View().ItemsLoadState==ERPGInventoryProjectionLoadState::Uninitialized) Mutations->RefreshItems(Scope.CharacterId);
	RefreshContext();
}
void URPGEquipmentGrowthWidget::Reload()
{
	RefreshContext(); if (!CanRequest(false)) return;
	bConfirming=false; LocalMessage=FText::GetEmpty(); EnsureOpened();
	if (CanRequest(false)) Mutations->RefreshItems(Scope.CharacterId);
	RefreshContext(); if (Lobby.IsValid()) Lobby->RefreshPresentation();
}
void URPGEquipmentGrowthWidget::HandleChanged()
{
	if (bHandlingChange || !bAttached) return; TGuardValue<bool> Guard(bHandlingChange,true);
	RefreshContext(); const auto Current=View();
	if (bScreenOpen && Current.bNeedsInventoryRefresh && Current.RequestId.IsValid() && Current.RequestId!=LastRefreshRequest && CanRequest(false))
	{ LastRefreshRequest=Current.RequestId; Mutations->RefreshItems(Scope.CharacterId); RefreshContext(); }
	if (Lobby.IsValid()) Lobby->RefreshPresentation();
}
void URPGEquipmentGrowthWidget::SelectedOperation(FString,ESelectInfo::Type) { if (!bUpdating) Cancel(); }
void URPGEquipmentGrowthWidget::SelectedSource(FString Option,ESelectInfo::Type)
{
	if (bUpdating || !SourceBox) return; const int32 I=SourceBox->FindOptionIndex(Option); SourceId=Options.IsValidIndex(I) ? Options[I].ItemId : FGuid(); Cancel();
}
void URPGEquipmentGrowthWidget::SelectedTarget(FString Option,ESelectInfo::Type)
{
	if (bUpdating || !TargetBox) return; const int32 I=TargetBox->FindOptionIndex(Option); TargetId=Options.IsValidIndex(I) ? Options[I].ItemId : FGuid(); Cancel();
}
void URPGEquipmentGrowthWidget::Prepare()
{
	RefreshContext(); if (!CanRequest(true)) return;
	Confirmation=Preview(); bConfirming=Confirmation.bValid; LocalMessage=FText::GetEmpty(); Draw();
	if (bConfirming && ConfirmButton && GetOwningPlayer()) ConfirmButton->SetUserFocus(GetOwningPlayer());
}
void URPGEquipmentGrowthWidget::Confirm()
{
	RefreshContext(); if (!bConfirming || !CanRequest(true) || !Confirmation.HasSameIntent(Preview())) { Cancel(); return; }
	const auto Intent=Confirmation; bConfirming=false; Confirmation={}; LocalMessage=FText::GetEmpty();
	if (Intent.Operation==ERPGEquipmentMutationOperation::Upgrade) Mutations->UpgradeEquipment(Scope.CharacterId,Intent.Source,Intent.Costs);
	else if (Intent.Operation==ERPGEquipmentMutationOperation::Transfer) Mutations->TransferEquipment(Scope.CharacterId,Intent.Source,Intent.Target);
	RefreshContext(); if (Lobby.IsValid()) Lobby->RefreshPresentation();
}
void URPGEquipmentGrowthWidget::Cancel() { bConfirming=false; Confirmation={}; LocalMessage=FText::GetEmpty(); Draw(); }
void URPGEquipmentGrowthWidget::Retry()
{
	RefreshContext(); if (!RPGEquipmentGrowthUI::CanRetry(View()) || !CanRequest(true,true)) return;
	bConfirming=false; LocalMessage=FText::GetEmpty(); Mutations->RetryPending(Scope.CharacterId);
	RefreshContext(); if (Lobby.IsValid()) Lobby->RefreshPresentation();
}
FReply URPGEquipmentGrowthWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
	const auto Reply=Super::NativeOnKeyDown(Geometry,Event);
	if (!Reply.IsEventHandled() && bConfirming && RPGConfirmationInput::IsBack(Event)) { if (!Event.IsRepeat()) Cancel(); return FReply::Handled(); }
	return Reply;
}
#undef LOCTEXT_NAMESPACE
