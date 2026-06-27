// Copyright (c) Yevhenii Selivanov

#include "Subsystems/GfpmPackageEditorSubsystem.h"

// GFPM
#include "GfpmAssetManager.h"
#include "GfpmUtils.h"

// UE
#include "Async/Async.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFeaturesSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProperties.h"
#include "IDesktopPlatform.h"
#include "IUATHelperModule.h"
#include "Misc/App.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "Misc/Paths.h"
#include "Settings/PlatformsMenuSettings.h"
#include "Settings/ProjectPackagingSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GfpmPackageEditorSubsystem)

namespace GfpmPackageEditorInternal
{
	// Container file extensions cooked mod ships
	static const TArray<FString> ContainerExtensions = {TEXT("pak"), TEXT("ucas"), TEXT("utoc"), TEXT("sig")};

	// Platform, architecture and build configuration mod cooks for, taken from editor Package Project selection
	struct FPackageTarget
	{
		// BuildCookRun -platform value, e.g. Win64
		FString Platform;
		// Cooked container suffix + staging folder name, e.g. Windows
		FString CookedPlatform;
		// Architecture id, e.g. x64, empty when platform exposes no selection
		FString Architecture;
		// BuildCookRun -clientconfig value, e.g. Development
		FString Configuration;
	};

	// Resolves Package Project platform, architecture and build configuration, defaulting to host platform and Development when unset
	static FPackageTarget ResolvePackageTarget()
	{
		const UPlatformsMenuSettings& MenuSettingsRef = *GetDefault<UPlatformsMenuSettings>();

		FName PlatformName = MenuSettingsRef.PackagePlatform;
		if (PlatformName.IsNone())
		{
			// Nothing chosen in Package Project, default to host platform
			PlatformName = FName(FPlatformProperties::IniPlatformName());
		}

		EProjectPackagingBuildConfigurations BuildConfig = MenuSettingsRef.GetPackageBuildConfiguration();
		if (BuildConfig == EProjectPackagingBuildConfigurations::PPBC_MAX)
		{
			// No menu override, fall back to project packaging default
			BuildConfig = GetDefault<UProjectPackagingSettings>()->BuildConfiguration;
		}
		if (BuildConfig == EProjectPackagingBuildConfigurations::PPBC_MAX)
		{
			// Project default also unset, use Development
			BuildConfig = EProjectPackagingBuildConfigurations::PPBC_Development;
		}

		FPackageTarget Target;
		Target.CookedPlatform = PlatformName.ToString();
		Target.Platform = FDataDrivenPlatformInfoRegistry::GetPlatformInfo(PlatformName).UBTPlatformString;
		if (Target.Platform.IsEmpty())
		{
			// Platform has no UBT mapping, fall back to its cooked name
			Target.Platform = Target.CookedPlatform;
		}
		Target.Architecture = MenuSettingsRef.GetArchitectureForPlatform(PlatformName);
		Target.Configuration = LexToString(UProjectPackagingSettings::ConfigurationInfo[static_cast<int32>(BuildConfig)].Configuration);
		return Target;
	}

