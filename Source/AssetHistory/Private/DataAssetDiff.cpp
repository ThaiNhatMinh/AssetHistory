
#include "DataAssetDiff.h"
#include "DetailsDiff.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "SBlueprintDif"
const FName DefaultsMode = FName(TEXT("DefaultsMode"));
FText RightRevision = LOCTEXT("OlderRevisionIdentifier", "Right Revision");

class IDiffControl
{
public:
	virtual ~IDiffControl() {}

	/** Adds widgets to the tree of differences to show */
	virtual void GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences) = 0;
};

static TSharedRef<SWidget> GenerateObjectDiffWidget(FSingleObjectDiffEntry DiffEntry, FText ObjectName)
{
	return SNew(STextBlock)
		.Text(DiffViewUtils::PropertyDiffMessage(DiffEntry, ObjectName))
		.ToolTipText(DiffViewUtils::PropertyDiffMessage(DiffEntry, ObjectName))
		.ColorAndOpacity(DiffViewUtils::Differs());
}

/** Generic wrapper around a details view, this does not actually fill out OutTreeEntries */
class FDetailsDiffControl : public TSharedFromThis<FDetailsDiffControl>, public IDiffControl
{
public:
	FDetailsDiffControl(const UObject* InOldObject, const UObject* InNewObject, FOnDiffEntryFocused InSelectionCallback)
		: SelectionCallback(InSelectionCallback)
		, OldDetails(InOldObject, FDetailsDiff::FOnDisplayedPropertiesChanged())
		, NewDetails(InNewObject, FDetailsDiff::FOnDisplayedPropertiesChanged())
	{
		OldDetails.DiffAgainst(NewDetails, DifferingProperties, true);

		TSet<FPropertyPath> PropertyPaths;
		Algo::Transform(DifferingProperties, PropertyPaths,
			[&InOldObject](const FSingleObjectDiffEntry& DiffEntry)
			{
				return DiffEntry.Identifier.ResolvePath(InOldObject);
			});

		OldDetails.DetailsWidget()->UpdatePropertyAllowList(PropertyPaths);

		PropertyPaths.Reset();
		Algo::Transform(DifferingProperties, PropertyPaths,
			[&InNewObject](const FSingleObjectDiffEntry& DiffEntry)
			{
				return DiffEntry.Identifier.ResolvePath(InNewObject);
			});

		NewDetails.DetailsWidget()->UpdatePropertyAllowList(PropertyPaths);
	}

	virtual void GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences) override
	{
		for (const FSingleObjectDiffEntry& Difference : DifferingProperties)
		{
			TSharedPtr<FBlueprintDifferenceTreeEntry> Entry = MakeShared<FBlueprintDifferenceTreeEntry>(
				FOnDiffEntryFocused::CreateSP(AsShared(), &FDetailsDiffControl::OnSelectDiffEntry, Difference.Identifier),
				FGenerateDiffEntryWidget::CreateStatic(&GenerateObjectDiffWidget, Difference, RightRevision));
			Children.Push(Entry);
			OutRealDifferences.Push(Entry);
		}
	}

	TSharedRef<SWidget> OldDetailsWidget() { return OldDetails.DetailsWidget(); }
	TSharedRef<SWidget> NewDetailsWidget() { return NewDetails.DetailsWidget(); }

protected:
	virtual void OnSelectDiffEntry(FPropertySoftPath PropertyName)
	{
		SelectionCallback.ExecuteIfBound();
		OldDetails.HighlightProperty(PropertyName);
		NewDetails.HighlightProperty(PropertyName);
	}

	FOnDiffEntryFocused SelectionCallback;
	FDetailsDiff OldDetails;
	FDetailsDiff NewDetails;

	TArray<FSingleObjectDiffEntry> DifferingProperties;
	TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> > Children;
};


/** Override for CDO special case */
class FCDODiffControl : public FDetailsDiffControl
{
public:
	FCDODiffControl(const UObject* InOldObject, const UObject* InNewObject, FOnDiffEntryFocused InSelectionCallback)
		: FDetailsDiffControl(InOldObject, InNewObject, InSelectionCallback)
	{
	}

