#include "Riding/RPGRidingDashIntent.h"

bool FRPGRidingDashIntent::IsValid() const
{
    return GrantId.IsValid() && Expected.RiderEpoch.IsValid() && Expected.StateRevision > 0
        && Expected.ActionSequence < MAX_uint32 - 1 && !Direction.ContainsNaN()
        && Direction.Z == 0 && Direction.IsNormalized();
}
bool FRPGRidingDashIntent::NetSerialize(FArchive& Ar, UPackageMap*, bool& Success)
{
    FRPGRidingDashIntent Value = Ar.IsSaving() ? *this : FRPGRidingDashIntent{};
    Ar << Value.GrantId << Value.Expected.RiderEpoch << Value.Expected.StateRevision << Value.Expected.ActionSequence << Value.Direction;
    Success = !Ar.IsError() && Value.IsValid();
    if (!Success) Ar.SetError();
    if (Ar.IsLoading()) *this = Success ? Value : FRPGRidingDashIntent{};
    return Success;
}
const FRPGRidingDashIntent* FRPGRidingDashIntent::Read(const FGameplayAbilityTargetDataHandle& Data)
{
    if (Data.Num() != 1 || !Data.Get(0) || Data.Get(0)->GetScriptStruct() != StaticStruct()) return nullptr;
    const auto* Value = static_cast<const FRPGRidingDashIntent*>(Data.Get(0));
    return Value->IsValid() ? Value : nullptr;
}
