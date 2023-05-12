// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetHistory.h"
#include "IAssetTools.h"
#include "FDataAssetTypeActions.h"

#define LOCTEXT_NAMESPACE "FAssetHistoryModule"

void FAssetHistoryModule::StartupModule()
{
	DataAssetTypeActions = MakeShared<FDataAssetTypeActions>();
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.RegisterAssetTypeActions(DataAssetTypeActions.ToSharedRef());
}

void FAssetHistoryModule::ShutdownModule()
{
	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");
	IAssetTools& AssetTools = AssetToolsModule->Get();
	AssetTools.UnregisterAssetTypeActions(DataAssetTypeActions.ToSharedRef());
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FAssetHistoryModule, AssetHistory)