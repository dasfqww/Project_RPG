#include "Riding/RPGRidingDashGrant.h"

bool FRPGRidingDashGrant::IsEmpty() const
{
    return !GrantId.IsValid() && !RiderEpoch.IsValid() && !MountRevision && !AbilityHandle.IsValid()
        && !CatalogVersion && Distance == 0 && Duration == 0 && Cooldown == 0;
}
bool FRPGRidingDashGrant::IsValid() const
{
    return GrantId.IsValid() && RiderEpoch.IsValid() && MountRevision > 0 && AbilityHandle.IsValid() && CatalogVersion > 0
        && FMath::IsFinite(Distance) && Distance >= 1 && Distance <= 5000
        && FMath::IsFinite(Duration) && Duration >= .05f && Duration <= 5
        && FMath::IsFinite(Cooldown) && Cooldown >= .05f && Cooldown <= 10 && Duration <= Cooldown;
}
bool FRPGRidingDashGrant::Matches(const FRPGRidingRepState& S) const
{
    return IsValid() && RPGRiding::IsCanonical(S) && S.Mode == ERPGRidingMode::Mounted
        && RiderEpoch == S.Key.RiderEpoch && MountRevision == S.Key.StateRevision && CatalogVersion == S.Movement.Version;
}
bool FRPGRidingDashGrant::SameGrant(const FRPGRidingDashGrant& G) const
{
    return IsValid() && G.IsValid() && GrantId == G.GrantId && RiderEpoch == G.RiderEpoch && MountRevision == G.MountRevision
        && AbilityHandle == G.AbilityHandle && CatalogVersion == G.CatalogVersion && Distance == G.Distance && Duration == G.Duration && Cooldown == G.Cooldown;
}
bool FRPGRidingDashGrant::NetSerialize(FArchive& Ar, UPackageMap*, bool& Success)
{
    FRPGRidingDashGrant Value = Ar.IsSaving() ? *this : FRPGRidingDashGrant{};
    uint8 Present = Value.GrantId.IsValid(); Ar.SerializeBits(&Present, 1);
    if (Present)
        Ar << Value.GrantId << Value.RiderEpoch << Value.MountRevision << Value.AbilityHandle << Value.CatalogVersion << Value.Distance << Value.Duration << Value.Cooldown;
    Success = !Ar.IsError() && (Present ? Value.IsValid() : Value.IsEmpty());
    if (!Success) Ar.SetError();
    if (Ar.IsLoading()) *this = Success ? Value : FRPGRidingDashGrant{};
    return Success;
}
FGuid FRPGRidingDashActivation::ExecutionFor(const FRPGRidingStateKey& E) const
{
    if (!IsValid() || E.RiderEpoch != Grant.RiderEpoch || E.StateRevision != Grant.MountRevision || E.ActionSequence >= MAX_uint32 - 1) return {};
    const uint32 Domain = PredictionKey.IsServerInitiatedKey() ? 0x80000000u : 0;
    FGuid Id(Grant.GrantId.A ^ E.StateRevision, Grant.GrantId.B ^ (E.ActionSequence + 1),
        Grant.GrantId.C ^ (uint32(uint16(PredictionKey.Current)) | Domain), Grant.GrantId.D ^ 0x52494445u);
    // Within one immutable grant, action sequence/key/domain occupy separate components.
    // Keep the one all-zero result valid without colliding with another result in that grant.
    if (!Id.IsValid()) Id.A = 1;
    return Id;
}