	virtual void GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences) override
	{
		FDetailsDiffControl::GenerateTreeEntries(OutTreeEntries, OutRealDifferences);

		const bool bHasDifferences = Children.Num() != 0;
		if (!bHasDifferences)
		{
			// make one child informing the user that there are no differences:
			Children.Push(FBlueprintDifferenceTreeEntry::NoDifferencesEntry());
		}

		OutTreeEntries.Push(FBlueprintDifferenceTreeEntry::CreateCategoryEntry(
			NSLOCTEXT("FBlueprintDifferenceTreeEntry", "DefaultsLabel", "Defaults"),
			NSLOCTEXT("FBlueprintDifferenceTreeEntry", "DefaultsTooltip", "The list of changes made in the Defaults panel"),
			SelectionCallback,
			Children,
			bHasDifferences
		));
	}
};

void SDataAssetDiff::Construct( const FArguments& InArgs)
{
	check(InArgs._AssetOld && InArgs._AssetNew);
	AssetNew = InArgs._AssetNew;
	AssetOld = InArgs._AssetOld;
	bLockViews = true;

	if (InArgs._ParentWindow.IsValid())
	{
		WeakParentWindow = InArgs._ParentWindow;

		AssetEditorCloseDelegate = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnAssetEditorRequestClose().AddSP(this, &SDataAssetDiff::OnCloseAssetEditor);
	}

	FToolBarBuilder NavToolBarBuilder(TSharedPtr< const FUICommandList >(), FMultiBoxCustomization::None);
	NavToolBarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SDataAssetDiff::PrevDiff),
			FCanExecuteAction::CreateSP( this, &SDataAssetDiff::HasPrevDiff)
		)
		, NAME_None
		, LOCTEXT("SBlueprintDif", "Prev")
		, LOCTEXT("PrevDiffTooltip", "Go to previous difference")
		, FSlateIcon(FEditorStyle::GetStyleSetName(), "BlueprintDif.PrevDiff")
	);
	NavToolBarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SDataAssetDiff::NextDiff),
			FCanExecuteAction::CreateSP(this, &SDataAssetDiff::HasNextDiff)
		)
		, NAME_None
		, LOCTEXT("NextDiffLabel", "Next")
		, LOCTEXT("NextDiffTooltip", "Go to next difference")
		, FSlateIcon(FEditorStyle::GetStyleSetName(), "BlueprintDif.NextDiff")
	);

	FToolBarBuilder GraphToolbarBuilder(TSharedPtr< const FUICommandList >(), FMultiBoxCustomization::None);
	GraphToolbarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateSP(this, &SDataAssetDiff::OnToggleLockView))
		, NAME_None
		, LOCTEXT("LockGraphsLabel", "Lock/Unlock")
		, LOCTEXT("LockGraphsTooltip", "Force all graph views to change together, or allow independent scrolling/zooming")
		, TAttribute<FSlateIcon>(this, &SDataAssetDiff::GetLockViewImage)
	);
	GraphToolbarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateSP(this, &SDataAssetDiff::OnToggleSplitViewMode))
		, NAME_None
		, LOCTEXT("SplitGraphsModeLabel", "Vertical/Horizontal")
		, LOCTEXT("SplitGraphsModeLabelTooltip", "Toggles the split view of graphs between vertical and horizontal")
		, TAttribute<FSlateIcon>(this, &SDataAssetDiff::GetSplitViewModeImage)
	);

	DifferencesTreeView = DiffTreeView::CreateTreeView(&MasterDifferencesList);

	GenerateDifferencesList();

	const auto TextBlock = [](FText Text) -> TSharedRef<SWidget>
	{
		return SNew(SBox)
			.Padding(FMargin(4.0f,10.0f))
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Visibility(EVisibility::HitTestInvisible)
			.TextStyle(FEditorStyle::Get(), "DetailsView.CategoryTextStyle")
			.Text(Text)
			];
	};

	TopRevisionInfoWidget =
		SNew(SSplitter)
		.Visibility(EVisibility::HitTestInvisible)
		+ SSplitter::Slot()
		.Value(.2f)
		[
			SNew(SBox)
		];

	GraphToolBarWidget = 
		SNew(SSplitter)
		.Visibility(EVisibility::HitTestInvisible)
		+ SSplitter::Slot()
		.Value(.2f)
		[
			SNew(SBox)
		]
	+ SSplitter::Slot()
		.Value(.8f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
		.AutoWidth()
		[
			GraphToolbarBuilder.MakeWidget()
		]	
		];

	this->ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush( "Docking.Tab", ".ContentAreaBrush" ))
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
		.VAlign(VAlign_Top)
		[
			TopRevisionInfoWidget.ToSharedRef()		
		]
	+ SOverlay::Slot()
		.VAlign(VAlign_Top)
		.Padding(0.0f, 6.0f, 0.0f, 4.0f)
		[
			GraphToolBarWidget.ToSharedRef()
		]
	+ SOverlay::Slot()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		.Padding(4.f)
		.AutoWidth()
		[
			NavToolBarBuilder.MakeWidget()
		]
	+ SHorizontalBox::Slot()
		[
			SNew(SSpacer)
		]
		]
	+ SVerticalBox::Slot()
		[
			SNew(SSplitter)
			+ SSplitter::Slot()
		.Value(.2f)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			DifferencesTreeView.ToSharedRef()
		]
		]
	+ SSplitter::Slot()
		.Value(.8f)
		[
			SAssignNew(ModeContents, SBox)
		]
		]
		]
		]
		];

	SetCurrentMode(DefaultsMode);

}