	// Returns release registry path mod cooks against, written by project cook via createreleaseversion
	static FString GetReleaseRegistryPath(const FString& ReleaseVersion, const FString& Configuration, const FString& CookedPlatform)
	{
		const FString ReleaseName = FString::Printf(TEXT("%s_%s"), *ReleaseVersion, *Configuration);
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("Releases"), ReleaseName, CookedPlatform, TEXT("AssetRegistry.bin"));
	}

	// Returns global Content/Paks dir of build found at or under picked path, empty when none
	static FString FindBuildContentPaksDir(const FString& PickedPath, const FString& CookedPlatform)
	{
		// Project game pak engine mounts at startup, its folder is install target
		const FString ProjectPakName = FString::Printf(TEXT("%s-%s.pak"), FApp::GetProjectName(), *CookedPlatform);

		TArray<FString> FoundProjectPaks;
		IFileManager::Get().FindFilesRecursive(FoundProjectPaks, *PickedPath, *ProjectPakName, /*Files*/ true, /*Directories*/ false);
		if (FoundProjectPaks.IsEmpty())
		{
			// Picked path is not packaged build of this project
			return FString();
		}

		return FPaths::GetPath(FoundProjectPaks[0]);
	}

	// Copies cooked mod container files from archive into own subfolder under target Content/Paks, replacing any prior copy of this plugin
	static int32 InstallPackagedMod(FName GameFeatureName, const FString& ArchiveDir, const FString& TargetPaksDir, const FString& CookedPlatform)
	{
		const FString PluginName = GameFeatureName.ToString();
		// Cook names DLC container after plugin and project, e.g. <Plugin><Project>-<CookedPlatform>
		const FString SourceStem = FString::Printf(TEXT("%s%s-%s"), *PluginName, FApp::GetProjectName(), *CookedPlatform);
		// Install drops project name and nests under own folder, so each mod ships as self-contained removable unit, e.g. <Plugin>/<Plugin>-<CookedPlatform>
		const FString ModPaksDir = FPaths::Combine(TargetPaksDir, PluginName);
		const FString DestStem = FString::Printf(TEXT("%s-%s"), *PluginName, *CookedPlatform);
		IFileManager& FileManager = IFileManager::Get();

		// Wipe any prior copy so re-install never leaves stale or double-mounted containers behind
		FileManager.DeleteDirectory(*ModPaksDir, /*RequireExists*/ false, /*Tree*/ true);

		int32 InstalledCount = 0;
		for (const FString& Extension : ContainerExtensions)
		{
			const FString SourceFileName = FString::Printf(TEXT("%s.%s"), *SourceStem, *Extension);

			TArray<FString> FoundContainers;
			FileManager.FindFilesRecursive(FoundContainers, *ArchiveDir, *SourceFileName, /*Files*/ true, /*Directories*/ false);
			if (FoundContainers.IsEmpty())
			{
				// Optional sibling (e.g. .sig only exists when signing is enabled), skip it
				continue;
			}

			const FString DestPath = FPaths::Combine(ModPaksDir, FString::Printf(TEXT("%s.%s"), *DestStem, *Extension));
			if (FileManager.Copy(*DestPath, *FoundContainers[0]) == COPY_OK)
			{
				++InstalledCount;
			}
		}

		UE_LOG(LogGameFeatures, Display, TEXT("%hs: installed %i container files of mod '%s' into '%s'"), __FUNCTION__, InstalledCount, *PluginName, *ModPaksDir);
		return InstalledCount;
	}

	// Compact target line shown as task subtitle, listing platform and configuration once
	static FString DescribeTarget(const FPackageTarget& Target)
	{
		const FString& Architecture = Target.Architecture;
		const FString ArchitectureSuffix = Architecture.IsEmpty() ? FString() : FString::Printf(TEXT(" (%s)"), *Architecture);
		return FString::Printf(TEXT("%s%s | %s"), *Target.CookedPlatform, *ArchitectureSuffix, *Target.Configuration);
	}
} // namespace GfpmPackageEditorInternal

// Packages given plugin as separate mod and installs it into build at given path
void UGfpmPackageEditorSubsystem::PackageGameFeatureIntoBuild(FName GameFeatureName, const FString& BuildPath)
{
	if (!ensureMsgf(FPaths::DirectoryExists(BuildPath), TEXT("ASSERT: [%i] %hs:\nTarget directory '%s' does not exist!"), __LINE__, __FUNCTION__, *BuildPath))
	{
		return;
	}

	CookMod(GameFeatureName, BuildPath, nullptr);
}

// Packages every registered plugin as separate mod and installs them into build at given path
void UGfpmPackageEditorSubsystem::PackageAllGameFeaturesIntoBuild(const FString& BuildPath)
{
	if (!ensureMsgf(FPaths::DirectoryExists(BuildPath), TEXT("ASSERT: [%i] %hs:\nTarget directory '%s' does not exist!"), __LINE__, __FUNCTION__, *BuildPath))
	{
		return;
	}

	const TArray<FString> RegisteredPlugins = UGfpmUtils::GetAllRegisteredGameFeaturePlugins();
	TArray<FName> PackageQueue;
	PackageQueue.Reserve(RegisteredPlugins.Num());
	for (const FString& PluginName : RegisteredPlugins)
	{
		PackageQueue.Add(FName(*PluginName));
	}

	// Cook full project without game features first, then cook every plugin as mod into its Content/Paks
	const TWeakObjectPtr<UGfpmPackageEditorSubsystem> WeakThis(this);
	CookProjectAll(BuildPath, [WeakThis, PackageQueue = MoveTemp(PackageQueue), BuildPath]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->CookModQueue(PackageQueue, BuildPath);
		}
	});
}

