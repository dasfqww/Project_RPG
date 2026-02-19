// Fill out your copyright notice in the Description page of Project Settings.

#include "Interface/RPGTeamAgentInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGTeamAgentInterface)

void IRPGTeamAgentInterface::ConditionalBroadcastTeamChanged(TScriptInterface<IRPGTeamAgentInterface> This, FGenericTeamId OldTeamID, FGenericTeamId NewTeamID)
{
	if (OldTeamID != NewTeamID)
	{
		const int32 OldTeamIndex = RPGGenericTeamIdToInteger(OldTeamID); 
		const int32 NewTeamIndex = RPGGenericTeamIdToInteger(NewTeamID);

		UObject* ThisObj = This.GetObject();
		
		// 로그 채널이 따로 정의되어 있지 않다면 LogTemp 사용
		UE_LOG(LogTemp, Verbose, TEXT("[%s] assigned team %d"), *GetNameSafe(ThisObj), NewTeamIndex);

		if (FOnRPGTeamIndexChangedDelegate* Delegate = This.GetInterface()->GetOnTeamIndexChangedDelegate())
		{
			Delegate->Broadcast(ThisObj, OldTeamIndex, NewTeamIndex);
		}
	}
}

ETeamAttitude::Type IRPGTeamAgentInterface::GetTeamAttitudeTowards(const AActor& Other) const
{
	const APawn* OtherPawn = Cast<APawn>(&Other);
	const AActor* LookTarget = OtherPawn ? (AActor*)OtherPawn->GetController() : &Other;

	if (const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(LookTarget))
	{
		FGenericTeamId MyTeamID = GetGenericTeamId();
		FGenericTeamId OtherTeamID = TeamAgent->GetGenericTeamId();

		if (MyTeamID == FGenericTeamId::NoTeam || OtherTeamID == FGenericTeamId::NoTeam)
		{
			return ETeamAttitude::Neutral;
		}

		if (MyTeamID == OtherTeamID)
		{
			return ETeamAttitude::Friendly;
		}
		else
		{
			return ETeamAttitude::Hostile;
		}
	}

	return ETeamAttitude::Neutral;
}