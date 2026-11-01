#include "Tests/RPGDungeonFlowE2EProbe.h"

#if !UE_BUILD_SHIPPING
#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Character/RPGPlayer.h"
#include "CommonGameViewportClient.h"
#include "Controller/RPGPlayerController.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Equipment/RPGPlayerWeaponLoadoutComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "InputAction.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Player/RPGPlayerState.h"
#include "RPGGameplayTags.h"
#include "UI/Content/RPGDungeonPartyCombatWidget.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace
{
	bool PartyInputPointer(UGameInstance& Instance, const FVector2D Point, bool bDown)
	{
		auto* Viewport = Cast<UCommonGameViewportClient>(Instance.GetGameViewportClient());
		const auto Player = Instance.GetFirstGamePlayer();
		const auto User = Player ? Player->GetSlateUser() : nullptr;
		const auto Window = Viewport ? Viewport->GetWindow() : nullptr;
		if (!Viewport || !Window || !User || !Viewport->GetGameViewportWidget()) return false;
		const FWidgetPath Path(Window->GetHittestGrid().GetBubblePath(Point, 0.f, false, User->GetUserIndex()));
		if (!Path.IsValid() || !Path.ContainsWidget(Viewport->GetGameViewportWidget().Get())) return false;
		const TSet<FKey> Buttons = bDown ? TSet<FKey>{EKeys::LeftMouseButton} : TSet<FKey>{};
		const FPointerEvent Event(User->GetUserIndex(), FSlateApplication::CursorPointerIndex,
			Point, Point, Buttons, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		if (bDown) FSlateApplication::Get().RoutePointerDownEvent(Path, Event);
		else FSlateApplication::Get().RoutePointerUpEvent(Path, Event);
		return true;
	}
	void PartyInputKey(const ULocalPlayer& Player, FName KeyName, bool bDown)
	{
		const auto User = Player.GetSlateUser();
		if (!User) return;
		const FKeyEvent Event(FKey(KeyName), FModifierKeysState(), User->GetUserIndex(), false, 0, 0);
		if (bDown) FSlateApplication::Get().ProcessKeyDownEvent(Event);
		else FSlateApplication::Get().ProcessKeyUpEvent(Event);
	}
}
#endif

void URPGDungeonFlowE2EClient::StopPartyHudInput()
{
#if !UE_BUILD_SHIPPING
	if (!bPartyHudInput || !FSlateApplication::IsInitialized()) return;
	if (auto* Player = GetGameInstance()->GetFirstGamePlayer())
		if (bPartyHudMoveDown) PartyInputKey(*Player, PartyHudMoveKeyName, false);
	if (bPartyHudAttackDown) PartyInputPointer(*GetGameInstance(), PartyHudInputPoint, false);
	bPartyHudMoveDown = false; bPartyHudAttackDown = false;
	if (auto* Widget = PartyHudInputWidget.Get()) Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
#endif
}

bool URPGDungeonFlowE2EClient::TickPartyHudInput(UWorld* World, APlayerController* PC)
{
#if !UE_BUILD_SHIPPING
	if (PartyHudInputPhase == 6) return true;
	if (PartyHudInputGate.IsEmpty() || !World || World->GetNetMode() != NM_Client || !PC
		|| !FSlateApplication::IsInitialized()) return Fail(TEXT("PartyInputUnsafeScope"));
	if (!IFileManager::Get().FileExists(*PartyHudInputGate)) return true;
	auto* Player = Cast<ARPGPlayer>(PC->GetPawn());
	auto* State = PC->GetPlayerState<ARPGPlayerState>();
	auto* Local = PC->GetLocalPlayer();
	auto* Viewport = Cast<UCommonGameViewportClient>(GetGameInstance()->GetGameViewportClient());
	const auto User = Local ? Local->GetSlateUser() : nullptr;
	auto* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
	const auto* Loadout = Player ? Player->FindComponentByClass<URPGPlayerWeaponLoadoutComponent>() : nullptr;
	auto* Weapon = Player ? Player->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon() : nullptr;
	if (!Player || !State || !Local || !User || !Viewport || !ASC || !Loadout || !Loadout->IsNativeManaged()
		|| !Weapon || !State->HasAuthenticatedCharacter() || State->GetBackendCharacterId() != CharacterId
		|| State->GetBackendDungeonSessionId() != SessionId) return Fail(TEXT("PartyInputPeerChanged"));
	auto* Spec = ASC->GetActivatableAbilities().FindByPredicate([Weapon](const FGameplayAbilitySpec& Candidate)
	{
		return Candidate.SourceObject.Get() == Weapon
			&& Candidate.GetDynamicSpecSourceTags().HasTagExact(RPGGameplayTags::InputTag_LightAttack_GreatSword);
	});
	if (!Spec) return Fail(TEXT("PartyInputAttackMissing"));
	const double Now = FPlatformTime::Seconds();
	if (PartyHudInputPhase == 0)
	{
		const auto* Controller = Cast<ARPGPlayerController>(PC);
		const auto* Config = Controller ? Controller->GetInputConfig() : nullptr;
		const auto* Move = Config ? Config->FindNativeInputActionByTag(RPGGameplayTags::InputTag_Move) : nullptr;
		const auto* Enhanced = Cast<UEnhancedPlayerInput>(PC->PlayerInput);
		const auto* InputSubsystem = Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		FKey MoveKey;
		if (Enhanced && Move)
			for (const auto& Mapping : Enhanced->GetEnhancedActionMappingsView())
				if (Mapping.Action == Move && Mapping.Key.IsValid() && !Mapping.Key.IsGamepadKey()
					&& !Mapping.Key.IsMouseButton() && !Mapping.Key.IsAxis1D() && !Mapping.Key.IsAxis2D()
					&& !Mapping.Key.IsAxis3D() && !Mapping.Key.IsModifierKey())
				{
					if (!MoveKey.IsValid() || Mapping.Key == EKeys::W) MoveKey = Mapping.Key;
				}
		UE_LOG(LogTemp, Display, TEXT("PARTY_HUD_INPUT MAPPINGS Configured=%d PrimaryApplied=%d ActiveMappings=%d MoveAction=%s SelectedKey=%s"),
			Config != nullptr, InputSubsystem && Config && Config->DefaultMappingContext
				&& InputSubsystem->HasMappingContext(Config->DefaultMappingContext),
			Enhanced ? Enhanced->GetEnhancedActionMappingsView().Num() : 0, Move ? *Move->GetName() : TEXT("none"), *MoveKey.GetFName().ToString());
		if (!MoveKey.IsValid())
		{
			FString Active, Primary;
			if (Enhanced) for (const auto& Mapping : Enhanced->GetEnhancedActionMappingsView())
				Active += Mapping.Key.GetFName().ToString() + TEXT("=") + (Mapping.Action ? Mapping.Action->GetName() : TEXT("none")) + TEXT(";");
			if (Config && Config->DefaultMappingContext) for (const auto& Mapping : Config->DefaultMappingContext->GetMappings())
				Primary += Mapping.Key.GetFName().ToString() + TEXT("=") + (Mapping.Action ? Mapping.Action->GetName() : TEXT("none")) + TEXT(";");
			UE_LOG(LogTemp, Display, TEXT("PARTY_HUD_INPUT MAPPING_DIAGNOSTIC RuntimeCopy=%d PrimaryMappings=%s ActiveMappings=%s"),
				Config && Config->GetOuter() == PC, *Primary, *Active);
		}
		if (!MoveKey.IsValid()) return Fail(TEXT("PartyInputMovementMappingMissing"));
		PartyHudMoveKeyName = MoveKey.GetFName();
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, URPGDungeonPartyCombatWidget::StaticClass(), false);
		URPGDungeonPartyCombatWidget* Found = nullptr;
		for (auto* Item : Widgets) if (Item->GetOwningPlayer() == PC && Item->IsInViewport())
		{
			if (Found) return Fail(TEXT("PartyInputDuplicateHud"));
			Found = Cast<URPGDungeonPartyCombatWidget>(Item);
		}
		if (!Found || Found->GetClass()->GetPathName() != TEXT("/Game/Blueprints/UI/Party/WBP_DungeonPartyCombat_LeftCenter.WBP_DungeonPartyCombat_LeftCenter_C")
			|| Found->GetPartyMembers().Num() != 2 || Found->GetVisibility() != ESlateVisibility::HitTestInvisible)
			return Fail(TEXT("PartyInputSavedHudMissing"));
		auto* Panel = Found->GetWidgetFromName(TEXT("PartyPanel"));
		if (!Panel || Panel->GetCachedGeometry().GetLocalSize().IsNearlyZero()) return true;
		PartyHudInputPoint = Panel->GetCachedGeometry().LocalToAbsolute(Panel->GetCachedGeometry().GetLocalSize() * .5f);
		PartyHudInputWidget = Found; PartyHudInputPawn = Player;
		// No SetInputMode, mapping-context changes, direct action injection or
		// ability activation. Only the normal game viewport receives Slate input.
		// A normal pointer click acquires viewport focus through the actual hit
		// path. Do not force focus or change the controller's input mode.
		if (!PartyInputPointer(*GetGameInstance(), PartyHudInputPoint, true)
			|| !PartyInputPointer(*GetGameInstance(), PartyHudInputPoint, false))
			return Fail(TEXT("PartyInputInitialViewportOccluded"));
		PartyHudInputFocus = User->GetFocusedWidget();
		if (!PartyHudInputFocus.IsValid() || PartyHudInputFocus.Pin() != Viewport->GetGameViewportWidget())
			return Fail(TEXT("PartyInputViewportNotFocused"));
		bPartyHudCursor = PC->ShouldShowMouseCursor();
		Found->SetVisibility(ESlateVisibility::Collapsed);
		PartyHudInputAt = Now + .5; PartyHudInputPhase = 1;
		return true;
	}
	if (PartyHudInputPawn.Get() != Player || !PartyHudInputWidget.IsValid()
		|| PartyHudInputWidget->GetPartyMembers().Num() != 2 || User->GetFocusedWidget() != PartyHudInputFocus.Pin()
		|| PC->ShouldShowMouseCursor() != bPartyHudCursor) return Fail(TEXT("PartyInputPresentationChangedFocusOrPeer"));
	if (PartyHudInputPhase == 1 && Now >= PartyHudInputAt && !Spec->IsActive())
	{
		PartyHudInputStart = Player->GetActorLocation();
		PartyInputKey(*Local, PartyHudMoveKeyName, true); bPartyHudMoveDown = true;
		PartyHudInputAt = Now + .6; PartyHudInputPhase = 2;
		return true;
	}
	if (PartyHudInputPhase == 2 && Now >= PartyHudInputAt)
	{
		FString Mappings;
		if (const auto* Enhanced = Cast<UEnhancedPlayerInput>(PC->PlayerInput))
			for (const auto& Mapping : Enhanced->GetEnhancedActionMappingsView())
				if (Mapping.Key == FKey(PartyHudMoveKeyName) && Mapping.Action) Mappings += Mapping.Action->GetName() + TEXT(";");
		UE_LOG(LogTemp, Display, TEXT("PARTY_HUD_INPUT MOVE_OBSERVATION Distance=%g KeyValue=%g IgnoreMove=%d IgnoreViewport=%d Velocity=%g Mappings=%s"),
			FVector::Dist2D(Player->GetActorLocation(), PartyHudInputStart), PC->PlayerInput ? PC->PlayerInput->GetKeyValue(FKey(PartyHudMoveKeyName)) : -1.f,
			PC->IsMoveInputIgnored(), Viewport->IgnoreInput(), Player->GetVelocity().Size2D(), *Mappings);
		PartyInputKey(*Local, PartyHudMoveKeyName, false); bPartyHudMoveDown = false;
		if (FVector::Dist2D(Player->GetActorLocation(), PartyHudInputStart) < 3.f) return Fail(TEXT("PartyInputMovementNotDelivered"));
		if (!PartyInputPointer(*GetGameInstance(), PartyHudInputPoint, true)) return Fail(TEXT("PartyInputViewportOccluded"));
		bPartyHudAttackDown = true; bPartyHudSawAttack = false;
		PartyHudInputAt = Now + .3; PartyHudInputPhase = 3;
		return true;
	}
	if (PartyHudInputPhase == 3)
	{
		bPartyHudSawAttack |= Spec->IsActive();
		if (Now < PartyHudInputAt) return true;
		if (!PartyInputPointer(*GetGameInstance(), PartyHudInputPoint, false)) return Fail(TEXT("PartyInputPointerReleaseOccluded"));
		bPartyHudAttackDown = false;
		PartyHudInputAt = Now + .7; PartyHudInputPhase = 4;
		return true;
	}
	if (PartyHudInputPhase == 4)
	{
		bPartyHudSawAttack |= Spec->IsActive();
		if (Now < PartyHudInputAt) return true;
		if (!bPartyHudSawAttack) return Fail(TEXT("PartyInputAttackNotDelivered"));
		UE_LOG(LogTemp, Display, TEXT("PARTY_HUD_INPUT Phase=%s MoveDistance=%g AbilityActivated=1 ViewportHitTest=1 FocusRetained=1 Physical=0"),
			PartyHudInputPass == 0 ? TEXT("baseline") : TEXT("visible"), FVector::Dist2D(Player->GetActorLocation(), PartyHudInputStart));
		if (PartyHudInputPass == 0)
		{
			PartyHudInputWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
			PartyHudInputPass = 1; PartyHudInputAt = Now + .5; PartyHudInputPhase = 1;
		}
		else
		{
			PartyHudInputPhase = 6;
			UE_LOG(LogTemp, Display, TEXT("PARTY_HUD_INPUT COMPLETE Baseline=1 Visible=1 SamePawn=1 SameSession=1 CursorUnchanged=1 Physical=0"));
		}
	}
	return true;
#else
	return false;
#endif
}