// Prompts developer for target build folder then packages given plugin into it
void UGfpmPackageEditorSubsystem::PromptPackageGameFeature(FName GameFeatureName)
{
	const FString BuildPath = PromptBuildDir(TEXT("Pick packaged build to install mod into, or any folder for separate distribution"));
	if (BuildPath.IsEmpty())
	{
		// Developer cancelled picker
		return;
	}

	PackageGameFeatureIntoBuild(GameFeatureName, BuildPath);
}

// Prompts developer for target build folder then packages every registered plugin into it
void UGfpmPackageEditorSubsystem::PromptPackageAllGameFeatures()
{
	const FString BuildPath = PromptBuildDir(TEXT("Pick empty output folder to package modular build into"));
	if (BuildPath.IsEmpty())
	{
		// Developer cancelled picker
		return;
	}

	PackageAllGameFeaturesIntoBuild(BuildPath);
}

// Opens desktop folder picker with given dialog title, empty when developer cancels
FString UGfpmPackageEditorSubsystem::PromptBuildDir(const FString& DialogTitle) const
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!ensureMsgf(DesktopPlatform, TEXT("ASSERT: [%i] %hs:\n'DesktopPlatform' is null!"), __LINE__, __FUNCTION__))
	{
		return FString();
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	FString PickedPath;
	DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, DialogTitle, TEXT(""), PickedPath);
	return PickedPath;
}

