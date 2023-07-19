
#include "PrimaryAssetEditorToolkit.h"
#include "Widgets/SWidget.h"
#include "Widgets/Images/SThrobber.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "AssetTypeActions/AssetTypeActions_DataAsset.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Images/SSpinningImage.h"

#define LOCTEXT_NAMESPACE "SipherSkillDataAssetTypeActions"

static void OnDiffRevisionPicked(const FRevisionInfoExtended& PrevRevisionInfo, const FRevisionInfoExtended& RevisionInfo, UPrimaryDataAsset* InCurrentAsset);

TSharedRef<FSimpleAssetEditor> PrimaryAssetEditorToolkit::CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, const TArray<UObject*>& ObjectsToEdit, FGetDetailsViewObjects GetDetailsViewObjects)
{
	TSharedRef< PrimaryAssetEditorToolkit > NewEditor(new PrimaryAssetEditorToolkit());
	NewEditor->InitEditor(Mode, InitToolkitHost, ObjectsToEdit, GetDetailsViewObjects);
	return NewEditor;
}

PrimaryAssetEditorToolkit::PrimaryAssetEditorToolkit()
{
}

PrimaryAssetEditorToolkit::~PrimaryAssetEditorToolkit()
{
}

void PrimaryAssetEditorToolkit::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, const TArray<UObject*>& ObjectsToEdit, FGetDetailsViewObjects GetDetailsViewObjects)
{
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& ToolbarBuilder)
			{
				ToolbarBuilder.BeginSection("SourceControl");
				ToolbarBuilder.AddComboButton(FUIAction(), FOnGetContent::CreateRaw(this, &PrimaryAssetEditorToolkit::MakeDiffMenu),
					LOCTEXT("Diff", "History"),
					LOCTEXT("BlueprintEditorDiffToolTip", "Diff against previous revisions"),
					FSlateIcon(FAppStyle::Get().GetStyleSetName(), "BlueprintDiff.ToolbarIcon"));

				ToolbarBuilder.EndSection();
			}));
	AddToolbarExtender(ToolbarExtender);

	FSimpleAssetEditor::InitEditor(Mode, InitToolkitHost, ObjectsToEdit, GetDetailsViewObjects);
}

void PrimaryAssetEditorToolkit::InitToolMenuContext(FToolMenuContext& MenuContext)
{
}

TSharedRef<SWidget> PrimaryAssetEditorToolkit::MakeDiffMenu()
{
	if (ISourceControlModule::Get().IsEnabled() && ISourceControlModule::Get().GetProvider().IsAvailable())
	{
		if (RevisionPicker.IsValid())
			return RevisionPicker.ToSharedRef();
		if (auto Object = Cast<UPrimaryDataAsset>(GetEditingObject()))
		{
			// Add our async SCC task widget
			return SAssignNew(RevisionPicker, SRevisionMenu, Object)
				.OnRevisionSelected_Static(&OnDiffRevisionPicked, Object);
		}
		else
		{
			// if BlueprintObj is null then this means that multiple blueprints are selected
			FMenuBuilder MenuBuilder(true, NULL);
			MenuBuilder.AddMenuEntry(LOCTEXT("NoRevisionsForMultipleBlueprints", "Invalid object"),
				FText(), FSlateIcon(), FUIAction());
			return MenuBuilder.MakeWidget();
		}
	}

	FMenuBuilder MenuBuilder(true, NULL);
	MenuBuilder.AddMenuEntry(LOCTEXT("SourceControlDisabled", "Source control is disabled"),
		FText(), FSlateIcon(), FUIAction());
	return MenuBuilder.MakeWidget();
}

/**  */
namespace ESourceControlQueryState
{
	enum Type
	{
		NotQueried,
		QueryInProgress,
		Queried,
	};
}

//------------------------------------------------------------------------------
SRevisionMenu::~SRevisionMenu()
{
	// cancel any operation if this widget is destroyed while in progress
	if (SourceControlQueryState == ESourceControlQueryState::QueryInProgress)
	{
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		if (SourceControlQueryOp.IsValid() && SourceControlProvider.CanCancelOperation(SourceControlQueryOp.ToSharedRef()))
		{
			SourceControlProvider.CancelOperation(SourceControlQueryOp.ToSharedRef());
		}
	}
}

