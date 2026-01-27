// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Composite/RPGComposite.h"
#include "Blueprint/WidgetTree.h"

void URPGComposite::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	WidgetTree->ForEachWidget([this](UWidget* Widget)
		{
			URPGCompositeBase* Composite = Cast<URPGCompositeBase>(Widget);
			if (IsValid(Composite))
			{
				Children.Add(Composite);
				Composite->Collapse();
			}
		});
}

void URPGComposite::ApplyFunction(FuncType Function)
{
	for (auto& Child : Children)
	{
		Child->ApplyFunction(Function);
	}
}

void URPGComposite::Collapse()
{
	for (auto& Child : Children)
	{
		Child->Collapse();
	}
}
