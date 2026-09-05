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
// Posted when the Deck's floating keyboard is dismissed — steam_keyboard.cpp.
struct FloatingGamepadTextInputDismissed_t { int m_unused; };
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
// Steam Input surface used by steam_input.cpp: the physical pad type
// behind a Steam-emulated controller, for the hint glyphs.
typedef uint64_t uint64;
typedef uint64 InputHandle_t;
#define STEAM_INPUT_MAX_COUNT 16
enum ESteamInputType {
  k_ESteamInputType_Unknown = 0,
  k_ESteamInputType_SteamController = 1,
  k_ESteamInputType_XBox360Controller = 2,
  k_ESteamInputType_XBoxOneController = 3,
  k_ESteamInputType_GenericGamepad = 4,
  k_ESteamInputType_PS4Controller = 5,
  k_ESteamInputType_AppleMFiController = 6,
  k_ESteamInputType_AndroidController = 7,
  k_ESteamInputType_SwitchJoyConPair = 8,
  k_ESteamInputType_SwitchJoyConSingle = 9,
  k_ESteamInputType_SwitchProController = 10,
  k_ESteamInputType_MobileTouch = 11,
  k_ESteamInputType_PS3Controller = 12,
  k_ESteamInputType_PS5Controller = 13,
  k_ESteamInputType_SteamDeckController = 14,
  k_ESteamInputType_Count = 15,
  k_ESteamInputType_MaximumPossibleValue = 255,
};
class ISteamInput {
public:
  virtual bool Init( bool bExplicitlyCallRunFrame ) = 0;
  virtual bool Shutdown() = 0;
  virtual void RunFrame( bool bReservedValue = true ) = 0;
  virtual int GetConnectedControllers( InputHandle_t *handlesOut ) = 0;
  virtual ESteamInputType GetInputTypeForHandle( InputHandle_t inputHandle ) = 0;
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
inline ISteamInput *SteamInput() { return nullptr; }