//------------------------------------------------------------------------------
void SRevisionMenu::Construct(const FArguments& InArgs, UPrimaryDataAsset const* Blueprint)
{
	OnRevisionSelected = InArgs._OnRevisionSelected;

	SourceControlQueryState = ESourceControlQueryState::NotQueried;

	ChildSlot	
		[
			SAssignNew(MenuBox, SVerticalBox)
			+SVerticalBox::Slot()
			[
				SNew(SBorder)
				.Visibility(this, &SRevisionMenu::GetInProgressVisibility)
				.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
				.Content()
				[
					SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SThrobber)
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DiffMenuOperationInProgress", "Updating history..."))
						]
						+SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.Visibility(this, &SRevisionMenu::GetCancelButtonVisibility)
							.OnClicked(this, &SRevisionMenu::OnCancelButtonClicked)
							.VAlign(VAlign_Center)
							.HAlign(HAlign_Center)
							.Content()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("DiffMenuCancelButton", "Cancel"))
							]
						]
				]
			]
		];

	if (Blueprint != nullptr)
	{
		Filename = SourceControlHelpers::PackageFilename(Blueprint->GetPathName());

		// make sure the history info is up to date
		SourceControlQueryOp = ISourceControlOperation::Create<FUpdateStatus>();
		// get the cached state
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(Filename, EStateCacheUsage::Use);
		if (SourceControlState->GetHistorySize() == 0)
			SourceControlQueryOp->SetUpdateHistory(true);
		SourceControlProvider.Execute(SourceControlQueryOp.ToSharedRef(), Filename, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateSP(this, &SRevisionMenu::OnSourceControlQueryComplete));

		SourceControlQueryState = ESourceControlQueryState::QueryInProgress;
	}
}

//------------------------------------------------------------------------------
EVisibility SRevisionMenu::GetInProgressVisibility() const
{
	return (SourceControlQueryState == ESourceControlQueryState::QueryInProgress) ? EVisibility::Visible : EVisibility::Collapsed;
}

//------------------------------------------------------------------------------
EVisibility SRevisionMenu::GetCancelButtonVisibility() const
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	return SourceControlQueryOp.IsValid() && SourceControlProvider.CanCancelOperation(SourceControlQueryOp.ToSharedRef()) ? EVisibility::Visible : EVisibility::Collapsed;
}

//------------------------------------------------------------------------------
FReply SRevisionMenu::OnCancelButtonClicked() const
{
	if (SourceControlQueryOp.IsValid())
	{
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		SourceControlProvider.CancelOperation(SourceControlQueryOp.ToSharedRef());
	}

	return FReply::Handled();
}

//------------------------------------------------------------------------------
void SRevisionMenu::OnSourceControlQueryComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	check(SourceControlQueryOp == InOperation);

	// Add pop-out menu for each revision
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection =*/false, /*InCommandList =*/NULL);
	MenuBuilder.BeginSection("UpdateHistory");
	MenuBuilder.AddMenuEntry(LOCTEXT("LocalRevision", "Update History"), LOCTEXT("LocalRevisionToolTip", "Force update history"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
			{
				if (SourceControlQueryState == ESourceControlQueryState::QueryInProgress)
					return;
				ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
				SourceControlQueryOp = ISourceControlOperation::Create<FUpdateStatus>();
				SourceControlQueryOp->SetUpdateHistory(true);
				if (MenuBox->IsValidSlotIndex(3))
				{
					auto& MenuSlot = MenuBox->GetSlot(3);
					MenuBox->RemoveSlot(MenuSlot.GetWidget());
				}
				SourceControlQueryState = ESourceControlQueryState::QueryInProgress;
				SourceControlProvider.Execute(SourceControlQueryOp.ToSharedRef(), Filename, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateSP(this, &SRevisionMenu::OnUpdateHistoryComplete));
			})));
	MenuBuilder.EndSection();

	auto UpdateMenu = MenuBuilder.MakeWidget(nullptr, 30);
	UpdateMenu->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]()
		{
			return (SourceControlQueryState == ESourceControlQueryState::QueryInProgress) ? EVisibility::Collapsed : EVisibility::Visible;
		}));
	MenuBox->AddSlot() 
		.AutoHeight()
		[
			UpdateMenu
		];
	MenuBox->AddSlot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Visibility(EVisibility::Visible)
		];

	OnUpdateHistoryComplete(InOperation, InResult);
}

