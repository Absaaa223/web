// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "DustCoreEditorModule.h"

#include "DustLog.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FDustCoreEditorModule"

void FDustCoreEditorModule::StartupModule()
{
	// Tool menus may not exist yet during module load; defer registration.
	ToolMenusHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDustCoreEditorModule::RegisterToolMenus));

	RegisterValidators();

	UE_LOG(LogDust, Log, TEXT("DustCoreEditor online."));
}

void FDustCoreEditorModule::ShutdownModule()
{
	if (ToolMenusHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		ToolMenusHandle.Reset();
	}
}

void FDustCoreEditorModule::RegisterToolMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->AddSection(
		TEXT("DustTools"),
		LOCTEXT("DustToolsSection", "DUST"));

	// Individual tool entries are added by the modules that own them
	// (DustWorldEditor adds the terrain baker, DustFaunaEditor the ecology
	// debugger). This module only guarantees the section exists first.
	Section.AddSeparator(TEXT("DustToolsSeparator"));
}

void FDustCoreEditorModule::RegisterValidators()
{
	// Content validators are registered as UEditorValidatorBase subclasses and
	// discovered automatically by the Data Validation module. This hook is kept
	// so CI can force-load the module before running the commandlet.
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDustCoreEditorModule, DustCoreEditor)
