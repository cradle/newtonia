#include "asteroid.h"
#include "wrapped_point.h"

#include <list>

using namespace std;

const int Asteroid::max_speed = 5;
const int Asteroid::max_rotation = 15;
int Asteroid::num_killable = 0;

const int Asteroid::radius_variation = 220;
const int Asteroid::minimum_radius = 20;

Mix_Chunk * Asteroid::explode_sound = NULL;
Mix_Chunk * Asteroid::thud_sound = NULL;

const int Asteroid::max_radius = Asteroid::radius_variation + Asteroid::minimum_radius;

Asteroid::Asteroid(bool invincible) : CompositeObject(), killed(false) {
  position = WrappedPoint();
  if(invincible) {
    radius = rand()%radius_variation + minimum_radius;
  } else {
    radius = (rand()%radius_variation + minimum_radius) * 0.5;
  }
  rotation_speed = (rand()%max_rotation-max_rotation/2)/radius;
  velocity = Point(rand()-RAND_MAX/2, rand()-RAND_MAX/2).normalized()*max_speed/radius;
  value = float(radius/(radius_variation + minimum_radius)) * 100.0f;
  children_added = false;
  this->invincible = invincible;
  if(!invincible) {
    num_killable++;
  }
  if(explode_sound == NULL) {
    explode_sound = Mix_LoadWAV("explode.wav");
    if(explode_sound == NULL) {
      std::cout << "Unable to load explode.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(thud_sound == NULL) {
    thud_sound = Mix_LoadWAV("thud.wav");
    if(thud_sound == NULL) {
        std::cout << "Unable to load thud.wav (" << Mix_GetError() << ")" << std::endl;
    } else {
        //Mix_VolumeChunk(thud_sound, MIX_MAX_VOLUME/2); // Todo:  distance volume
    }
  }
}

Asteroid::~Asteroid() {
  if(!killed && !invincible) {
    num_killable--;
  }
}

Asteroid::Asteroid(Asteroid const *mother) {
  radius = mother->radius/2.0f;
  rotation_speed = (rand()%6-3)/radius;
  velocity = Point(rand()-RAND_MAX/2, rand()-RAND_MAX/2).normalized()*max_speed/radius;
  position = mother->position + velocity.normalized() * radius;
  value = float(radius/(radius_variation + minimum_radius)) * 100.0f;
  value += mother->value;
  children_added = false;
  if(!invincible) {
    killed = false;
    num_killable++;
  }
}

bool Asteroid::kill() {
  if(thud_sound != NULL && invincible) {
    Mix_PlayChannel(-1, thud_sound, 0);
  }
  if(!invincible && !killed) {
    num_killable--;
    killed = true;
  }
  return CompositeObject::kill();
}

void Asteroid::add_children(list<Asteroid*> *roids) {
  if(alive || children_added) return;
  children_added = true;
  if(radius/2.0f < minimum_radius) {
    // explode good and proper
  } else {
    roids->push_back(new Asteroid(this));
    roids->push_back(new Asteroid(this));
  }
  if(explode_sound != NULL) {
    Mix_PlayChannel(-1, explode_sound, 0);
  }
  velocity = velocity / 8;
}
