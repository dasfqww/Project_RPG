#include "Manager/RPGManagerContext.h"

#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

UWorld* RPGManagerContext::ResolveWorld(const UObject* Context)
{
    if (!IsInGameThread() || !IsValid(Context)) { return nullptr; }
    UWorld* World = GEngine
        ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull)
        : Context->GetWorld();
    return IsValid(World) && !World->bIsTearingDown ? World : nullptr;
}

UGameInstance* RPGManagerContext::ResolveGameInstance(const UObject* Context)
{
    if (!IsInGameThread()) { return nullptr; }
    if (Context)
    {
        if (!IsValid(Context)) { return nullptr; }
        if (const UGameInstance* Instance = Cast<UGameInstance>(Context))
        {
            return const_cast<UGameInstance*>(Instance);
        }
        if (const ULocalPlayer* Player = Cast<ULocalPlayer>(Context))
        {
            return Player->GetGameInstance();
        }
        if (const UUserWidget* Widget = Cast<UUserWidget>(Context))
        {
            return Widget->GetGameInstance();
        }
        UWorld* World = ResolveWorld(Context);
        return World ? World->GetGameInstance() : nullptr;
    }

    // Preserve legacy Get() callers. Explicit contexts are required for async work
    // and for selecting another player or world in the same editor process.
    UWorld* CurrentWorld = GWorld;
    if (IsValid(CurrentWorld) && CurrentWorld->IsGameWorld() && !CurrentWorld->bIsTearingDown)
    {
        if (UGameInstance* Instance = CurrentWorld->GetGameInstance()) { return Instance; }
    }
    if (GEngine && GEngine->GameViewport)
    {
        UWorld* ViewportWorld = GEngine->GameViewport->GetWorld();
        if (IsValid(ViewportWorld) && !ViewportWorld->bIsTearingDown)
        {
            if (UGameInstance* Instance = ViewportWorld->GetGameInstance()) { return Instance; }
        }
    }

    UGameInstance* Candidate = nullptr;
    if (GEngine)
    {
        for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
        {
            UWorld* World = WorldContext.World();
            if (!IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown) { continue; }
            UGameInstance* Instance = World->GetGameInstance();
            if (!Instance) { continue; }
            if (Candidate && Candidate != Instance) { return nullptr; }
            Candidate = Instance;
        }
    }
    return Candidate;
}
