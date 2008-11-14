#include "game.h"

Game::Game(float width, float height) {
  window_width = width;
  window_height = height;
}

void Game::step(float delta) {
  for(int i = 0; i < ships.size(); i++) {
    ships[i].step(delta);
  }
}

void Game::resize(int width, int height) {
  for(int i = 0; i < ships.size(); i++) {
    ships[i].set_world_size(width, height);
  }  
}

void Game::press(unsigned char key) {
  for(int i = 0; i < ships.size(); i++) {
    ships[i].press(key);
  }
}

void release(unsigned char key) {
  for(int i = 0; i < ships.size(); i++) {
    ships[i].release(key);
  }
}