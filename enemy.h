#ifndef ENEMY_H
#define ENEMY_H

#include <list>
#include "ship.h"

class Enemy : public Ship {
  public:
    Enemy() : Ship(true) {};
    Enemy(float x, float y, std::list<Ship*> * targets, int difficulty = 0);
    virtual ~Enemy();

    void step(float delta, const Grid &grid);
    virtual void reset(bool was_killed = true);
    //FIX: Why isn't this workinging without this method?
    bool is_removable() const;

  private:
    Ship* target;
    std::list<Ship*> * targets;
    int difficulty;
};

#endif
