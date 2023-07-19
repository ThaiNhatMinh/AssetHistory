#pragma once

#include "CoreMinimal.h"
#include "Toolkits/SimpleAssetEditor.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"
#include "AssetTypeActions_Base.h"


struct FRevisionInfoExtended : public FRevisionInfo
{
	TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> RevisionData;

	static inline FRevisionInfoExtended InvalidRevision()
	{
		static const FRevisionInfoExtended Ret = { TEXT(""), -1, FDateTime() };
		return Ret;
	}
};

class SRevisionMenu : public SCompoundWidget
{
	DECLARE_DELEGATE_TwoParams(FOnRevisionSelected, const FRevisionInfoExtended&, const FRevisionInfoExtended&)

public:
	SLATE_BEGIN_ARGS(SRevisionMenu){}
		SLATE_EVENT(FOnRevisionSelected, OnRevisionSelected)
	SLATE_END_ARGS()

	~SRevisionMenu();

	void Construct(const FArguments& InArgs, UPrimaryDataAsset const* Blueprint);

private: 
	/** Delegate used to determine the visibility 'in progress' widgets */
	EVisibility GetInProgressVisibility() const;
	/** Delegate used to determine the visibility of the cancel button */
	EVisibility GetCancelButtonVisibility() const;

	/** Delegate used to cancel a source control operation in progress */
	FReply OnCancelButtonClicked() const;
	/** Callback for when the source control operation is complete */
	void OnSourceControlQueryComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult);
	void OnUpdateHistoryComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult);

	/**  */
	FOnRevisionSelected OnRevisionSelected;
	/** The name of the file we want revision info for */
	FString Filename;
	/** The box we are using to display our menu */
	TSharedPtr<SVerticalBox> MenuBox;
	/** The source control operation in progress */
	TSharedPtr<FUpdateStatus, ESPMode::ThreadSafe> SourceControlQueryOp;
	/** The state of the SCC query */
	uint32 SourceControlQueryState;
};

/**
 * 
 */
class PrimaryAssetEditorToolkit : public FSimpleAssetEditor
{
public:
	static TSharedRef<FSimpleAssetEditor> CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, const TArray<UObject*>& ObjectsToEdit,  FGetDetailsViewObjects GetDetailsViewObjects = FGetDetailsViewObjects());
	PrimaryAssetEditorToolkit();
	~PrimaryAssetEditorToolkit();

	void InitEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, const TArray<UObject*>& ObjectsToEdit, FGetDetailsViewObjects GetDetailsViewObjects);
	void InitToolMenuContext(FToolMenuContext& MenuContext) override;

	TSharedRef<SWidget> MakeDiffMenu();
	TSharedPtr<SRevisionMenu> RevisionPicker;
};
