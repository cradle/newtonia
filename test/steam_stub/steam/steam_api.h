// Minimal stub of the Steamworks API surface newtonia uses, with
// signatures copied verbatim from the real SDK headers — syntax-checks
// the STEAM_BUILD code paths without the proprietary SDK.
#pragma once
enum EFloatingGamepadTextInputMode {
  k_EFloatingGamepadTextInputModeModeSingleLine = 0,
  k_EFloatingGamepadTextInputModeModeMultipleLines = 1,
  k_EFloatingGamepadTextInputModeModeEmail = 2,
  k_EFloatingGamepadTextInputModeModeNumeric = 3,
};
class ISteamUtils {
public:
  virtual bool ShowFloatingGamepadTextInput( EFloatingGamepadTextInputMode eKeyboardMode, int nTextFieldXPosition, int nTextFieldYPosition, int nTextFieldWidth, int nTextFieldHeight ) = 0;
  virtual bool DismissFloatingGamepadTextInput() = 0;
};
class ISteamApps {
public:
  virtual bool GetCurrentBetaName( char *pchName, int cchNameBufferSize ) = 0;
};
inline bool SteamAPI_Init() { return false; }
inline void SteamAPI_Shutdown() {}
inline void SteamAPI_RunCallbacks() {}
inline ISteamUtils *SteamUtils() { return nullptr; }
inline ISteamApps *SteamApps() { return nullptr; }
