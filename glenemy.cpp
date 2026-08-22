#include "glenemy.h"
#include "gltrail.h"
#include "enemy.h"

#include "gl_compat.h"
#include "mesh.h"

#include "follower.h"
#include <list>

using namespace std;

// The derivation threshold sits in the gap between the hull families:
// standard wave ships top out ~0.135 (0.129 + noise + a dead difficulty
// term), interceptors start at 0.162. Save/rebuild paths use it to pick
// the variant back out of the stats, so if either band moves, keep the
// threshold between them.
const float GLEnemy::INTERCEPTOR_THRUST_MIN = 0.15f;

GLEnemy::GLEnemy(const Grid &grid, float x, float y, list<GLShip*>* targets, float difficulty, list<Object*>* asteroids, float aim_lead, bool interceptor) : GLShip(grid, false) {
  list<Ship*>* ships = new list<Ship*>;
  list<GLShip*>::iterator s;
  for(s = targets->begin(); s != targets->end(); s++) {
    ships->push_back((*s)->ship);
  }
  ship = new Ship(grid); // FIX: Enemy is unused
  // PROTO 16 wire identity. Minted HERE and not only in the (unused)
  // Enemy constructor — station enemies are plain Ships wrapped in
  // GLEnemy, and an id of 0 would collapse every exact enemy claim
  // onto the first alive enemy. The host's mint is authoritative; a
  // client replica's is overwritten from the extras on every apply.
  ship->net_ship_id = ++Ship::net_next_ship_id;
  ship->behaviours.push_back(new Follower(ship, (list<Object*>*)ships, asteroids,
                                          difficulty, aim_lead,
                                          interceptor ? 1500 : 3000));
  ship->position = WrappedPoint(x,y);
  if (interceptor) {
    // 0.162..0.170: fast enough to close on anyone hesitating, capped a
    // touch under 0.85x the player's plain thrust (0.2) so a committed
    // straight-line escape always wins — see the class comment's design
    // rule. Harder turning expresses the intercept steering; the value
    // pays for the pressure.
    ship->thrust_force = 0.162 + rand()%80/10000.0;
    ship->rotation_force = 0.30 + rand()%10/1000.0;
    ship->value = 150 + difficulty * 50;
  } else {
    ship->thrust_force = 0.129 + difficulty*0.00025 + rand()%50/10000.0;
    ship->rotation_force = 0.15 + difficulty*0.01 + rand()%10/1000.0;
    ship->value = 50 + difficulty * 50;
  }
  ship->lives = 1;

  // The interceptor runs a denser trail — the "something fast is coming"
  // telegraph has to be visible before the hull detail is.
  trails.push_back(new GLTrail(this, interceptor ? 0.02 : 0.05));

  if (interceptor) {
    // Amber, against the standard ship's green: the two variants must
    // read apart at a glance (late-game design rule: readability).
    color[0] = 1.0f; color[1] = 0.62f; color[2] = 0.1f;
  } else {
    color[0] = color[2] = 0.0;
    color[1] = 255/255.0;
  }

  {
    // Interceptor: a longer, narrower dart; standard: the classic arrowhead.
    const float nose = interceptor ? 1.4f : 1.0f;
    const float half = interceptor ? 0.5f : 0.8f;
    const float tail = interceptor ? -1.0f : -0.9f;
    const float notch = interceptor ? -0.7f : -1.3f;
    MeshBuilder mb;
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(0.0f, 0.0f, 0.0f);
    mb.vertex( 0.0f, nose); mb.vertex(-half, tail);
    mb.vertex(-0.0f, notch); mb.vertex( half, tail);
    mb.end();
    body_fill.upload(mb);

    mb.clear();
    mb.begin(GL_LINE_LOOP);
    mb.color(color[0], color[1], color[2]);
    mb.vertex( 0.0f, nose); mb.vertex(-half, tail);
    mb.vertex(-0.0f, notch); mb.vertex( half, tail);
    mb.end();
    body_outline.upload(mb);
    // jets mesh stays empty — enemy has no thruster effect
  }

  genForceShield();
}

GLEnemy::~GLEnemy() {
}
