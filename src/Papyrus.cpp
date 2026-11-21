#include "Papyrus.h"

// The base class's destructor is declared in CommonLibF4 but not defined there; our functor needs one to link.
RE::BSScript::IStackCallbackFunctor::~IStackCallbackFunctor() = default;

namespace mg::Papyrus
{
	namespace
	{
		// logs what a call came to (on the script thread)
		class LogResult final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit LogResult(std::string a_what) :
				_what(std::move(a_what))
			{}

			void CallQueued() override {}
			void CallCanceled() override { log::info("papyrus {}: canceled", _what); }
			void StartMultiDispatch() override {}
			void EndMultiDispatch() override {}
			void operator()(RE::BSScript::Variable) override {}

		private:
			std::string _what;
		};

		// Calls a Papyrus function on an actor's script object (Playable Pool's way)
		template <class... Args>
		bool CallActor(RE::Actor* a_actor, const char* a_function, Args... a_args)
		{
			auto* game = RE::GameVM::GetSingleton();
			auto  vm = game ? game->GetVM() : nullptr;
			if (!vm || !a_actor) {
				return false;
			}
			auto&      handles = vm->GetObjectHandlePolicy();
			const auto handle = handles.GetHandleForObject(RE::BSScript::kActor, a_actor);
			if (handle == handles.EmptyHandle()) {
				log::warn("{}: no script handle for {:08X}", a_function, a_actor->GetFormID());
				return false;
			}
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new LogResult(a_function) };
			const RE::BSFixedString script{ "Actor" };
			const RE::BSFixedString function{ a_function };
			return vm->DispatchMethodCall(
				handle, script, function,
				[&](RE::BSScrapArray<RE::BSScript::Variable>& a_out) {
					a_out = RE::BSScript::detail::PackVariables(a_args...);
					return true;
				},
				callback);
		}
	}

	bool SetPosition(RE::Actor* a_actor, const RE::NiPoint3& a_pos)
	{
		return CallActor(a_actor, "SetPosition", a_pos.x, a_pos.y, a_pos.z);
	}

	bool SetAngle(RE::Actor* a_actor, float a_x, float a_y, float a_z)
	{
		return CallActor(a_actor, "SetAngle", a_x, a_y, a_z);
	}

	bool SetRestrained(RE::Actor* a_actor, bool a_restrained)
	{
		return CallActor(a_actor, "SetRestrained", a_restrained);
	}

	bool EvaluatePackage(RE::Actor* a_actor)
	{
		return CallActor(a_actor, "EvaluatePackage");
	}
}