void SRevisionMenu::OnUpdateHistoryComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection =*/true, /*InCommandList =*/NULL);

	if (InResult == ECommandResult::Succeeded)
	{
		// get the cached state
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(Filename, EStateCacheUsage::Use);

		if (SourceControlState.IsValid() && SourceControlState->GetHistorySize() > 0)
		{
			if (SourceControlState->IsModified())
			{
				auto HeadRevision = SourceControlState->GetHistoryItem(0);
				FOnRevisionSelected OnRevisionSelectedDelegate = OnRevisionSelected;
				FRevisionInfoExtended Prev = {
					HeadRevision->GetRevision(),
					HeadRevision->GetCheckInIdentifier(),
					HeadRevision->GetDate(),
					HeadRevision
				};
				auto LocalRevision = FRevisionInfoExtended::InvalidRevision();
				LocalRevision.Revision = "HEAD";

				auto OnItemLocalSelected = [OnRevisionSelectedDelegate, Prev, LocalRevision]()
				{
					OnRevisionSelectedDelegate.ExecuteIfBound(Prev, LocalRevision);
				};
				MenuBuilder.AddMenuEntry(LOCTEXT("RevisionNumber", "Local"), LOCTEXT("RevisionNumber", "Diff local changes"), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda(OnItemLocalSelected)));
			}
			for (int32 HistoryIndex = 0; HistoryIndex < SourceControlState->GetHistorySize(); HistoryIndex++)
			{
				TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Revision = SourceControlState->GetHistoryItem(HistoryIndex);
				if (Revision.IsValid())
				{
					FInternationalization& I18N = FInternationalization::Get();

					FText Label = FText::Format(LOCTEXT("RevisionNumber", "{0}"), FText::FromString(Revision->GetRevision()));

					FFormatNamedArguments Args;
					Args.Add(TEXT("CheckInNumber"), FText::AsNumber(Revision->GetCheckInIdentifier(), NULL, I18N.GetInvariantCulture()));
					Args.Add(TEXT("Revision"), FText::FromString(Revision->GetRevision()));
					Args.Add(TEXT("UserName"), FText::FromString(Revision->GetUserName()));
					Args.Add(TEXT("DateTime"), FText::AsDate(Revision->GetDate()));
					Args.Add(TEXT("ChanglistDescription"), FText::FromString(Revision->GetDescription()));
					FText ToolTipText;
					if (ISourceControlModule::Get().GetProvider().UsesChangelists())
					{
						ToolTipText = FText::Format(LOCTEXT("ChangelistToolTip", "CL #{CheckInNumber} {UserName} \n{DateTime} \n{ChanglistDescription}"), Args);
					}
					else
					{
						ToolTipText = FText::Format(LOCTEXT("RevisionToolTip", "{Revision} {UserName} \n{DateTime} \n{ChanglistDescription}"), Args);
					}

					FRevisionInfoExtended RevisionInfo = { 
						Revision->GetRevision(), 
						Revision->GetCheckInIdentifier(), 
						Revision->GetDate() 
					};
					FRevisionInfoExtended Prev = FRevisionInfoExtended::InvalidRevision();
					if (HistoryIndex + 1 < SourceControlState->GetHistorySize())
					{
						auto PrevRevision = SourceControlState->GetHistoryItem(HistoryIndex + 1);
						if (PrevRevision.IsValid())
						{
							Prev.Revision = PrevRevision->GetRevision();
							Prev.Changelist = PrevRevision->GetCheckInIdentifier();
							Prev.Date = PrevRevision->GetDate();
							Prev.RevisionData = PrevRevision;
						}
					}

					RevisionInfo.RevisionData = Revision;

					FOnRevisionSelected OnRevisionSelectedDelegate = OnRevisionSelected;
					auto OnMenuItemSelected = [RevisionInfo, OnRevisionSelectedDelegate, Prev]()
					{
						OnRevisionSelectedDelegate.ExecuteIfBound(Prev, RevisionInfo);
					};
					Prev = RevisionInfo;
					MenuBuilder.AddMenuEntry( TAttribute<FText>(Label), ToolTipText, FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda(OnMenuItemSelected)) );
				}
			}
		}
		else
		{
			// Show 'empty' item in toolbar
			MenuBuilder.AddMenuEntry(LOCTEXT("NoRevisonHistory", "No revisions found"),
				FText(), FSlateIcon(), FUIAction());
		}
	}
	else
	{
		// Show 'empty' item in toolbar
		MenuBuilder.AddMenuEntry(LOCTEXT("NoRevisonHistory", "No revisions found"),
			FText(), FSlateIcon(), FUIAction());
	}
	MenuBox->AddSlot()
		[
			MenuBuilder.MakeWidget(nullptr, 500)
		];

	SourceControlQueryOp.Reset();
	SourceControlQueryState = ESourceControlQueryState::Queried;
}


