// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Fauna/DustSpeciesData.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "DustSpeciesData"

#if WITH_EDITOR

EDataValidationResult UDustSpeciesData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (SpeciesId.IsNone())
	{
		Context.AddError(LOCTEXT("NoSpeciesId",
			"SpeciesId must be set; the creature ledger keys on it."));
		Result = EDataValidationResult::Invalid;
	}

	// This is the rule that keeps the bestiary from drifting into fantasy.
	// It is enforced in code because it is the one that gets quietly skipped
	// when a deadline is close.
	if (EvolutionaryRationale.Len() < 40)
	{
		Context.AddError(LOCTEXT("NoRationale",
			"EvolutionaryRationale must explain what environmental pressure this animal's body answers. "
			"If it cannot be explained, the species does not belong in DUST."));
		Result = EDataValidationResult::Invalid;
	}

	if (ReactionRadius > AwarenessRadius)
	{
		Context.AddError(LOCTEXT("ReactionExceedsAwareness",
			"ReactionRadius exceeds AwarenessRadius; the creature would react to a player it has not noticed."));
		Result = EDataValidationResult::Invalid;
	}

	// The gap between noticing and reacting is where the interesting behaviour
	// lives. Collapsing it makes animals feel like trip-wires.
	if (AwarenessRadius > 0.0f && ReactionRadius / AwarenessRadius > 0.75f)
	{
		Context.AddWarning(LOCTEXT("NarrowReactionBand",
			"ReactionRadius is close to AwarenessRadius. The creature will go from unaware to reacting almost instantly, "
			"losing the observable 'deciding what to do about you' behaviour."));
	}

	if (RunSpeed <= WalkSpeed)
	{
		Context.AddWarning(LOCTEXT("RunNotFasterThanWalk",
			"RunSpeed is not greater than WalkSpeed; fleeing will not read as urgent."));
	}

	if (Disposition == EDustDisposition::Territorial && TerritoryRadius <= 0.0f)
	{
		Context.AddError(LOCTEXT("NoTerritory",
			"A Territorial species needs a positive TerritoryRadius or it has nothing to defend."));
		Result = EDataValidationResult::Invalid;
	}

	if (bMigratory && MigrationRoute.Num() < 2)
	{
		Context.AddError(LOCTEXT("ShortMigrationRoute",
			"A migratory species needs at least two stops on its route."));
		Result = EDataValidationResult::Invalid;
	}

	if (ObservableBehaviours.Num() == 0)
	{
		Context.AddWarning(LOCTEXT("NoObservableBehaviours",
			"No ObservableBehaviours. Watching this creature will never unlock journal text, "
			"so patient observation goes unrewarded."));
	}

	if (PawnClass.IsNull())
	{
		Context.AddError(LOCTEXT("NoPawnClass",
			"PawnClass is unset; this species can never be promoted to a visible creature."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
