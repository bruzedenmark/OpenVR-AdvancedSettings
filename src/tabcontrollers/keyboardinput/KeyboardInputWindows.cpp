#include "KeyboardInputWindows.h"
#include <Windows.h>
#include "easylogging++.h"

namespace advsettings
{
/*!
Represents the state of a keyboard button press. Can be either Up or Down.

Implemented because neither Qt nor Windows.h included a sensible key state enum.
*/
enum class KeyStatus
{
    Up,
    Down,
};

/*!
Fills out an INPUT struct ip with scanCode and keyup status.
*/
void fillKiStruct( INPUT& ip, WORD scanCode, bool keyup )
{
    ip.type = INPUT_KEYBOARD;
    ip.ki.wVk = scanCode;
    if ( keyup )
    {
        ip.ki.dwFlags = KEYEVENTF_KEYUP;
    }
    else
    {
        ip.ki.dwFlags = 0;
    }
    ip.ki.wScan = 0;
    ip.ki.dwExtraInfo = 0;
    ip.ki.time = 0;
};

/*!
Calls SendInput on an INPUT struct and has associated error handling.

inputCount can't be deduced from input because of implicit conversions from
arrays to pointers when passed as arguments.
*/
void sendKeyboardInputRaw( const int inputCount, const LPINPUT input )
{
    const auto success
        = SendInput( static_cast<UINT>( inputCount ), input, sizeof( INPUT ) );

    if ( ( inputCount > 0 ) && !success )
    {
        char* err;
        auto errCode = GetLastError();
        if ( !FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER
                                 | FORMAT_MESSAGE_FROM_SYSTEM,
                             nullptr,
                             errCode,
                             MAKELANGID( LANG_NEUTRAL,
                                         SUBLANG_DEFAULT ), // default language
                             reinterpret_cast<LPTSTR>( &err ),
                             0,
                             nullptr ) )
        {
            LOG( ERROR )
                << "Error calling SendInput(): Could not get error message ("
                << errCode << ")";
        }
        else
        {
            LOG( ERROR ) << "Error calling SendInput(): " << err;
        }
    }
}

/*!
Returns an INPUT struct with the corresponding virtualKeyCode and keyStatus set.

All fields except ki.wVk and ki.dwFlags are zero.
The INPUT struct is used with the SendInput Windows function.
Official Docs:
https://docs.microsoft.com/en-us/windows/desktop/api/winuser/ns-winuser-taginput
*/
INPUT createInputStruct( const WORD virtualKeyCode, const KeyStatus keyStatus )
{
    // Zero init to ensure random data doesn't muck something up.
    INPUT input = {};

    input.type = INPUT_KEYBOARD;

    input.ki.wVk = virtualKeyCode;

    if ( keyStatus == KeyStatus::Up )
    {
        input.ki.dwFlags = KEYEVENTF_KEYUP;
    }
    else if ( keyStatus == KeyStatus::Down )
    {
        // Struct is already zero initialized, but this is here for clarity.
        // Compiler will likely sort this out, otherwise the performance hit is
        // negligible.
        // There is no corresponding KEYDOWN event, you just don't
        // set KEYEVENTF_KEYUP.
        input.ki.dwFlags = 0;
    }

    // The sizeof(INPUT) on MSVC is 28. Returning by value is a non-issue
    // compared to the simplicity of not having to pass a ref to an already
    // existing INPUT struct, and possibly forgetting to zero it out.
    return input;
}

/*!
Sends a key press to Windows. The virtualKeyCode is a Windows specific define
found in <windows.h>.

Virtual Key Codes offical docs:
https://docs.microsoft.com/en-us/windows/desktop/inputdev/virtual-key-codes
*/
void sendKeyPressAndRelease( const WORD virtualKeyCode )
{
    const auto press = createInputStruct( virtualKeyCode, KeyStatus::Down );
    const auto release = createInputStruct( virtualKeyCode, KeyStatus::Up );

    constexpr auto numberOfActions = 2;

    INPUT actions[numberOfActions] = { press, release };

    sendKeyboardInputRaw( numberOfActions, actions );
}