/** Delegate called to diff a specific revision with the current */
static void OnDiffRevisionPicked(const FRevisionInfoExtended& PrevRevisionInfo, const FRevisionInfoExtended& RevisionInfo, UPrimaryDataAsset* InCurrentAsset)
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	FString CurrentPkgName;
	FString PrevPkgName;
	if (RevisionInfo.RevisionData.IsValid())
		RevisionInfo.RevisionData->Get(CurrentPkgName);
	// Get the revision of this package from source control
	if (PrevRevisionInfo.RevisionData.IsValid())
		PrevRevisionInfo.RevisionData->Get(PrevPkgName);

	FString AssetName = FPaths::GetBaseFilename(InCurrentAsset->GetPathName());
	if (RevisionInfo.RevisionData.IsValid())
		AssetName = FPaths::GetBaseFilename(RevisionInfo.RevisionData->GetFilename(), true);
	UObject* PreviousAsset = NULL;
	UObject* Asset = NULL;
	if (UPackage* PreviousTempPkg = LoadPackage(NULL, *PrevPkgName, LOAD_ForDiff | LOAD_DisableCompileOnLoad))
	{
		PreviousAsset = FindObject<UObject>(PreviousTempPkg, *AssetName);
	}

	if (UPackage* CurrentPkg = LoadPackage(NULL, *CurrentPkgName, LOAD_ForDiff | LOAD_DisableCompileOnLoad))
	{
		Asset = FindObject<UObject>(CurrentPkg, *AssetName);;
	}
	else if (RevisionInfo.Revision == "HEAD")
	{
		Asset = InCurrentAsset;
	}
	// Try and load that package

	if (IsValid(Asset))
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		if (IsValid(PreviousAsset))
		{
			FRevisionInfo OldRevision = { PrevRevisionInfo.RevisionData->GetRevision(), PrevRevisionInfo.RevisionData->GetCheckInIdentifier(), PrevRevisionInfo.RevisionData->GetDate() };
			FRevisionInfo CurrentRevision;
			if (RevisionInfo.Revision == "HEAD")
				CurrentRevision = {"HEAD", 0, FDateTime::Now()};
			else
				CurrentRevision = { RevisionInfo.RevisionData->GetRevision(), RevisionInfo.RevisionData->GetCheckInIdentifier(), RevisionInfo.RevisionData->GetDate() };
			AssetToolsModule.Get().DiffAssets(PreviousAsset, Asset, OldRevision, CurrentRevision);
		}
		else
		{
			AssetToolsModule.Get().OpenEditorForAssets({Asset});
		}
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("SourceControl.HistoryWindow", "UnableToLoadAssets", "Unable to load assets to diff. Content may no longer be supported?"));
	}
}