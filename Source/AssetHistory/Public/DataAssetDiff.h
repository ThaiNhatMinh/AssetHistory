// Copyright Ather Labs, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Developer/AssetTools/Public/IAssetTypeActions.h"
#include "DiffUtils.h"

/* Visual Diff between two Blueprints*/
class SDataAssetDiff: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SDataAssetDiff ){}
	SLATE_ARGUMENT( const class UPrimaryDataAsset*, AssetOld )
		SLATE_ARGUMENT( const class UPrimaryDataAsset*, AssetNew )
		SLATE_ARGUMENT( struct FRevisionInfo, OldRevision )
		SLATE_ARGUMENT( struct FRevisionInfo, NewRevision )
		SLATE_ARGUMENT( bool, ShowAssetNames )
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
		SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDataAssetDiff();

	/** Called when user clicks on an entry in the listview of differences */
	void OnDiffListSelectionChanged(TSharedPtr<struct FDiffResultItem> TheDiff);

	/** Helper function for generating an empty widget */
	static TSharedRef<SWidget> DefaultEmptyPanel();

	/** Helper function to create a window that holds a diff widget */
	static TSharedPtr<SWindow> CreateDiffWindow(FText WindowTitle, UPrimaryDataAsset* AssetOld, UPrimaryDataAsset* AssetNew, const struct FRevisionInfo& OldRevision, const struct FRevisionInfo& NewRevision);

protected:
	/** Called when user clicks button to go to next difference */
	void NextDiff();

	/** Called when user clicks button to go to prev difference */
	void PrevDiff();

	/** Called to determine whether we have a list of differences to cycle through */
	bool HasNextDiff() const;
	bool HasPrevDiff() const;

	/** Disable the focus on a particular pin */
	void DisablePinDiffFocus();

	/** User toggles the option to lock the views between the two blueprints */
	void OnToggleLockView();

	/** User toggles the option to change the split view mode betwwen vertical and horizontal */
	void OnToggleSplitViewMode();

	/** Get the image to show for the toggle lock option*/
	FSlateIcon GetLockViewImage() const;

	/** Get the image to show for the toggle split view mode option*/
	FSlateIcon GetSplitViewModeImage() const;

	/** Function used to generate the list of differences and the widgets needed to calculate that list */
	void GenerateDifferencesList();

	/** Called when editor may need to be closed */
	void OnCloseAssetEditor(UObject* Asset, EAssetEditorCloseReason CloseReason);

	struct FDiffControl
	{
		FDiffControl()
			: Widget()
			, DiffControl(nullptr)
		{
		}

		TSharedPtr<SWidget> Widget;
		TSharedPtr< class IDiffControl > DiffControl;
	};

	FDiffControl GenerateDefaultsPanel();

	TSharedRef<SBox> GenerateRevisionInfoWidgetForPanel(TSharedPtr<SWidget>& OutGeneratedWidget,const FText& InRevisionText) const;

	/** Accessor and event handler for toggling between diff view modes (defaults, components, graph view, interface, macro): */
	void SetCurrentMode(FName NewMode);
	FName GetCurrentMode() const { return CurrentMode; }
	void OnModeChanged(const FName& InNewViewMode) const;

	void UpdateTopSectionVisibility(const FName& InNewViewMode) const;

	FName CurrentMode;

	/** If the two views should be locked */
	bool	bLockViews;

	/** If the view on Graph Mode should be divided vertically */
	bool bVerticalSplitGraphMode = true;

	/** Contents widget that we swap when mode changes (defaults, components, etc) */
	TSharedPtr<SBox> ModeContents;

	TSharedPtr<SSplitter> TopRevisionInfoWidget;

	TSharedPtr<SSplitter> DiffGraphSplitter;

	TSharedPtr<SSplitter> GraphToolBarWidget;

	friend struct FListItemGraphToDiff;

	/** We can't use the global tab manager because we need to instance the diff control, so we have our own tab manager: */
	TSharedPtr<FTabManager> TabManager;

	/** Tree of differences collected across all panels: */
	TArray< TSharedPtr<class FBlueprintDifferenceTreeEntry> > MasterDifferencesList;

	/** List of all differences, cached so that we can iterate only the differences and not labels, etc: */
	TArray< TSharedPtr<class FBlueprintDifferenceTreeEntry> > RealDifferences;

	/** Tree view that displays the differences, cached for the buttons that iterate the differences: */
	TSharedPtr< STreeView< TSharedPtr< FBlueprintDifferenceTreeEntry > > > DifferencesTreeView;

	/** Stored references to widgets used to display various parts of a blueprint, from the mode name */
	TMap<FName, FDiffControl> ModePanels;

	/** A pointer to the window holding this */
	TWeakPtr<SWindow> WeakParentWindow;

	FDelegateHandle AssetEditorCloseDelegate;
	const UPrimaryDataAsset* AssetOld;
	const UPrimaryDataAsset* AssetNew;
};