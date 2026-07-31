// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "DustDeveloperSettings.h"

UDustDeveloperSettings::UDustDeveloperSettings()
{
	CategoryName = TEXT("DUST");
	SectionName = TEXT("World & Simulation");
}

const UDustDeveloperSettings& UDustDeveloperSettings::Get()
{
	const UDustDeveloperSettings* Settings = GetDefault<UDustDeveloperSettings>();
	check(Settings);
	return *Settings;
}

FName UDustDeveloperSettings::GetCategoryName() const
{
	return TEXT("DUST");
}
