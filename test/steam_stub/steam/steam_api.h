// Minimal stub of the Steamworks API surface newtonia uses, with
// signatures copied verbatim from the real SDK headers — syntax-checks
// the STEAM_BUILD code paths without the proprietary SDK.
#pragma once
#include <stdint.h>
// The SDK's steamtypes.h fixed-width aliases (a subset).
typedef uint8_t uint8;
typedef uint32_t uint32;
typedef uint64_t uint64;
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
// Minimal stand-in for the SDK's CCallback registration helper — enough to
// syntax-check the backends' member-callback wiring without the real SDK.
template <class T, class P>
class CCallback {
public:
  CCallback( T *pObj, void ( T::*func )( P * ) ) { (void)pObj; (void)func; }
};
// ---- ISteamInput (steam_input.cpp) — the action-set surface of
// isteaminput.h, signatures verbatim; the origin enum carries the members
// the origin -> position table names (values are irrelevant to a syntax
// check, but the NAMES must match the SDK's — re-check on a bump). ----
#define STEAM_INPUT_MAX_COUNT 16
#define STEAM_INPUT_MAX_ORIGINS 8
typedef uint64 InputHandle_t;
typedef uint64 InputActionSetHandle_t;
typedef uint64 InputDigitalActionHandle_t;
typedef uint64 InputAnalogActionHandle_t;
enum EInputSourceMode { k_EInputSourceMode_None, k_EInputSourceMode_JoystickMove };
enum EInputActionOrigin {
  k_EInputActionOrigin_None,
  k_EInputActionOrigin_SteamController_A, k_EInputActionOrigin_SteamController_B,
  k_EInputActionOrigin_SteamController_X, k_EInputActionOrigin_SteamController_Y,
  k_EInputActionOrigin_SteamController_LeftBumper, k_EInputActionOrigin_SteamController_RightBumper,
  k_EInputActionOrigin_SteamController_Start, k_EInputActionOrigin_SteamController_Back,
  k_EInputActionOrigin_SteamController_LeftTrigger_Pull, k_EInputActionOrigin_SteamController_LeftTrigger_Click,
  k_EInputActionOrigin_SteamController_RightTrigger_Pull, k_EInputActionOrigin_SteamController_RightTrigger_Click,
  k_EInputActionOrigin_SteamController_LeftStick_Move, k_EInputActionOrigin_SteamController_LeftStick_Click,
  k_EInputActionOrigin_PS4_X, k_EInputActionOrigin_PS4_Circle, k_EInputActionOrigin_PS4_Triangle, k_EInputActionOrigin_PS4_Square,
  k_EInputActionOrigin_PS4_LeftBumper, k_EInputActionOrigin_PS4_RightBumper,
  k_EInputActionOrigin_PS4_Options, k_EInputActionOrigin_PS4_Share,
  k_EInputActionOrigin_PS4_LeftTrigger_Pull, k_EInputActionOrigin_PS4_LeftTrigger_Click,
  k_EInputActionOrigin_PS4_RightTrigger_Pull, k_EInputActionOrigin_PS4_RightTrigger_Click,
  k_EInputActionOrigin_PS4_LeftStick_Move, k_EInputActionOrigin_PS4_LeftStick_Click,
  k_EInputActionOrigin_PS4_RightStick_Move, k_EInputActionOrigin_PS4_RightStick_Click,
  k_EInputActionOrigin_PS4_DPad_North, k_EInputActionOrigin_PS4_DPad_South,
  k_EInputActionOrigin_PS4_DPad_West, k_EInputActionOrigin_PS4_DPad_East,
  k_EInputActionOrigin_XBoxOne_A, k_EInputActionOrigin_XBoxOne_B, k_EInputActionOrigin_XBoxOne_X, k_EInputActionOrigin_XBoxOne_Y,
  k_EInputActionOrigin_XBoxOne_LeftBumper, k_EInputActionOrigin_XBoxOne_RightBumper,
  k_EInputActionOrigin_XBoxOne_Menu, k_EInputActionOrigin_XBoxOne_View,
  k_EInputActionOrigin_XBoxOne_LeftTrigger_Pull, k_EInputActionOrigin_XBoxOne_LeftTrigger_Click,
  k_EInputActionOrigin_XBoxOne_RightTrigger_Pull, k_EInputActionOrigin_XBoxOne_RightTrigger_Click,
  k_EInputActionOrigin_XBoxOne_LeftStick_Move, k_EInputActionOrigin_XBoxOne_LeftStick_Click,
  k_EInputActionOrigin_XBoxOne_RightStick_Move, k_EInputActionOrigin_XBoxOne_RightStick_Click,
  k_EInputActionOrigin_XBoxOne_DPad_North, k_EInputActionOrigin_XBoxOne_DPad_South,
  k_EInputActionOrigin_XBoxOne_DPad_West, k_EInputActionOrigin_XBoxOne_DPad_East,
  k_EInputActionOrigin_XBox360_A, k_EInputActionOrigin_XBox360_B, k_EInputActionOrigin_XBox360_X, k_EInputActionOrigin_XBox360_Y,
  k_EInputActionOrigin_XBox360_LeftBumper, k_EInputActionOrigin_XBox360_RightBumper,
  k_EInputActionOrigin_XBox360_Start, k_EInputActionOrigin_XBox360_Back,
  k_EInputActionOrigin_XBox360_LeftTrigger_Pull, k_EInputActionOrigin_XBox360_LeftTrigger_Click,
  k_EInputActionOrigin_XBox360_RightTrigger_Pull, k_EInputActionOrigin_XBox360_RightTrigger_Click,
  k_EInputActionOrigin_XBox360_LeftStick_Move, k_EInputActionOrigin_XBox360_LeftStick_Click,
  k_EInputActionOrigin_XBox360_RightStick_Move, k_EInputActionOrigin_XBox360_RightStick_Click,
  k_EInputActionOrigin_XBox360_DPad_North, k_EInputActionOrigin_XBox360_DPad_South,
  k_EInputActionOrigin_XBox360_DPad_West, k_EInputActionOrigin_XBox360_DPad_East,
  k_EInputActionOrigin_Switch_A, k_EInputActionOrigin_Switch_B, k_EInputActionOrigin_Switch_X, k_EInputActionOrigin_Switch_Y,
  k_EInputActionOrigin_Switch_LeftBumper, k_EInputActionOrigin_Switch_RightBumper,
  k_EInputActionOrigin_Switch_Plus, k_EInputActionOrigin_Switch_Minus,
  k_EInputActionOrigin_Switch_LeftTrigger_Pull, k_EInputActionOrigin_Switch_LeftTrigger_Click,
  k_EInputActionOrigin_Switch_RightTrigger_Pull, k_EInputActionOrigin_Switch_RightTrigger_Click,
  k_EInputActionOrigin_Switch_LeftStick_Move, k_EInputActionOrigin_Switch_LeftStick_Click,
  k_EInputActionOrigin_Switch_RightStick_Move, k_EInputActionOrigin_Switch_RightStick_Click,
  k_EInputActionOrigin_Switch_DPad_North, k_EInputActionOrigin_Switch_DPad_South,
  k_EInputActionOrigin_Switch_DPad_West, k_EInputActionOrigin_Switch_DPad_East,
  k_EInputActionOrigin_PS5_X, k_EInputActionOrigin_PS5_Circle, k_EInputActionOrigin_PS5_Triangle, k_EInputActionOrigin_PS5_Square,
  k_EInputActionOrigin_PS5_LeftBumper, k_EInputActionOrigin_PS5_RightBumper,
  k_EInputActionOrigin_PS5_Option, k_EInputActionOrigin_PS5_Create,
  k_EInputActionOrigin_PS5_LeftTrigger_Pull, k_EInputActionOrigin_PS5_LeftTrigger_Click,
  k_EInputActionOrigin_PS5_RightTrigger_Pull, k_EInputActionOrigin_PS5_RightTrigger_Click,
  k_EInputActionOrigin_PS5_LeftStick_Move, k_EInputActionOrigin_PS5_LeftStick_Click,
  k_EInputActionOrigin_PS5_RightStick_Move, k_EInputActionOrigin_PS5_RightStick_Click,
  k_EInputActionOrigin_PS5_DPad_North, k_EInputActionOrigin_PS5_DPad_South,
  k_EInputActionOrigin_PS5_DPad_West, k_EInputActionOrigin_PS5_DPad_East,
  k_EInputActionOrigin_SteamDeck_A, k_EInputActionOrigin_SteamDeck_B, k_EInputActionOrigin_SteamDeck_X, k_EInputActionOrigin_SteamDeck_Y,
  k_EInputActionOrigin_SteamDeck_L1, k_EInputActionOrigin_SteamDeck_R1,
  k_EInputActionOrigin_SteamDeck_Menu, k_EInputActionOrigin_SteamDeck_View,
  k_EInputActionOrigin_SteamDeck_L2_SoftPull, k_EInputActionOrigin_SteamDeck_L2,
  k_EInputActionOrigin_SteamDeck_R2_SoftPull, k_EInputActionOrigin_SteamDeck_R2,
  k_EInputActionOrigin_SteamDeck_LeftStick_Move, k_EInputActionOrigin_SteamDeck_L3,
  k_EInputActionOrigin_SteamDeck_RightStick_Move, k_EInputActionOrigin_SteamDeck_R3,
  k_EInputActionOrigin_SteamDeck_DPad_North, k_EInputActionOrigin_SteamDeck_DPad_South,
  k_EInputActionOrigin_SteamDeck_DPad_West, k_EInputActionOrigin_SteamDeck_DPad_East,
  k_EInputActionOrigin_Count,
};
enum ESteamInputType {
  k_ESteamInputType_Unknown, k_ESteamInputType_SteamController,
  k_ESteamInputType_XBox360Controller, k_ESteamInputType_XBoxOneController,
  k_ESteamInputType_GenericGamepad, k_ESteamInputType_PS4Controller,
  k_ESteamInputType_AppleMFiController, k_ESteamInputType_AndroidController,
  k_ESteamInputType_SwitchJoyConPair, k_ESteamInputType_SwitchJoyConSingle,
  k_ESteamInputType_SwitchProController, k_ESteamInputType_MobileTouch,
  k_ESteamInputType_PS3Controller, k_ESteamInputType_PS5Controller,
  k_ESteamInputType_SteamDeckController, k_ESteamInputType_Count,
};
struct InputAnalogActionData_t { EInputSourceMode eMode; float x, y; bool bActive; };
struct InputDigitalActionData_t { bool bState; bool bActive; };
class ISteamInput {
public:
  virtual bool Init( bool bExplicitlyCallRunFrame ) = 0;
  virtual bool Shutdown() = 0;
  virtual bool SetInputActionManifestFilePath( const char *pchInputActionManifestAbsolutePath ) = 0;
  virtual void RunFrame( bool bReservedValue = true ) = 0;
  virtual int GetConnectedControllers( InputHandle_t *handlesOut ) = 0;
  virtual InputActionSetHandle_t GetActionSetHandle( const char *pszActionSetName ) = 0;
  virtual void ActivateActionSet( InputHandle_t inputHandle, InputActionSetHandle_t actionSetHandle ) = 0;
  virtual InputActionSetHandle_t GetCurrentActionSet( InputHandle_t inputHandle ) = 0;
  virtual InputDigitalActionHandle_t GetDigitalActionHandle( const char *pszActionName ) = 0;
  virtual InputDigitalActionData_t GetDigitalActionData( InputHandle_t inputHandle, InputDigitalActionHandle_t digitalActionHandle ) = 0;
  virtual int GetDigitalActionOrigins( InputHandle_t inputHandle, InputActionSetHandle_t actionSetHandle, InputDigitalActionHandle_t digitalActionHandle, EInputActionOrigin *originsOut ) = 0;
  virtual InputAnalogActionHandle_t GetAnalogActionHandle( const char *pszActionName ) = 0;
  virtual InputAnalogActionData_t GetAnalogActionData( InputHandle_t inputHandle, InputAnalogActionHandle_t analogActionHandle ) = 0;
  virtual int GetAnalogActionOrigins( InputHandle_t inputHandle, InputActionSetHandle_t actionSetHandle, InputAnalogActionHandle_t analogActionHandle, EInputActionOrigin *originsOut ) = 0;
  virtual const char *GetStringForActionOrigin( EInputActionOrigin eOrigin ) = 0;
  virtual bool ShowBindingPanel( InputHandle_t inputHandle ) = 0;
  virtual ESteamInputType GetInputTypeForHandle( InputHandle_t inputHandle ) = 0;
};
inline ISteamInput *SteamInput() { return nullptr; }
inline bool SteamAPI_Init() { return false; }
inline void SteamAPI_Shutdown() {}
inline void SteamAPI_RunCallbacks() {}
inline ISteamUtils *SteamUtils() { return nullptr; }
inline ISteamApps *SteamApps() { return nullptr; }
inline ISteamFriends *SteamFriends() { return nullptr; }
inline ISteamUser *SteamUser() { return nullptr; }
