#pragma once

struct Options {
    float keyboard_sensitivity = 1.0f;  // rotation speed multiplier for keyboard input
};

extern Options g_options;
Options load_options();
void save_options(const Options &opts);
