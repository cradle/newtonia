#ifndef ENEMY_H
#define ENEMY_H

#include <list>
#include "ship.h"

class Enemy : public Ship {
  public:
    Enemy(const Grid &grid) : Ship(grid, true) {};
    Enemy(const Grid &grid, float x, float y, std::list<Ship*> * targets, int difficulty = 0);
    virtual ~Enemy();

    void step(int delta, const Grid &grid);
    virtual void reset(bool was_killed = true);
    //FIX: Why isn't this workinging without this method?
    bool is_removable() const;

  private:
    Ship* target;
    std::list<Ship*> * targets;
    int difficulty;
};

#endif