SDataAssetDiff::~SDataAssetDiff()
{
	if (AssetEditorCloseDelegate.IsValid())
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnAssetEditorRequestClose().Remove(AssetEditorCloseDelegate);
	}
}

void SDataAssetDiff::OnCloseAssetEditor(UObject* Asset, EAssetEditorCloseReason CloseReason)
{
	if (AssetOld == Asset || AssetNew == Asset || CloseReason == EAssetEditorCloseReason::CloseAllAssetEditors)
	{
		// Tell our window to close and set our selves to collapsed to try and stop it from ticking
		SetVisibility(EVisibility::Collapsed);

		if (AssetEditorCloseDelegate.IsValid())
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnAssetEditorRequestClose().Remove(AssetEditorCloseDelegate);
		}

		if (WeakParentWindow.IsValid())
		{
			WeakParentWindow.Pin()->RequestDestroyWindow();
		}
	}
}

TSharedRef<SWidget> SDataAssetDiff::DefaultEmptyPanel()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BlueprintDifGraphsToolTip", "Select Graph to Diff"))
		];
}

TSharedPtr<SWindow> SDataAssetDiff::CreateDiffWindow(FText WindowTitle, UPrimaryDataAsset*OldBlueprint, UPrimaryDataAsset* NewBlueprint, const FRevisionInfo& OldRevision, const FRevisionInfo& NewRevision)
{
	// sometimes we're comparing different revisions of one single asset (other 
	// times we're comparing two completely separate assets altogether)
	bool bIsSingleAsset = (NewBlueprint->GetName() == OldBlueprint->GetName());

	TSharedPtr<SWindow> Window = SNew(SWindow)
		.Title(WindowTitle)
		.ClientSize(FVector2D(1000, 800));

	Window->SetContent(SNew(SDataAssetDiff)
		.AssetOld(OldBlueprint)
		.AssetNew(NewBlueprint)
		.OldRevision(OldRevision)
		.NewRevision(NewRevision)
		.ShowAssetNames(!bIsSingleAsset)
		.ParentWindow(Window));

	// Make this window a child of the modal window if we've been spawned while one is active.
	TSharedPtr<SWindow> ActiveModal = FSlateApplication::Get().GetActiveModalWindow();
	if (ActiveModal.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window.ToSharedRef(), ActiveModal.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window.ToSharedRef());
	}

	return Window;
}

void SDataAssetDiff::NextDiff()
{
	DiffTreeView::HighlightNextDifference(DifferencesTreeView.ToSharedRef(), RealDifferences, MasterDifferencesList);
}

void SDataAssetDiff::PrevDiff()
{
	DiffTreeView::HighlightPrevDifference(DifferencesTreeView.ToSharedRef(), RealDifferences, MasterDifferencesList);
}

bool SDataAssetDiff::HasNextDiff() const
{
	return DiffTreeView::HasNextDifference(DifferencesTreeView.ToSharedRef(), RealDifferences);
}

bool SDataAssetDiff::HasPrevDiff() const
{
	return DiffTreeView::HasPrevDifference(DifferencesTreeView.ToSharedRef(), RealDifferences);
}


