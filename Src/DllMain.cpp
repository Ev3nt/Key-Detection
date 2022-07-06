#include <Windows.h>
#include <detours.h>
#include <map>
#include <vector>

#define AttachDetour(pointer, detour) (DetourUpdateThread(GetCurrentThread()), DetourAttach(&(PVOID&)pointer, detour))
#define DetachDetour(pointer, detour) (DetourUpdateThread(GetCurrentThread()), DetourDetach(&(PVOID&)pointer, detour))

UINT_PTR gameBase = (UINT_PTR)GetModuleHandle("game.dll");	
WNDPROC wndProc = NULL;
HMODULE thismodule = NULL;

std::map<WPARAM, bool> isKeyDown;
std::map<WPARAM, std::map<bool, std::vector<UINT>>> keyEvents;
std::vector<UINT> mouseWheelTriggers;
int mouseWheelDelta = NULL;

UINT_PTR pCGxDeviceD3D = gameBase + 0xacbd40; // 0x574 = [349] - wnd
UINT_PTR pCGameWar3 = gameBase + 0xab65f4; // 0xc4 = [49]

//auto TriggerEvaluate = (BOOL(__cdecl*)(UINT))(gameBase + 0x3c3f80);
auto TriggerEvaluate = (BOOL(__thiscall*)(UINT))(gameBase + 0x448320);
//auto TriggerExecute = (BOOL(__cdecl*)(UINT))(gameBase + 0x3c3f40);
auto TriggerExecute = (BOOL(__thiscall*)(UINT, UINT))(gameBase + 0x4482d0);
auto SetJassState = (void(__thiscall*)(BOOL jassState))(gameBase + 0x2ab0e0);

HRESULT CALLBACK WndProcCustom(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam);

void __fastcall SetJassStateCustom(BOOL jassState);

bool IsInGame();

bool ValidVersion();

extern "C" void __stdcall TriggerRegisterKeyEvent(UINT trigger, WPARAM key, bool keyevent) {
	if (trigger) {
		keyEvents[key][keyevent].push_back(trigger);
	}
}

extern "C" void __stdcall TriggerRegisterMouseWheelEvent(UINT trigger) {
	mouseWheelTriggers.push_back(trigger);
}

extern "C" int __stdcall GetWheelDelta() {
	return mouseWheelDelta;
}

//---------------------------------------------------

BOOL APIENTRY DllMain(HMODULE module, UINT reason, LPVOID reserved) {
	switch (reason) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(module);

		if (!gameBase || !ValidVersion()) {
			return FALSE;
		}

		thismodule = module;

		DetourTransactionBegin();

		wndProc = (WNDPROC)SetWindowLong((*(HWND**)pCGxDeviceD3D)[349], GWL_WNDPROC, (LONG)WndProcCustom);
		
		AttachDetour(SetJassState, SetJassStateCustom);

		DetourTransactionCommit();

		break;
	case DLL_PROCESS_DETACH:
		DetourTransactionBegin();

		SetWindowLong((*(HWND**)pCGxDeviceD3D)[349], GWL_WNDPROC, (LONG)wndProc);

		DetachDetour(SetJassState, SetJassStateCustom);

		DetourTransactionCommit();

		break;
	}
	return TRUE;
}

//---------------------------------------------------

HRESULT CALLBACK WndProcCustom(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (IsInGame()) {
		if (msg == WM_KEYDOWN || msg == WM_KEYUP) {
			bool keyevent = (msg - 0x100) != FALSE;
			bool& isDown = isKeyDown[wParam];

			if (keyevent == isDown) {
				auto& triggers = keyEvents[wParam][keyevent];
				isDown = !keyevent;

				for (const UINT trigger : triggers) {
					if (TriggerEvaluate(trigger)) {
						TriggerExecute(trigger, 0);
					}
				}
			}
		}
		else if (msg == WM_MOUSEWHEEL) {
			mouseWheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);

			for (const UINT trigger : mouseWheelTriggers) {
				if (TriggerEvaluate(trigger)) {
					TriggerExecute(trigger, 0);
				}
			}
		}
	}

	return CallWindowProc(wndProc, wnd, msg, wParam, lParam);
}

void __fastcall SetJassStateCustom(BOOL jassState) {
	if (jassState == TRUE && thismodule) {
		CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)FreeLibrary, thismodule, NULL, NULL);
	}

	return SetJassState(jassState);
}

//---------------------------------------------------

bool IsInGame() {
	return *(UINT*)pCGameWar3 && (*(UINT**)pCGameWar3)[49];
}

bool ValidVersion() {
	DWORD handle;
	DWORD size = GetFileVersionInfoSize("game.dll", &handle);

	LPSTR buffer = new char[size];
	GetFileVersionInfo("game.dll", handle, size, buffer);

	VS_FIXEDFILEINFO* verInfo;
	size = sizeof(VS_FIXEDFILEINFO);
	VerQueryValue(buffer, "\\", (LPVOID*)&verInfo, (UINT*)&size);
	delete[] buffer;

	return (((verInfo->dwFileVersionMS >> 16) & 0xffff) == 1 && (verInfo->dwFileVersionMS & 0xffff) == 26 && ((verInfo->dwFileVersionLS >> 16) & 0xffff) == 0 && ((verInfo->dwFileVersionLS >> 0) & 0xffff) == 6401);
}