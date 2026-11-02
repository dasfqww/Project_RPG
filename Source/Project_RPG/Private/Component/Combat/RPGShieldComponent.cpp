#include "Component/Combat/RPGShieldComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

URPGShieldComponent::URPGShieldComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

URPGShieldComponent* URPGShieldComponent::FindOrCreate(AActor* Owner)
{
	if (!IsValid(Owner) || !Owner->HasAuthority()) return nullptr;
	if (auto* Found = Owner->FindComponentByClass<URPGShieldComponent>()) return Found;
	auto* Created = NewObject<URPGShieldComponent>(Owner);
	Owner->AddInstanceComponent(Created);
	Created->RegisterComponent();
	return Created;
}

bool URPGShieldComponent::BindAttributes()
{
	auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	if (!ASC || ASC->GetAvatarActor() != GetOwner() || !ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetMaxHealthAttribute())) return false;
	if (BoundASC == ASC) return true;
	if (BoundASC.IsValid())
	{
		BoundASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).Remove(HealthChanged);
		BoundASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetMaxHealthAttribute()).Remove(MaximumChanged);
	}
	Entries.Reset();
	BoundASC = ASC;
	HealthChanged = ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).AddUObject(this, &ThisClass::HandleHealthChanged);
	MaximumChanged = ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetMaxHealthAttribute()).AddUObject(this, &ThisClass::HandleMaximumChanged);
	return true;
}

bool URPGShieldComponent::GrantOrRefresh(const FGuid& Source, const float Fraction, const float Duration)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld() || !Source.IsValid()
		|| !FMath::IsFinite(Fraction) || Fraction <= 0.f || Fraction > 1.f
		|| !FMath::IsFinite(Duration) || Duration <= 0.f || !BindAttributes()) return false;
	FEntry Entry;
	Entry.Lifetime = FRPGApprovedHitContext(GetOwner(), GetOwner());
	const float Maximum = BoundASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute());
	if (!Entry.Lifetime.IsCurrent(GetOwner(), GetOwner()) || !FMath::IsFinite(Maximum) || Maximum <= 0.f) return false;
	Entry.Fraction = Fraction;
	Entry.Remaining = Maximum * Fraction;
	Entry.Expires = GetWorld()->GetTimeSeconds() + Duration;
	if (!FMath::IsFinite(Entry.Remaining) || !FMath::IsFinite(Entry.Expires)) return false;
	Entries.Add(Source, Entry); // Replacement, never accumulation from the same source.
	Refresh();
	return true;
}

void URPGShieldComponent::RemoveSource(const FGuid& Source)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	Entries.Remove(Source);
	Refresh();
}

void URPGShieldComponent::ClearShields()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	Entries.Reset();
	Refresh();
}

void URPGShieldComponent::Refresh()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	const float Maximum = BoundASC.IsValid() ? BoundASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()) : 0.f;
	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		auto& Entry = It.Value();
		Entry.Remaining = FMath::Min(Entry.Remaining, FMath::Max(0.f, Maximum) * Entry.Fraction);
		if (!Entry.Lifetime.IsCurrent(GetOwner(), GetOwner()) || !FMath::IsFinite(Entry.Remaining)
			|| Entry.Remaining <= 0.f || Now >= Entry.Expires) It.RemoveCurrent();
	}
	FRPGShieldPresentation Next;
	float Soonest = TNumericLimits<float>::Max();
	for (const auto& Pair : Entries)
	{
		Next.Remaining += Pair.Value.Remaining;
		Next.ExpiresAt = FMath::Max(Next.ExpiresAt, Pair.Value.Expires);
		Soonest = FMath::Min(Soonest, Pair.Value.Expires);
	}
	GetWorld()->GetTimerManager().ClearTimer(ExpiryTimer);
	if (!Entries.IsEmpty()) GetWorld()->GetTimerManager().SetTimer(ExpiryTimer, this, &ThisClass::Refresh, FMath::Max(0.001f, Soonest - Now), false);
	if (State.Remaining != Next.Remaining || State.ExpiresAt != Next.ExpiresAt)
	{
		State = Next;
		GetOwner()->ForceNetUpdate();
		OnRep_State();
	}
}

float URPGShieldComponent::AbsorbDamage(const float Damage)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !FMath::IsFinite(Damage) || Damage <= 0.f) return Damage;
	Refresh();
	float Left = Damage;
	TArray<FGuid> Sources;
	Entries.GetKeys(Sources);
	Sources.Sort([this](const FGuid& A, const FGuid& B) { return Entries[A].Expires < Entries[B].Expires; });
	for (const FGuid& Source : Sources)
	{
		auto& Entry = Entries[Source];
		const float Absorbed = FMath::Min(Left, Entry.Remaining);
		Entry.Remaining -= Absorbed;
		Left -= Absorbed;
		if (Left <= 0.f) break;
	}
	Refresh();
	return Left;
}

void URPGShieldComponent::HandleHealthChanged(const FOnAttributeChangeData& Change)
{
	if (!FMath::IsFinite(Change.NewValue) || Change.NewValue <= 0.f) ClearShields();
}

void URPGShieldComponent::HandleMaximumChanged(const FOnAttributeChangeData&) { Refresh(); }
void URPGShieldComponent::OnRep_State() { OnShieldChanged.Broadcast(State); }

void URPGShieldComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ExpiryTimer);
	if (BoundASC.IsValid())
	{
		BoundASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).Remove(HealthChanged);
		BoundASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetMaxHealthAttribute()).Remove(MaximumChanged);
	}
	Entries.Reset();
	Super::EndPlay(Reason);
}

void URPGShieldComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGShieldComponent, State, COND_OwnerOnly, REPNOTIFY_Always);
}