void SDataAssetDiff::OnToggleLockView()
{
	bLockViews = !bLockViews;
}

void SDataAssetDiff::OnToggleSplitViewMode()
{
	bVerticalSplitGraphMode = !bVerticalSplitGraphMode;

	if(SSplitter* DiffGraphSplitterPtr = DiffGraphSplitter.Get())
	{
		DiffGraphSplitterPtr->SetOrientation(bVerticalSplitGraphMode ? Orient_Horizontal : Orient_Vertical);
	}
}

FSlateIcon SDataAssetDiff::GetLockViewImage() const
{
	return FSlateIcon(FEditorStyle::GetStyleSetName(), bLockViews ? "Icons.Lock" : "Icons.Unlock");
}

FSlateIcon SDataAssetDiff::GetSplitViewModeImage() const
{
	return FSlateIcon(FEditorStyle::GetStyleSetName(), bVerticalSplitGraphMode ? "BlueprintDif.VerticalDiff.Small" : "BlueprintDif.HorizontalDiff.Small");
}

void SDataAssetDiff::GenerateDifferencesList()
{
	MasterDifferencesList.Empty();
	RealDifferences.Empty();
	ModePanels.Empty();

	// Now that we have done the diffs, create the panel widgets
	ModePanels.Add(DefaultsMode, GenerateDefaultsPanel());
	DifferencesTreeView->RebuildList();
}

SDataAssetDiff::FDiffControl SDataAssetDiff::GenerateDefaultsPanel()
{
	const UObject* A = AssetOld;
	const UObject* B = AssetNew;

	TSharedPtr<FCDODiffControl> NewDiffControl = MakeShared<FCDODiffControl>(A, B, FOnDiffEntryFocused::CreateRaw(this, &SDataAssetDiff::SetCurrentMode, DefaultsMode));
	NewDiffControl->GenerateTreeEntries(MasterDifferencesList, RealDifferences);

	SDataAssetDiff::FDiffControl Ret;
	Ret.DiffControl = NewDiffControl;
	Ret.Widget = SNew(SSplitter)
		.PhysicalSplitterHandleSize(10.0f)
		+ SSplitter::Slot()
		.Value(0.5f)
		[
			NewDiffControl->OldDetailsWidget()
		]
	+ SSplitter::Slot()
		.Value(0.5f)
		[
			NewDiffControl->NewDetailsWidget()
		];

	return Ret;
}

TSharedRef<SBox> SDataAssetDiff::GenerateRevisionInfoWidgetForPanel(TSharedPtr<SWidget>& OutGeneratedWidget, const FText& InRevisionText) const
{
	return SAssignNew(OutGeneratedWidget,SBox)
		.Padding(FMargin(4.0f, 10.0f))
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "DetailsView.CategoryTextStyle")
		.Text(InRevisionText)
		.ShadowColorAndOpacity(FColor::Black)
		.ShadowOffset(FVector2D(1.4,1.4))
		];
}

void SDataAssetDiff::SetCurrentMode(FName NewMode)
{
	if( CurrentMode == NewMode )
	{
		return;
	}

	CurrentMode = NewMode;

	FDiffControl* FoundControl = ModePanels.Find(NewMode);

	if (FoundControl)
	{
		ModeContents->SetContent(FoundControl->Widget.ToSharedRef());
	}
	else
	{
		ensureMsgf(false, TEXT("Diff panel does not support mode %s"), *NewMode.ToString() );
	}

	OnModeChanged(NewMode);
}

void SDataAssetDiff::UpdateTopSectionVisibility(const FName& InNewViewMode) const
{
	SSplitter* GraphToolBarPtr = GraphToolBarWidget.Get();
	SSplitter* TopRevisionInfoWidgetPtr = TopRevisionInfoWidget.Get();

	if (!GraphToolBarPtr || !TopRevisionInfoWidgetPtr)
	{
		return;
	}

	GraphToolBarPtr->SetVisibility(EVisibility::Collapsed);
	TopRevisionInfoWidgetPtr->SetVisibility(EVisibility::HitTestInvisible);
}

void SDataAssetDiff::OnModeChanged(const FName& InNewViewMode) const
{
	UpdateTopSectionVisibility(InNewViewMode);
}

#undef LOCTEXT_NAMESPACE