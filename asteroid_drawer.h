#ifndef ASTEROID_DRAWER_H
#define ASTEROID_DRAWER_H

#include <list>
#include <vector>
using namespace std;

class Asteroid;
class Particle;

class AsteroidDrawer {
public:
  static void draw(Asteroid const *object, float direction, bool is_minimap);
  static void draw_batch(list<Asteroid*> const *objects, float direction, bool is_minimap);
  static void draw_debris(vector<Particle> const &debris);

private:
  static const int number_of_segments;
  static int seg_count(float radius);
};

#endif 