void KeyboardInputWindows::sendKeyboardInput( QString input )
{
    int len = input.length();
    if ( len > 0 )
    {
        LPINPUT ips = new INPUT[static_cast<unsigned int>( len ) * 5 + 3];
        int ii = 0;
        bool shiftPressed = false;
        bool ctrlPressed = false;
        bool altPressed = false;
        for ( int i = 0; i < len; i++ )
        {
            short tmp = VkKeyScan( input.at( i ).toLatin1() );
            WORD c = tmp & 0xFF;
            short shiftState = tmp >> 8;
            bool isShift = shiftState & 1;
            bool isCtrl = shiftState & 2;
            bool isAlt = shiftState & 4;
            if ( isShift && !shiftPressed )
            {
                fillKiStruct( ips[ii++], VK_SHIFT, false );
                shiftPressed = true;
            }
            else if ( !isShift && shiftPressed )
            {
                fillKiStruct( ips[ii++], VK_SHIFT, true );
                shiftPressed = false;
            }
            if ( isCtrl && !ctrlPressed )
            {
                fillKiStruct( ips[ii++], VK_CONTROL, false );
                ctrlPressed = true;
            }
            else if ( !isCtrl && ctrlPressed )
            {
                fillKiStruct( ips[ii++], VK_CONTROL, true );
                ctrlPressed = false;
            }
            if ( isAlt && !altPressed )
            {
                fillKiStruct( ips[ii++], VK_MENU, false );
                altPressed = true;
            }
            else if ( !isAlt && altPressed )
            {
                fillKiStruct( ips[ii++], VK_MENU, true );
                altPressed = false;
            }
            fillKiStruct( ips[ii++], c, false );
            fillKiStruct( ips[ii++], c, true );
        }
        if ( shiftPressed )
        {
            fillKiStruct( ips[ii++], VK_SHIFT, true );
            shiftPressed = false;
        }
        if ( ctrlPressed )
        {
            fillKiStruct( ips[ii++], VK_CONTROL, true );
            ctrlPressed = false;
        }
        if ( altPressed )
        {
            fillKiStruct( ips[ii++], VK_MENU, true );
            altPressed = false;
        }

        sendKeyboardInputRaw( ii, ips );
        delete[] ips;
    }
}

/*!
Sends a single Enter keypress to Windows.
*/
void KeyboardInputWindows::sendKeyboardEnter()
{
    sendKeyPressAndRelease( VK_RETURN );
}

/*!
Sends count amount of backspaces to Windows. count of 0 or below will do
nothing.
*/
void KeyboardInputWindows::sendKeyboardBackspace( const int count )
{
    // We can only send a positive amount of key presses.
    if ( count <= 0 )
    {
        return;
    }

    for ( int presses = 0; presses < count; ++presses )
    {
        sendKeyPressAndRelease( VK_BACK );
    }
}

/*!
Sends an Alt + Tab combination to Windows.

Used for tabbing out of game windows blocking other programs on desktop while in
VR. At least one use case was tabbing out of a fullscreen game to be able to use
a music player.
*/
void KeyboardInputWindows::sendKeyboardAltTab()
{
    // VK_MENU is Alt.
    const auto pressAlt = createInputStruct( VK_MENU, KeyStatus::Down );
    const auto pressTab = createInputStruct( VK_TAB, KeyStatus::Down );
    const auto releaseAlt = createInputStruct( VK_MENU, KeyStatus::Up );
    const auto releaseTab = createInputStruct( VK_TAB, KeyStatus::Up );

    constexpr auto numberOfActions = 4;

    INPUT actions[numberOfActions]
        = { pressAlt, pressTab, releaseAlt, releaseTab };

    sendKeyboardInputRaw( numberOfActions, actions );
}

} // namespace advsettings
