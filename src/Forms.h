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

	// The workshop menu: Furniture > Evodube Games > Mini Golf. Evodube Games is a submenu shared by evodube's mods
	// (Wasteland Wagers, Amusement Games, Mini Golf): a form list whose first entry is the keyword naming it (as
	// vanilla's own submenus are), each mod carrying its own copy with the same editor ids. Here, when the game's data
	// is loaded: in each Furniture menu (the form list, or lists, with Tables in it), the Evodube Games already there
	// (another mod put it in) is found by its keyword's editor id and our category added to it; with none there yet,
	// ours goes in, right after Tables. The menu's lists aren't overridden in the .esl (that would undo other mods'
	// changes to them).
	inline constexpr std::uint32_t    kGamesMenuListID = 0x80A;  // FLST EvodubeGamesWorkshopMenuList
	inline constexpr std::string_view kGamesMenuEditorID = "EvodubeGamesWorkshopMenu";

	inline RE::BGSListForm* GamesSubmenuIn(RE::BGSListForm* a_menu)
	{
		for (auto* f : a_menu->arrayOfForms) {
			auto* sub = f ? f->As<RE::BGSListForm>() : nullptr;
			if (!sub || sub->arrayOfForms.empty() || !sub->arrayOfForms[0]) {
				continue;
			}
			auto* kw = sub->arrayOfForms[0]->As<RE::BGSKeyword>();
			if (kw && std::string_view(kw->formEditorID.c_str() ? kw->formEditorID.c_str() : "") == kGamesMenuEditorID) {
				return sub;
			}
		}
		return nullptr;
	}

	inline void AddWorkshopMenu()
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		auto* ours = Own(kWorkshopMenuID);
		auto* gamesForm = Own(kGamesMenuListID);
		auto* games = gamesForm ? gamesForm->As<RE::BGSListForm>() : nullptr;
		auto* tables = RE::TESForm::GetFormByID(kFurnitureTables);
		if (!data || !ours || !games || !tables) {
			log::warn("workshop menu: the Mini Golf category ({}), Evodube Games ({}) or Tables ({}) isn't there", ours != nullptr,
				games != nullptr, tables != nullptr);
			return;
		}
		int added = 0;
		for (auto* list : data->GetFormArray<RE::BGSListForm>()) {
			if (!list || list == games) {
				continue;
			}
			auto& forms = list->arrayOfForms;
			if (std::ranges::find(forms, tables) == forms.end()) {
				continue;
			}
			// (an older version put its category straight in Furniture: out of there, into Evodube Games)
			if (auto old = std::ranges::find(forms, ours); old != forms.end()) {
				forms.erase(old);
			}
			auto* sub = GamesSubmenuIn(list);
			if (!sub) {
				sub = games;
				forms.insert(std::ranges::find(forms, tables) + 1, games);
				log::info("workshop menu: Evodube Games added to {:08X} (the Furniture menu, after Tables)", list->GetFormID());
			}
			if (std::ranges::find(sub->arrayOfForms, ours) == sub->arrayOfForms.end()) {
				sub->arrayOfForms.push_back(ours);
			}
			log::info("workshop menu: Mini Golf in Evodube Games ({:08X}) in {:08X}", sub->GetFormID(), list->GetFormID());
			++added;
		}
		if (added == 0) {
			log::warn("workshop menu: the Furniture menu (the list with Tables in it) wasn't found: Evodube Games isn't in the workshop menu");
		}
	}
}
