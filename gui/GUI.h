#pragma once
#include "../dxsdk/include/d3d8.h"

class GUI
{
public:
    // Initializes the GUI subsystem with the host application's device
    static void Start(LPDIRECT3DDEVICE8 device);

    // Invoked every frame (from EndScene hooks) to draw the GUI
    static void Render();
    static void RenderGreeting();

    // Call this to clean up when your DLL is detaching
    static void Shutdown();

    // Notify GUI about device loss/reset events so resources can be recreated
    static void OnDeviceLost();
    static void OnDeviceReset(LPDIRECT3DDEVICE8 device);

    // Toggles the GUI's visibility
    static void Toggle();
    static void ToggleGreeting();

    // Returns true if the GUI is currently visible
    static bool isVisible;
    static bool isGreeting;

    // Returns true when the GUI has been initialized with a valid device
    static bool isInitialized;

private:
    static bool GetCursorPosition(POINT& cursorViewport);
    static void DrawGuiContent(const RECT& viewport, bool hasCursorPosition, const POINT& cursorPosition,
        bool leftMouseDown, bool mousePressedThisFrame);
    static void DrawGreetingContent();

    static bool ShouldRedrawGui();
    static bool ShouldRedrawGreeting();
};
