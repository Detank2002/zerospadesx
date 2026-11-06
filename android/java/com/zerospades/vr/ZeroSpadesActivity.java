package com.zerospades.vr;

import org.libsdl.app.SDLActivity;

/**
 * The game's activity: SDL's own, with SDL linked into libmain.so rather than
 * a libSDL2.so of its own, so the libraries to load are OpenAL and the game.
 */
public class ZeroSpadesActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"openal", "main"};
    }
}
