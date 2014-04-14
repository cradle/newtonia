#ifndef GL_STATION_H
#define GL_STATION_H

#include <list>
#include "glship.h"
#include "wrapped_point.h"

using namespace std;

class GLStation : public Ship {
public:
  GLStation(list<GLShip*>* objects, list<GLShip*>* targets);
  virtual ~GLStation();

  void draw(bool minimap = false) const;
  void step(float delta, const Grid &grid);
  // void collide(Ship *ship) const;
  void reset();
  int level() const;

private:
  unsigned int body, map_body;
  static const int NUM_SEGMENTS = 30;
  float inner_rotation, outer_rotation, outer_rotation_speed, inner_rotation_speed;

  std::list<GLShip*>* objects, *targets;
  int ships_this_wave, max_ships_per_wave, extra_ships_per_wave, ships_left_to_deploy;
  int time_until_next_ship, time_between_ships;
  bool deploying, redeploying;
  int wave, difficulty;
};

#endif
