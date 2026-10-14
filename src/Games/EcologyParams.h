/***********************************************************************
EcologyParams - a sourced, swappable parameter store for the succession
model's three successional roles (pioneer/intermediate/climax).

Phase 1 of the succession-model rebuild (see CLAUDE.md's "Final
contribution alignment and roadmap toward a mid-December deadline"
entry, 2026-10-14). This class builds ONLY the mechanism - loading
either a "designer" or "published" preset from an external file and
exposing it live in the GUI - not the suitability model itself. Nothing
in VegetationField's actual growth logic reads these values yet; that
is Phase 3 (soil depth/moisture/slope fields plus the real suitability
test and the active-displacement succession mechanic). Building the
sourced-parameter mechanism first, independently of the model that will
consume it, mirrors how every other phase of this project has been
built: the mechanism gets proven and testable on its own before being
wired into the simulation that depends on it.

WHY THIS EXISTS, in terms of the thesis itself: section 1.5/1.7 of the
project's own thesis introduction describes a model whose "relationship
to data is a changeable setting" - parameters stored in a file separate
from the code, each entry naming its source, swappable between
designer-set values and values drawn from published ecology. This class
IS that mechanism, not a description of it. The Preset enum below
(Designer/Published) is the literal data-free/data-informed distinction
from that section, not just a label applied after the fact.

ROLE MAPPING: the three successional roles are fixed at three, mapped
onto VegetationField's EXISTING three density channels rather than
introducing new ones - fruit=pioneer, nut=intermediate, shrub=climax.
This specific mapping, and the decision to keep these as abstract roles
rather than renaming the channels to real species identities, was the
user's explicit choice (see CLAUDE.md): real named species and their
field-study citations back each role's actual numbers in the "published"
preset, but the system and the thesis keep talking in terms of the three
abstract roles. Real species/citations are already identified by the
user as of this phase - not an open research dependency, just a data
hand-off still pending (Phase 2) at the time this class was written.

UNITS ARE DELIBERATELY NOT YET CONVERTED to this project's internal 0..1
calibrated-range fractions (the convention VegetationField's elevation/
water-depth fields already use - see VegetationField.h's header note).
Each Range below stores whatever real-world unit its source material
actually reports (fraction of field capacity, degrees of slope, cm of
soil depth, etc.) plus a literal unit string, so the file stays a
faithful, checkable transcription of its cited source rather than an
already-reinterpreted number. Converting into VegetationField's internal
units is Phase 3's job, once the real suitability test exists to do the
converting.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include <array>
#include <string>

class EcologyParams {
public:
	static constexpr int ROLE_COUNT = 3;

	// One ecologically-meaningful tolerance range - see the header note
	// on why units are left as-reported rather than pre-converted.
	struct Range {
		float min = 0.0f;
		float max = 0.0f;
		std::string unit;
		// Overrides the owning SpeciesEntry's defaultSource for this one
		// parameter specifically, if a role's three ranges come from
		// different papers. Empty means "use defaultSource."
		std::string sourceOverride;
	};

	struct SpeciesEntry {
		std::string roleName;    // "pioneer" / "intermediate" / "climax"
		std::string channel;     // "fruit" / "nut" / "shrub" - the density channel this role maps to
		std::string speciesName; // real species name - empty in the designer preset
		std::string defaultSource;
		Range moisture;
		Range slope;
		Range soilDepth;
	};

	// Designer = data-free: values chosen for the fiction, no published
	// source. Published = data-informed: real species + field-study
	// citations per parameter. This enum IS the axis section 1.5 of the
	// thesis describes, not just a label for it - see the header note.
	enum class Preset { Designer, Published };

	// Loads bin/data/settings/ecologyParamsDesigner.xml or
	// .../ecologyParamsPublished.xml. Returns false (and leaves
	// whatever was previously loaded untouched) if the file is missing
	// or malformed, so a bad/incomplete file can't silently blank out a
	// working parameter set mid-session.
	bool load(Preset preset);

	Preset getActivePreset() const { return activePreset; }
	bool isLoaded() const { return loaded; }
	std::array<SpeciesEntry, ROLE_COUNT> const& getSpecies() const { return species; }

	// Renders into whatever ImGui window is already open (no Begin/End
	// of its own) - VegetationField::drawGui() calls this inline as a
	// new section of the existing Vegetation panel, matching this
	// project's established single-panel convention rather than opening
	// a second window.
	void drawGui();

private:
	std::string filePathForPreset(Preset preset) const;
	// Shared by load() for all three fixed role tags (ROLE_PIONEER/
	// ROLE_INTERMEDIATE/ROLE_CLIMAX) - returns false if the tag is
	// missing entirely, matching load()'s own "malformed file changes
	// nothing" contract. Takes the already-fetched ECOLOGYPARAMS root
	// node (ofXml's getChild() return type) rather than the whole
	// document, so it can be called once per fixed role tag.
	bool loadRole(ofXml & root, char const * tagName, SpeciesEntry & outEntry) const;

	Preset activePreset = Preset::Designer;
	bool loaded = false;
	std::array<SpeciesEntry, ROLE_COUNT> species;
};