// Cooks one plugin as mod DLC into build, runs continuation after install
void UGfpmPackageEditorSubsystem::CookMod(FName GameFeatureName, const FString& BuildPath, TFunction<void()> OnComplete)
{
	const GfpmPackageEditorInternal::FPackageTarget Target = GfpmPackageEditorInternal::ResolvePackageTarget();

	const FString ReleaseRegistryPath = GfpmPackageEditorInternal::GetReleaseRegistryPath(ProjectReleaseVersion, Target.Configuration, Target.CookedPlatform);
	if (!FPaths::FileExists(ReleaseRegistryPath))
	{
		// External Data Layer cell binds to host world's COOKED exports, so mod must cook against cooked release, never editor's uncooked one which leaves those host-world imports null at runtime
		CookProjectMinimal([WeakThis = TWeakObjectPtr(this), GameFeatureName, BuildPath, OnComplete, ReleaseRegistryPath]()
		{
			const UGfpmPackageEditorSubsystem* This = WeakThis.Get();
			if (This
			    && ensureMsgf(FPaths::FileExists(ReleaseRegistryPath), TEXT("ASSERT: [%i] %hs:\nMinimal project cook produced no release registry at '%s'"), __LINE__, __FUNCTION__, *ReleaseRegistryPath))
			{
				WeakThis->CookMod(GameFeatureName, BuildPath, OnComplete);
			}
		});
		return;
	}

	// Install into build's Content/Paks when picked path is one, otherwise use picked folder directly so mod ships standalone
	const FString FoundPaksDir = GfpmPackageEditorInternal::FindBuildContentPaksDir(BuildPath, Target.CookedPlatform);
	const FString TargetPaksDir = FoundPaksDir.IsEmpty() ? BuildPath : FoundPaksDir;

	const FString ModName = GameFeatureName.ToString();
	const FString StagedRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StagedBuilds"));
	const FString ArchiveDir = FPaths::Combine(StagedRoot, FString::Printf(TEXT("EditorCook_%s"), *ModName));
	const FString StagingDir = FPaths::Combine(StagedRoot, FString::Printf(TEXT("EditorCook_Staging_%s"), *ModName));

	// External Data Layer host worlds mod injects into, force their World Partition map cook so content lands in mod
	const TArray<FName> HostWorlds = UGfpmAssetManager::GetExternalDataLayerHostWorlds(ModName);
	FString CookerOptions = AdditionalCookerOptions;
	if (!HostWorlds.IsEmpty())
	{
		TArray<FString> HostWorldStrings;
		for (const FName HostWorld : HostWorlds)
		{
			HostWorldStrings.Add(HostWorld.ToString());
		}
		CookerOptions += FString::Printf(TEXT(" -Map=%s"), *FString::Join(HostWorldStrings, TEXT("+")));
	}

	// Architecture is optional, only some platforms expose selection
	FString ArchitectureArg;
	if (!Target.Architecture.IsEmpty())
	{
		ArchitectureArg = FString::Printf(TEXT("-clientarchitecture=%s "), *Target.Architecture);
	}

	// Mod cook cooks against same release CookProjectAll emits (ProjectReleaseVersion_Configuration order), so mod excludes only real project content, never another release
	const FString CommandLine = FString::Printf(
	    TEXT("BuildCookRun -project=\"%s\" -platform=%s %s-clientconfig=%s -target=%s %s ")
	        TEXT("-stagingdirectory=\"%s\" -archivedirectory=\"%s\" ")
	            TEXT("-DLCName=%s -basedonreleaseversion=%s_%s -AdditionalCookerOptions=\"%s\""),
	    *FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), *Target.Platform, *ArchitectureArg, *Target.Configuration, FApp::GetProjectName(), *CookModArguments,
	    *StagingDir, *ArchiveDir,
	    *ModName, *ProjectReleaseVersion, *Target.Configuration, *CookerOptions);

	// Cook out-of-process, then install produced containers into chosen build when it completes
	const TWeakObjectPtr<UGfpmPackageEditorSubsystem> WeakThis(this);
	const FText TaskName = FText::FromString(FString::Printf(TEXT("Packaging mod %s"), *ModName));
	const FString CookedPlatform = Target.CookedPlatform;
	IUATHelperModule::Get().CreateUatTask(CommandLine, FText::FromString(GfpmPackageEditorInternal::DescribeTarget(Target)), TaskName,
	    FText::FromString(TEXT("Packaging Mod")), nullptr, nullptr,
	    [WeakThis, ModName, ArchiveDir, TargetPaksDir, CookedPlatform, OnComplete](FString Result, double)
	{
		// UAT completion fires on background thread, finish on game thread so next cook task creates its Slate notification there
		AsyncTask(ENamedThreads::GameThread, [WeakThis, ModName, ArchiveDir, TargetPaksDir, CookedPlatform, OnComplete, Result]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->HandleCookModFinished(Result, FName(*ModName), ArchiveDir, TargetPaksDir, CookedPlatform, OnComplete);
			}
		});
	});
}

// Cooks full project excluding game features into build path, runs continuation after it succeeds
void UGfpmPackageEditorSubsystem::CookProjectAll(const FString& BuildPath, TFunction<void()> OnComplete)
{
	const GfpmPackageEditorInternal::FPackageTarget Target = GfpmPackageEditorInternal::ResolvePackageTarget();

	// Architecture is optional, only some platforms expose selection
	FString ArchitectureArg;
	if (!Target.Architecture.IsEmpty())
	{
		ArchitectureArg = FString::Printf(TEXT("-clientarchitecture=%s "), *Target.Architecture);
	}

	// Force game feature exclusion so project ships none inline, every plugin lands as separate mod instead
	const FString CommandLine = FString::Printf(
	    TEXT("BuildCookRun -project=\"%s\" -platform=%s %s-clientconfig=%s -target=%s %s ")
	        TEXT("-archivedirectory=\"%s\" -createreleaseversion=%s_%s -AdditionalCookerOptions=\"-GfpmExcludeGameFeatures\""),
	    *FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), *Target.Platform, *ArchitectureArg, *Target.Configuration, FApp::GetProjectName(), *CookProjectAllArguments,
	    *BuildPath, *ProjectReleaseVersion, *Target.Configuration);

	const TWeakObjectPtr<UGfpmPackageEditorSubsystem> WeakThis(this);
	const FText TaskName = FText::FromString(FString::Printf(TEXT("Packaging %s"), FApp::GetProjectName()));
	IUATHelperModule::Get().CreateUatTask(CommandLine, FText::FromString(GfpmPackageEditorInternal::DescribeTarget(Target)), TaskName,
	    TaskName, nullptr, nullptr,
	    [WeakThis, OnComplete](FString Result, double)
	{
		// UAT completion fires on background thread, finish on game thread so mod cook tasks create their Slate notifications there
		AsyncTask(ENamedThreads::GameThread, [WeakThis, OnComplete, Result]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->HandleCookProjectAllFinished(Result, OnComplete);
			}
		});
	});
}

