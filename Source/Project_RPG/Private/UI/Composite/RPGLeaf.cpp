// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Composite/RPGLeaf.h"

void URPGLeaf::ApplyFunction(FuncType Function)
{
	Function(this);
}
