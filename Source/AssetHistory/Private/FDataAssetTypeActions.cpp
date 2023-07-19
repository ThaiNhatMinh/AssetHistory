
#include "FDataAssetTypeActions.h"
#include "DataAssetDiff.h"
#include "ToolMenuSection.h"
#include "PrimaryAssetEditorToolkit.h"

#define LOCTEXT_NAMESPACE "SipherSkillDataAssetTypeActions"

void FDataAssetTypeActions::PerformAssetDiff(UObject* OldAsset, UObject* NewAsset, const FRevisionInfo& OldRevision, const FRevisionInfo& NewRevision) const
{
	auto OldPrimaryAsset = CastChecked<UPrimaryDataAsset>(OldAsset);
	auto NewPrimaryAsset = CastChecked<UPrimaryDataAsset>(NewAsset);
	// sometimes we're comparing different revisions of one single asset (other 
	// times we're comparing two completely separate assets altogether)
	bool bIsSingleAsset = (OldAsset->GetName() == NewAsset->GetName());

	FText WindowTitle = LOCTEXT("NamelessWidgetBlueprintDiff", "Widget Blueprint Diff");
	// if we're diffing one asset against itself 
	if (bIsSingleAsset)
	{
		// identify the assumed single asset in the window's title
		WindowTitle = FText::Format(LOCTEXT("Data Asset Diff", "{0}"), FText::FromString(NewAsset->GetName()));
	}
	SDataAssetDiff::CreateDiffWindow(WindowTitle, OldPrimaryAsset, NewPrimaryAsset, OldRevision, NewRevision);
}


UClass* FDataAssetTypeActions::GetSupportedClass() const
{
	return UPrimaryDataAsset::StaticClass();
}

void FDataAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
{
	PrimaryAssetEditorToolkit::CreateEditor(EToolkitMode::Standalone, EditWithinLevelEditor, InObjects);
}

#undef LOCTEXT_NAMESPACE
