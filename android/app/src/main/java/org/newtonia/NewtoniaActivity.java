package org.newtonia;

// NewtoniaActivity extends SDL's SDLActivity.
// SDLActivity sets up the EGL surface, event loop bridge, and calls
// SDL_main() in the native library.

import org.libsdl.app.SDLActivity;

public class NewtoniaActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        // SDL2 and SDL2_mixer must be listed before the game library so they
        // are loaded first by the class loader.
        return new String[]{
            "SDL2",
            "SDL2_mixer",
            "newtonia"      // our CMake target
        };
    }

    @Override
    protected String getMainSharedObject() {
        return "libnewtonia.so";
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }
}
