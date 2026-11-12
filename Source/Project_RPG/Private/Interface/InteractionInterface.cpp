// Fill out your copyright notice in the Description page of Project Settings.


#include "Interface/InteractionInterface.h"
#include "Misc/SecureHash.h"

FGuid IInteractionInterface::MakeRidingLocalInteractionIdentity(const FString& Value)
{
    FGuid Result;
    if (!Value.IsEmpty())
    {
        const FTCHARToUTF8 Bytes(*Value);
        FGuid::ParseExact(FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()), EGuidFormats::Digits, Result);
    }
    return Result; // Configuration fingerprint, never an authorization credential.
}

// Add default functionality here for any IInteractionInterface functions that are not pure virtual.

void IInteractionInterface::BeginFocus()
{
}

void IInteractionInterface::EndFocus()
{
}

void IInteractionInterface::BeginInteract()
{
}

void IInteractionInterface::EndInteract()
{
}

void IInteractionInterface::Interact(APlayerController* PlayerController)
{
}
