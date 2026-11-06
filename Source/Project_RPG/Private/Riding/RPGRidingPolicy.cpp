#include "Riding/RPGRidingPolicy.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool FRPGRidingPolicyValues::Validate(TArray<FText>& Errors) const
{
    const int32 Before = Errors.Num();
    const auto Fail = [&Errors](const TCHAR* Message) { Errors.Add(FText::FromString(Message)); };
    if (ProtocolVersion != 1 || CatalogVersion < 1) Fail(TEXT("Riding requires protocol 1 and a positive catalog version."));
    bool bHashValid = GameplayHash.Len() == 64;
    for (TCHAR C : GameplayHash)
        bHashValid &= (C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('a') && C <= TEXT('f'));
    if (!bHashValid) Fail(TEXT("GameplayHash must be a canonical lowercase SHA-256 manifest hash."));
    const auto InRange = [](float Value, float Min, float Max) { return FMath::IsFinite(Value) && Value >= Min && Value <= Max; };
    if (!InRange(RideSpeed, 1, 5000) || !InRange(Acceleration, 1, 20000) || !InRange(Braking, 1, 20000)
        || !InRange(YawRate, 1, 1080)) Fail(TEXT("Riding locomotion values exceed finite v1 authoring bounds."));
    if (!InRange(DashDistance, 1, 5000) || !InRange(DashDuration, 0.05f, 5)
        || !InRange(DashCooldown, 0.05f, 10) || DashDuration > DashCooldown)
        Fail(TEXT("Dash needs finite distance/duration and cooldown <= the 10 second admission protection."));
    if (!InRange(CombatLock, 0.05f, 60)) Fail(TEXT("Combat lock must be finite and in (0, 60] seconds."));
    if (!InRange(EmoteDuration, 0.05f, 3) || !InRange(EmoteCooldown, 0.05f, 2))
        Fail(TEXT("Emote duration/cooldown exceed the v1 3/2 second bounds."));
    return Errors.Num() == Before;
}

bool URPGRidingPolicy::TryBuildRuntimeValues(FRPGRidingPolicyValues& Out, TArray<FText>& Errors) const
{
    Out = FRPGRidingPolicyValues{};
    if (!Values.Validate(Errors)) return false;
    Out = Values;
    return true;
}

#if WITH_EDITOR
EDataValidationResult URPGRidingPolicy::IsDataValid(FDataValidationContext& Context) const
{
    const auto Parent = Super::IsDataValid(Context);
    TArray<FText> Errors;
    const bool bValid = Values.Validate(Errors);
    for (const auto& Error : Errors) Context.AddError(Error);
    return bValid && Parent != EDataValidationResult::Invalid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
