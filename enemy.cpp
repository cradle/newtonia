#include "enemy.h"

#include <list>
#include <iostream>
#include <cstdlib>
using namespace std;
#include "point.h"
#include "follower.h"
#include "weapon/default.h"

Enemy::Enemy(const Grid &grid, float x, float y, std::list<Ship*>* targets, int difficulty) : Ship(grid, true), targets(targets), difficulty(difficulty) {
  position = WrappedPoint(x,y);
  thrust_force = 0.135 + difficulty*0.00025 + rand()%50/10000.0;
  rotation_force = 0.15 + difficulty*0.01 + rand()%10/1000.0;
  value = 50 + difficulty * 50;
  lives = 1;
  target = NULL;
  alive = true;
}

Enemy::~Enemy() {
  delete targets;
}

void Enemy::step(float delta, const Grid &grid) {
  Ship::step(delta, grid);
}

bool Enemy::is_removable() const {
  //TODO: Fix this, why can't it just call Ship::is_removable()?
  return !alive && debris.empty();
}

void Enemy::reset(bool was_killed) {
  cout << "reset()" << endl;
  thrust(true);
  if(difficulty > 10) {
    delete primary_weapons.front();
    primary_weapons.pop_front();
    primary_weapons.push_front(new Weapon::Default(this, true, 0, 1.0f/difficulty, rand()%100 + (5000/(difficulty-9))));
    shoot(true);
  } else {
    // FIRE ZE MISSILES!
  }
  behaviours.push_back(new Follower(this, (std::list<Object*>*)&targets));
}
