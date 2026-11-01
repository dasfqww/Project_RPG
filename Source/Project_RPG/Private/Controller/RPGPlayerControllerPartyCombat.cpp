#include "Controller/RPGPlayerController.h"

#include "Component/RPGDungeonPartyCombatComponent.h"
#include "HAL/PlatformTime.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "UI/Content/RPGDungeonPartyCombatWidget.h"

void ARPGPlayerController::UpdateDungeonPartyCombatPresentation()
{
	if (!IsLocalController() || !GetWorld()) return;
	const double Now = FPlatformTime::Seconds();
	if (Now < NextPartyCombatRefreshTime) return;
	NextPartyCombatRefreshTime = Now + .25;
	const auto* Party = FindComponentByClass<URPGDungeonPartyCombatComponent>();
	const auto Snapshot = Party ? Party->GetPartyCombatSnapshot() : FRPGDungeonPartyCombatSnapshot();
	if (Snapshot.Members.IsEmpty())
	{
		if (DungeonPartyCombatWidget) DungeonPartyCombatWidget->RemoveFromParent();
		DungeonPartyCombatWidget = nullptr;
		return;
	}
	const auto* GameState = GetWorld()->GetGameState();
	const double ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : 0.0;
	if (!DungeonPartyCombatWidget)
	{
		UClass* WidgetClass = DungeonPartyCombatWidgetClass.Get();
		if (!WidgetClass || WidgetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			WidgetClass = URPGDungeonPartyCombatWidget::StaticClass();
		DungeonPartyCombatWidget = CreateWidget<URPGDungeonPartyCombatWidget>(this, WidgetClass);
		if (!DungeonPartyCombatWidget && WidgetClass != URPGDungeonPartyCombatWidget::StaticClass())
			DungeonPartyCombatWidget = CreateWidget<URPGDungeonPartyCombatWidget>(this, URPGDungeonPartyCombatWidget::StaticClass());
		if (!DungeonPartyCombatWidget) return;
		// Both native and designer-owned layouts see the current data during their
		// first Construct, rather than one empty frame before the next refresh.
		DungeonPartyCombatWidget->SetPartySnapshot(Snapshot, ServerTime);
		if (!DungeonPartyCombatWidget->AddToPlayerScreen(5)) { DungeonPartyCombatWidget = nullptr; return; }
		return;
	}
	DungeonPartyCombatWidget->SetPartySnapshot(Snapshot, ServerTime);
}
