#include "EcologyParams.h"
#include "ofxImGui.h"

std::string EcologyParams::filePathForPreset(Preset preset) const
{
	// Same "settings/<name>.xml" relative-path convention
	// SandSurfaceRenderer::loadSettings()/saveSettings() already use -
	// see that file for the precedent this mirrors.
	return (preset == Preset::Designer)
		? "settings/ecologyParamsDesigner.xml"
		: "settings/ecologyParamsPublished.xml";
}

bool EcologyParams::loadRole(ofXml & root, char const * tagName, SpeciesEntry & outEntry) const
{
	auto roleNode = root.getChild(tagName);
	if (!roleNode)
		return false;

	std::string tag(tagName);
	outEntry.roleName = (tag == "ROLE_PIONEER") ? "pioneer"
		: (tag == "ROLE_INTERMEDIATE") ? "intermediate"
		: "climax";
	outEntry.channel = roleNode.getChild("CHANNEL").getValue();
	outEntry.speciesName = roleNode.getChild("SPECIESNAME").getValue();
	outEntry.defaultSource = roleNode.getChild("SOURCE").getValue();

	outEntry.moisture.min = roleNode.getChild("MOISTUREMIN").getFloatValue();
	outEntry.moisture.max = roleNode.getChild("MOISTUREMAX").getFloatValue();
	outEntry.moisture.unit = roleNode.getChild("MOISTUREUNIT").getValue();
	outEntry.moisture.sourceOverride = roleNode.getChild("MOISTURESOURCE").getValue();

	outEntry.slope.min = roleNode.getChild("SLOPEMIN").getFloatValue();
	outEntry.slope.max = roleNode.getChild("SLOPEMAX").getFloatValue();
	outEntry.slope.unit = roleNode.getChild("SLOPEUNIT").getValue();
	outEntry.slope.sourceOverride = roleNode.getChild("SLOPESOURCE").getValue();

	outEntry.soilDepth.min = roleNode.getChild("SOILDEPTHMIN").getFloatValue();
	outEntry.soilDepth.max = roleNode.getChild("SOILDEPTHMAX").getFloatValue();
	outEntry.soilDepth.unit = roleNode.getChild("SOILDEPTHUNIT").getValue();
	outEntry.soilDepth.sourceOverride = roleNode.getChild("SOILDEPTHSOURCE").getValue();

	return true;
}

bool EcologyParams::load(Preset preset)
{
	std::string path = filePathForPreset(preset);

	ofXml xml;
	if (!xml.load(path))
		return false;
	auto root = xml.getChild("ECOLOGYPARAMS");
	if (!root)
		return false;

	// Parsed into a temporary first, committed only on full success -
	// see the header note's "malformed file changes nothing" contract.
	// Without this, a file missing e.g. ROLE_CLIMAX would leave
	// `species[2]` holding stale data from whatever preset was loaded
	// before, silently mixing two presets together.
	std::array<SpeciesEntry, ROLE_COUNT> parsed;
	if (!loadRole(root, "ROLE_PIONEER", parsed[0])) return false;
	if (!loadRole(root, "ROLE_INTERMEDIATE", parsed[1])) return false;
	if (!loadRole(root, "ROLE_CLIMAX", parsed[2])) return false;

	species = parsed;
	activePreset = preset;
	loaded = true;
	return true;
}

void EcologyParams::drawGui()
{
	ImGui::Separator();
	ImGui::Text("Ecological data source (succession-model phase, Phase 1 of the");
	ImGui::Text("2026-10-14 contribution alignment - see CLAUDE.md). This section");
	ImGui::Text("is NOT YET READ BY ANY GROWTH LOGIC - it only proves the sourced-");
	ImGui::Text("parameter/preset-switch mechanism works; Phase 3 wires moisture/");
	ImGui::Text("slope/soil-depth fields and a real suitability test into it.");

	if (!loaded) {
		ImGui::Text("No ecology parameter file loaded yet.");
		if (ImGui::Button("Load designer preset")) load(Preset::Designer);
		ImGui::SameLine();
		if (ImGui::Button("Load published preset")) load(Preset::Published);
		return;
	}

	ImGui::Text("Active preset: %s", activePreset == Preset::Designer
		? "DESIGNER (data-free - no published source)"
		: "PUBLISHED (data-informed - real species + field-study citations)");
	if (ImGui::Button("Switch to designer preset")) load(Preset::Designer);
	ImGui::SameLine();
	if (ImGui::Button("Switch to published preset")) load(Preset::Published);
	ImGui::SameLine();
	if (ImGui::Button("Reload current preset from disk")) load(activePreset);

	auto drawRange = [](char const * label, Range const & r) {
		std::string line = "  " + std::string(label) + ": " + ofToString(r.min) + " - " + ofToString(r.max);
		if (!r.unit.empty())
			line += " " + r.unit;
		if (!r.sourceOverride.empty())
			line += "  [source: " + r.sourceOverride + "]";
		ImGui::Text("%s", line.c_str());
	};

	for (auto const & s : species) {
		std::string header = s.roleName + " (channel: " + s.channel + ")";
		if (!s.speciesName.empty())
			header += " - " + s.speciesName;
		ImGui::Text("%s", header.c_str());
		ImGui::Text("  Source: %s", s.defaultSource.empty() ? "(none)" : s.defaultSource.c_str());
		drawRange("Moisture", s.moisture);
		drawRange("Slope", s.slope);
		drawRange("Soil depth", s.soilDepth);
		ImGui::Separator();
	}
}
