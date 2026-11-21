#pragma once

#include "Settings.h"
#include "Util.h"

// fo4minigolf.esl's records (tools/make_esl.py) and the vanilla ones the plugin uses.
namespace mg::Forms
{
	inline constexpr std::uint32_t kWorkshopMenuID = 0x800;  // KYWD fo4minigolfWorkshopMenu: the Mini Golf workshop category
	inline constexpr std::uint32_t kCourseID = 0x801;        // ACTI fo4minigolfCourse: Meshes\fo4minigolf\Course.nif
	inline constexpr std::uint32_t kBallID = 0x803;          // STAT fo4minigolfBall
	inline constexpr std::uint32_t kPutterID = 0x804;        // STAT fo4minigolfPutter
	inline constexpr std::uint32_t kDotID = 0x805;           // STAT fo4minigolfDot: a guide dot

	inline constexpr RE::TESFormID kFurnitureTables = 0x00118F59;  // WorkshopRecipeFilterFurniture02Tables

	inline RE::TESForm* Own(std::uint32_t a_id) { return util::ResolveAnyForm(Settings::Get().plugin, a_id); }
	inline RE::TESBoundObject* Bound(std::uint32_t a_id) { return util::ResolveForm(Settings::Get().plugin, a_id); }

	inline RE::TESBoundObject* Course() { return Bound(kCourseID); }
	inline RE::TESBoundObject* Ball() { return Bound(kBallID); }
	inline RE::TESBoundObject* Putter() { return Bound(kPutterID); }
	inline RE::TESBoundObject* Dot() { return Bound(kDotID); }

	inline bool IsCourse(const RE::TESObjectREFR* a_ref)
	{
		auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
		return base && base == Course();
	}

	// the workshop menu's Mini Golf category: in the Furniture menu, right after Tables, each time the game's data is
	// loaded. (The menu's form lists aren't overridden in the plugin: that would undo other mods' changes to them.)
	// From Wasteland Wagers.
	inline void AddWorkshopMenu()
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		auto* ours = Own(kWorkshopMenuID);
		auto* tables = RE::TESForm::GetFormByID(kFurnitureTables);
		if (!data || !ours || !tables) {
			log::warn("workshop menu: the Mini Golf category ({}) or the Tables one ({}) isn't there", ours != nullptr, tables != nullptr);
			return;
		}
		int added = 0;
		for (auto* list : data->GetFormArray<RE::BGSListForm>()) {
			if (!list) {
				continue;
			}
			auto& forms = list->arrayOfForms;
			const auto at = std::ranges::find(forms, tables);
			if (at == forms.end() || std::ranges::find(forms, ours) != forms.end()) {
				continue;
			}
			forms.insert(at + 1, ours);
			++added;
			log::info("workshop menu: Mini Golf added to {:08X} (the Furniture menu, after Tables)", list->GetFormID());
		}
		if (added == 0) {
			log::warn("workshop menu: the Furniture menu (the list with Tables in it) wasn't found");
		}
	}
}
