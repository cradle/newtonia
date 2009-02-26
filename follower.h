#ifndef FOLLOWER_H
#define FOLLOWER_H

class Object;
#include "behaviour.h"
#include <list>

using namespace std;

class Follower : public Behaviour {
public:
  Follower(Ship *ship);
  Follower(Ship *ship, list<Object *> *targets);
  virtual ~Follower();
  
  virtual void step(int delta);
  
private:
  void common_init();
  void lock_step(int delta);
  void lock_nearest_target();
  void burst_shooting_step(int delta);
  
  list<Object *> *targets;
  int time_until_next_lock, time_between_locks;
  Object *target;
};

#endif