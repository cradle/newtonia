// Stub of the Steamworks flat C API (steam/steam_api_flat.h) for the
// STEAM_BUILD syntax gate. Signatures copied from the real SDK header; add
// a function here before calling it from game code.
#pragma once
#include "steam_api.h"

extern "C" {
InputDigitalActionData_t SteamAPI_ISteamInput_GetDigitalActionData( ISteamInput* self, InputHandle_t inputHandle, InputDigitalActionHandle_t digitalActionHandle );
InputAnalogActionData_t SteamAPI_ISteamInput_GetAnalogActionData( ISteamInput* self, InputHandle_t inputHandle, InputAnalogActionHandle_t analogActionHandle );
}
