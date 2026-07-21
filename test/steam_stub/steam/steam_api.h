// Minimal stub of the Steamworks API surface newtonia uses, with
// signatures copied verbatim from the real SDK headers — syntax-checks
// the STEAM_BUILD code paths without the proprietary SDK.
#pragma once
#include <stdint.h>
// The SDK's steamtypes.h fixed-width aliases (a subset).
typedef uint8_t uint8;
typedef uint32_t uint32;
typedef uint32 HAuthTicket;
const HAuthTicket k_HAuthTicketInvalid = 0;
// EResult subset — only the OK we branch on (real enum has ~120 values).
enum EResult { k_EResultOK = 1 };
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
  virtual int GetLaunchCommandLine( char *pszCommandLine, int cubCommandLine ) = 0;
};
// Posted to a running game when Steam hands it a fresh launch command line
// (an API-delivered Join / steam:// while already open) — steam_invites.cpp.
struct NewUrlLaunchParameters_t { int m_unused; };
// Rich Presence surface used by steam_presence.cpp and steam_invites.cpp;
// persona name used by steam_identity.cpp.
class ISteamFriends {
public:
  virtual const char *GetPersonaName() = 0;
  virtual void SetRichPresence( const char *pchKey, const char *pchValue ) = 0;
  virtual void ClearRichPresence() = 0;
};
// Fired when a friend accepts an invite while the game is already running;
// m_rgchConnect carries the advertised "connect" string (steam_invites.cpp).
struct GameRichPresenceJoinRequested_t {
  char m_rgchConnect[256];
};
// Web-API auth ticket response (steam_identity_verify.cpp): posted when an
// async GetAuthTicketForWebApi() call completes, carrying the ticket bytes.
struct GetTicketForWebApiResponse_t {
  HAuthTicket m_hAuthTicket;
  EResult m_eResult;
  int m_cubTicket;
  uint8 m_rgubTicket[2560];
};
// User surface — the Web-API ticket mint used by the netplay identity
// verifier (steam_identity_verify.cpp).
class ISteamUser {
public:
  virtual HAuthTicket GetAuthTicketForWebApi( const char *pchIdentity ) = 0;
  virtual void CancelAuthTicket( HAuthTicket hAuthTicket ) = 0;
};
// Minimal stand-in for the SDK's CCallback registration helper — enough to
// syntax-check the backends' member-callback wiring without the real SDK.
template <class T, class P>
class CCallback {
public:
  CCallback( T *pObj, void ( T::*func )( P * ) ) { (void)pObj; (void)func; }
};
inline bool SteamAPI_Init() { return false; }
inline void SteamAPI_Shutdown() {}
inline void SteamAPI_RunCallbacks() {}
inline ISteamUtils *SteamUtils() { return nullptr; }
inline ISteamApps *SteamApps() { return nullptr; }
inline ISteamFriends *SteamFriends() { return nullptr; }
inline ISteamUser *SteamUser() { return nullptr; }
