#include "UI/Option/RPGInputOptionsRuntime.h"

#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

FName IRPGInputOptionsRuntime::GetFeatureName()
{
	static const FName Name(TEXT("RPGInputOptionsRuntime"));
	return Name;
}

FOnRPGInputOptionMappingsChanged& IRPGInputOptionsRuntime::OnMappingsChanged()
{
	static FOnRPGInputOptionMappingsChanged Changed;
	return Changed;
}

void IRPGInputOptionsRuntime::NotifyMappingsChanged(ULocalPlayer* Player, UWorld* World)
{
	if (!IsInGameThread() || !IsValid(Player) || Player->IsUnreachable()
		|| Player->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || !IsValid(World) || World->IsUnreachable()
		|| World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || World->bIsTearingDown) { return; }
	TStrongObjectPtr<ULocalPlayer> KeepPlayer(Player);
	TStrongObjectPtr<UWorld> KeepWorld(World);
	OnMappingsChanged().Broadcast(Player, World);
}

IRPGInputOptionsRuntime* IRPGInputOptionsRuntime::Get()
{
	check(IsInGameThread());
	IModularFeatures& Features = IModularFeatures::Get();
	if (!Features.IsModularFeatureAvailable(GetFeatureName()))
	{
		FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("RPGOptionsUIRuntime"));
	}
	return Features.IsModularFeatureAvailable(GetFeatureName())
		? &Features.GetModularFeature<IRPGInputOptionsRuntime>(GetFeatureName()) : nullptr;
}