// Cooks minimal project (game features excluded, no -build, no stage) so mod cook has release to cook against, runs continuation after it succeeds
void UGfpmPackageEditorSubsystem::CookProjectMinimal(TFunction<void()> OnComplete)
{
	const GfpmPackageEditorInternal::FPackageTarget Target = GfpmPackageEditorInternal::ResolvePackageTarget();

	FString ArchitectureArg;
	if (!Target.Architecture.IsEmpty())
	{
		ArchitectureArg = FString::Printf(TEXT("-clientarchitecture=%s "), *Target.Architecture);
	}

	// Minimal cook: configured args skip -build and stage (content-only modder has no toolchain nor staged binaries),
	// -createreleaseversion still writes release for mod cook
	const FString CommandLine = FString::Printf(
	    TEXT("BuildCookRun -project=\"%s\" -platform=%s %s-clientconfig=%s -target=%s %s ")
	        TEXT("-createreleaseversion=%s_%s -AdditionalCookerOptions=\"-GfpmExcludeGameFeatures\""),
	    *FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), *Target.Platform, *ArchitectureArg, *Target.Configuration, FApp::GetProjectName(), *CookProjectMinimalArguments,
	    *ProjectReleaseVersion, *Target.Configuration);

	const FText TaskName = FText::FromString(TEXT("Cooking minimal project for mod"));
	IUATHelperModule::Get().CreateUatTask(CommandLine, FText::FromString(GfpmPackageEditorInternal::DescribeTarget(Target)), TaskName,
	    TaskName, nullptr, nullptr,
	    [OnComplete](FString Result, double)
	{
		if (Result != TEXT("Completed"))
		{
			// Minimal project cook failed, do not chain mod cook against missing or partial release
			return;
		}

		// UAT completion fires on background thread, finish on game thread so chained mod cook task creates its Slate notification there
		AsyncTask(ENamedThreads::GameThread, [OnComplete]()
		{
			if (OnComplete)
			{
				OnComplete();
			}
		});
	});
}

// Cooks queue head into build then recurses on completion, so plugins cook one after another
void UGfpmPackageEditorSubsystem::CookModQueue(const TArray<FName>& Queue, const FString& BuildPath)
{
	TArray<FName> MutableQueue = Queue;
	if (MutableQueue.IsEmpty())
	{
		// Every plugin cooked, nothing left
		return;
	}

	const FName NextPlugin = MutableQueue[0];
	MutableQueue.RemoveAt(0);

	CookMod(NextPlugin, BuildPath, [WeakThis = TWeakObjectPtr(this), MutableQueue, BuildPath]()
	{
		if (UGfpmPackageEditorSubsystem* This = WeakThis.Get())
		{
			This->CookModQueue(MutableQueue, BuildPath);
		}
	});
}

// Installs cooked mod into build then drives queue continuation, runs on game thread after mod cook task completes
void UGfpmPackageEditorSubsystem::HandleCookModFinished(const FString& Result, FName GameFeatureName, const FString& ArchiveDir, const FString& TargetPaksDir, const FString& CookedPlatform, const TFunction<void()>& OnComplete)
{
	if (Result == TEXT("Completed"))
	{
		GfpmPackageEditorInternal::InstallPackagedMod(GameFeatureName, ArchiveDir, TargetPaksDir, CookedPlatform);
	}

	if (OnComplete)
	{
		// Drive next plugin in cook-all queue, or no-op for single cook
		OnComplete();
	}
}

// Drives queue continuation once project cook succeeds, runs on game thread after project cook task completes
void UGfpmPackageEditorSubsystem::HandleCookProjectAllFinished(const FString& Result, const TFunction<void()>& OnComplete)
{
	if (Result != TEXT("Completed"))
	{
		// Project cook failed, do not cook mods into incomplete build
		return;
	}

	if (OnComplete)
	{
		OnComplete();
	}
}
