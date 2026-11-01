#include "Input/RPGAdditionalInputProvider.h"
#include "UI/Option/RPGInputOptionsRuntime.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

IRPGAdditionalInputProvider::IRPGAdditionalInputProvider() = default;

FName IRPGAdditionalInputProvider::GetFeatureName()
{
	return FName(TEXT("RPGAdditionalInputProvider"));
}

IRPGAdditionalInputRestorePlan::IRPGAdditionalInputRestorePlan() = default;
IRPGAdditionalInputRestorePlan::~IRPGAdditionalInputRestorePlan() = default;

FOnRPGAdditionalInputOptionsChanged& IRPGAdditionalInputProvider::OnInputOptionsChanged()
{
	static FOnRPGAdditionalInputOptionsChanged Changed;
	return Changed;
}

void IRPGAdditionalInputProvider::NotifyInputOptionsChanged(ULocalPlayer* Player, UWorld* World)
{
	if (!IsInGameThread() || !IsValid(Player) || Player->IsUnreachable()
		|| Player->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || !IsValid(World) || World->IsUnreachable()
		|| World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || World->bIsTearingDown) { return; }
	TStrongObjectPtr<ULocalPlayer> KeepPlayer(Player);
	TStrongObjectPtr<UWorld> KeepWorld(World);
	OnInputOptionsChanged().Broadcast(Player, World);
	IRPGInputOptionsRuntime::NotifyMappingsChanged(Player, World);
}
