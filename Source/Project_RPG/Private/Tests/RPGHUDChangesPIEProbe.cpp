#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Component/UI/RPGInteractionProgressComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UI/HUD/RPGHUD.h"
#include "UI/Notice/RPGPlayerNoticeWidget.h"
#include "UI/Progress/RPGActionProgressWidget.h"
#include "UObject/UObjectIterator.h"

namespace RPGHUDChangesPIE
{
struct FProbe
{
    FString Directory;
    bool bChanged = false;
    int32 ClientPhase = 0;
    bool bNotice = false, bHolding = false, bCasting = false, bInteraction = false, bStaleEndRejected = false;
    FRPGActionProgressHandle Held, Casted;
    bool Tick()
    {
        if (!FPaths::FileExists(Directory / TEXT("hud-changes-initial.ready"))) return true;
        for (const auto& Context : GEngine->GetWorldContexts())
        {
            UWorld* World = Context.World();
            if (!World || World->bIsTearingDown || !World->IsGameWorld() || !World->GetMapName().Contains(TEXT("L_ProgressionTraining"))) continue;
            auto* PC = World->GetFirstPlayerController();
            APawn* Pawn = PC ? PC->GetPawn() : nullptr;
            auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, true);
            if (!Pawn || !ASC || !ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute())) continue;
            if (Pawn->HasAuthority())
            {
                if (bChanged)
                {
                    if (!FPaths::FileExists(Directory / TEXT("hud-changes-respawn.ready"))) return true;
                    const FString OldName = Pawn->GetName();
                    FActorSpawnParameters Parameters;
                    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                    auto* Replacement = World->SpawnActor<APawn>(Pawn->GetClass(), Pawn->GetActorTransform(), Parameters);
                    if (!Replacement) return true;
                    PC->Possess(Replacement);
                    const bool Passed = PC->GetPawn() == Replacement;
                    if (Passed)
                    {
                        // Keep the old replicated object valid while already queued
                        // movement RPCs drain. The owned test world destroys it on
                        // shutdown; this scenario verifies possession/UI lifetime.
                        Pawn->SetActorHiddenInGame(true);
                        Pawn->SetActorEnableCollision(false);
                    }
                    FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"passed\":%s,\"authority\":true,\"oldPawn\":\"%s\",\"newPawn\":\"%s\"}"),
                        Passed ? TEXT("true") : TEXT("false"), *OldName, *Replacement->GetName()), *(Directory / TEXT("HUDRespawnServer.json")));
                    UE_LOG(LogTemp, Display, TEXT("RPG_HUD_CHANGES RESPAWN Passed=%d"), Passed);
                    return false;
                }
                const float Health = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
                const float Mana = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentManaAttribute());
                if (!FMath::IsNearlyEqual(Health, 200.f) || !FMath::IsNearlyEqual(Mana, 100.f)) return true;
                auto* Effect = NewObject<UGameplayEffect>();
                Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
                for (const auto& Pair : {TPair<FGameplayAttribute, float>(URPGAttributeSet::GetCurrentHealthAttribute(), -25.f),
                                        TPair<FGameplayAttribute, float>(URPGAttributeSet::GetCurrentManaAttribute(), -10.f)})
                {
                    FGameplayModifierInfo Modifier;
                    Modifier.Attribute = Pair.Key; Modifier.ModifierOp = EGameplayModOp::Additive;
                    Modifier.ModifierMagnitude = FScalableFloat(Pair.Value);
                    Effect->Modifiers.Add(Modifier);
                }
                ASC->ApplyGameplayEffectToSelf(Effect, 1.f, ASC->MakeEffectContext());
                const float AfterHealth = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
                const float AfterMana = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentManaAttribute());
                const bool Passed = FMath::IsNearlyEqual(AfterHealth, 175.f) && FMath::IsNearlyEqual(AfterMana, 90.f);
                FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"passed\":%s,\"authority\":true,\"beforeHealth\":%g,\"beforeMana\":%g,\"afterHealth\":%g,\"afterMana\":%g,\"gameplayEffect\":true}"),
                    Passed ? TEXT("true") : TEXT("false"), Health, Mana, AfterHealth, AfterMana), *(Directory / TEXT("HUDChangesServer.json")));
                UE_LOG(LogTemp, Display, TEXT("RPG_HUD_CHANGES SERVER Passed=%d Health=%g Mana=%g"), Passed, AfterHealth, AfterMana);
                bChanged = true;
                return true;
            }
            if (!PC->IsLocalController()) continue;
            auto* HUD = Cast<ARPGHUD>(PC->GetHUD());
            auto* Root = HUD ? HUD->GetPlayerHUDWidget() : nullptr;
            auto* Notice = Root ? Cast<URPGPlayerNoticeWidget>(Root->GetWidgetFromName(TEXT("PlayerNotice"))) : nullptr;
            auto* UI = Pawn->FindComponentByClass<UPlayerUIComponent>();
            if (!Notice || !UI) continue;
            URPGActionProgressWidget* View = nullptr;
            for (TObjectIterator<URPGActionProgressWidget> It; It; ++It)
                if (It->GetWorld() == World && It->GetOwningPlayer() == PC && It->IsInViewport())
                { if (View) return true; View = *It; }
            auto* Producer = PC->FindComponentByClass<URPGInteractionProgressComponent>();
            if (!View || !Producer) continue;
            const auto Matches = [View](ERPGActionProgressKind Kind, const TCHAR* Label, float MinimumFraction)
            {
                const auto Snapshot = View->GetDisplayedProgress();
                auto* Bar = Cast<UProgressBar>(View->GetWidgetFromName(TEXT("ProgressBar")));
                auto* Text = Cast<UTextBlock>(View->GetWidgetFromName(TEXT("ActionLabel")));
                return Snapshot.bActive && Snapshot.Data.Kind == Kind && Snapshot.Fraction >= MinimumFraction && Snapshot.Fraction < 1.f
                    && View->IsVisible() && Bar && Text && FMath::IsNearlyEqual(Bar->GetPercent(), Snapshot.Fraction)
                    && Text->GetText().ToString() == Label;
            };
            FRPGActionProgressData Data;
            Data.bHasDuration = true; Data.DurationSeconds = 2.f; Data.ElapsedSeconds = .5f;
            Data.TimeDisplay = ERPGActionProgressTimeDisplay::Remaining;
            if (ClientPhase == 0)
            {
                const FString Message = TEXT("HUD notice verified");
                UI->OnNoticeTextChanged.Broadcast(FText::FromString(Message));
                bNotice = Notice->GetDisplayedNotice().ToString() == Message;
                Data.Kind = ERPGActionProgressKind::Holding; Data.Label = FText::FromString(TEXT("Hold validation"));
                Held = UI->BeginActionProgress(Pawn, Data);
                ++ClientPhase; return true;
            }
            if (ClientPhase == 1)
            {
                bHolding = Matches(ERPGActionProgressKind::Holding, TEXT("Hold validation"), .25f);
                Data.Kind = ERPGActionProgressKind::Casting; Data.Label = FText::FromString(TEXT("Cast validation"));
                Casted = UI->BeginActionProgress(Pawn, Data);
                bStaleEndRejected = !UI->EndActionProgress(Held, ERPGActionProgressEndReason::Cancelled);
                ++ClientPhase; return true;
            }
            if (ClientPhase == 2)
            {
                bCasting = Matches(ERPGActionProgressKind::Casting, TEXT("Cast validation"), .25f);
                UI->EndActionProgress(Casted, ERPGActionProgressEndReason::Completed);
                Producer->BeginInteraction(Pawn, Pawn, FText::FromString(TEXT("Interaction validation")), 2.f);
                Producer->UpdateInteraction(.5f);
                ++ClientPhase; return true;
            }
            bInteraction = Matches(ERPGActionProgressKind::Interaction, TEXT("Interaction validation"), .25f);
            Producer->EndInteraction(ERPGActionProgressEndReason::Completed);
            const bool bHidden = !View->GetDisplayedProgress().bActive && View->GetVisibility() == ESlateVisibility::Collapsed;
            const bool Passed = bNotice && bHolding && bCasting && bInteraction && bStaleEndRejected && bHidden;
            FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"passed\":%s,\"productionEvent\":true,\"notice\":\"HUD notice verified\",\"holding\":%s,\"casting\":%s,\"interactionProducer\":%s,\"staleEndRejected\":%s,\"finalProgressHidden\":%s,\"sourceApiStimulus\":true,\"actualSkillActivation\":false}"),
                Passed ? TEXT("true") : TEXT("false"), bHolding ? TEXT("true") : TEXT("false"), bCasting ? TEXT("true") : TEXT("false"),
                bInteraction ? TEXT("true") : TEXT("false"), bStaleEndRejected ? TEXT("true") : TEXT("false"), bHidden ? TEXT("true") : TEXT("false")), *(Directory / TEXT("HUDChangesClient.json")));
            UE_LOG(LogTemp, Display, TEXT("RPG_HUD_CHANGES CLIENT NoticePassed=%d"), Passed);
            return false;
        }
        return true;
    }
};

static const FDelegateHandle Registration = FCoreDelegates::GetOnPostEngineInit().AddLambda([]
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("RPGHUDChangesPIE"))
        || !FParse::Param(FCommandLine::Get(), TEXT("RPGTrainingPlaytest"))
        || !FParse::Param(FCommandLine::Get(), TEXT("nosteam"))) return;
    auto Probe = MakeShared<FProbe>();
    if (!FParse::Value(FCommandLine::Get(), TEXT("RPGTrainingGates="), Probe->Directory)) return;
    FString Normalized = Probe->Directory; FPaths::NormalizeDirectoryName(Normalized);
    FGuid RunId;
    if (FPaths::IsRelative(Normalized) || !Normalized.Contains(TEXT("/Saved/Tests/TrainingPlaytest/"))
        || !FGuid::ParseExact(FPaths::GetCleanFilename(Normalized), EGuidFormats::Digits, RunId)) return;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .05f);
});
}
#endif
