#include "glenemy.h"
#include "gltrail.h"
#include "enemy.h"

#include "gl_compat.h"
#include "mesh.h"

#include "follower.h"
#include <list>

using namespace std;

// The derivation thresholds sit in the gaps between the hull families:
// bombers top out at 0.115, standard wave ships run ~0.129-0.135 (0.129 +
// noise + a dead difficulty term), rammers run 0.140-0.148, interceptors
// start at 0.162. The save/rebuild paths pick the variant back out of the
// stats with these, so if any band moves, keep the thresholds between them.
const float GLEnemy::INTERCEPTOR_THRUST_MIN = 0.15f;
const float GLEnemy::RAMMER_THRUST_MIN      = 0.138f;
const float GLEnemy::BOMBER_THRUST_MAX      = 0.12f;

GLEnemy::Variant GLEnemy::variant_for_thrust(float thrust_force) {
  if (thrust_force >= INTERCEPTOR_THRUST_MIN) return INTERCEPTOR;
  if (thrust_force >= RAMMER_THRUST_MIN)      return RAMMER;
  if (thrust_force <= BOMBER_THRUST_MAX)      return BOMBER;
  return STANDARD;
}

GLEnemy::GLEnemy(const Grid &grid, float x, float y, list<GLShip*>* targets, float difficulty, list<Object*>* asteroids, float aim_lead, Variant variant) : GLShip(grid, false) {
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
                                          variant == INTERCEPTOR ? 1500
                                          : variant == BOMBER    ? 4000
                                                                 : 3000,
                                          variant == BOMBER ? Follower::BOMBER
                                          : variant == RAMMER ? Follower::RAMMER
                                                              : Follower::GUNNER));
  ship->position = WrappedPoint(x,y);
  if (variant == INTERCEPTOR) {
    // 0.162..0.170: fast enough to close on anyone hesitating, capped a
    // touch under 0.85x the player's plain thrust (0.2) so a committed
    // straight-line escape always wins — see the class comment's design
    // rule. Harder turning expresses the intercept steering; the value
    // pays for the pressure.
    ship->thrust_force = 0.162 + rand()%80/10000.0;
    ship->rotation_force = 0.30 + rand()%10/1000.0;
    ship->value = 150 + difficulty * 50;
  } else if (variant == BOMBER) {
    // 0.105..0.115: slower than every other hull — the artillery piece
    // waddles, and once you close on it, it is dead meat. Sluggish
    // turning to match; the value pays for the flak pressure.
    ship->thrust_force = 0.105 + rand()%100/10000.0;
    ship->rotation_force = 0.12 + rand()%10/1000.0;
    ship->value = 250 + difficulty * 50;
  } else if (variant == RAMMER) {
    // 0.140..0.148: quicker than the standard ship, slower than the
    // interceptor and well under the player's 0.2 (the hunter hazard's
    // 0.155 precedent) — the charge you can always outrun but must
    // respect. Turning near the interceptor's so the never-break-off
    // pursuit actually tracks; the value sits between its neighbours.
    ship->thrust_force = 0.140 + rand()%80/10000.0;
    ship->rotation_force = 0.24 + rand()%10/1000.0;
    ship->value = 200 + difficulty * 50;
  } else {
    ship->thrust_force = 0.129 + difficulty*0.00025 + rand()%50/10000.0;
    ship->rotation_force = 0.15 + difficulty*0.01 + rand()%10/1000.0;
    ship->value = 50 + difficulty * 50;
  }
  ship->lives = 1;

  // The interceptor runs a denser trail — the "something fast is coming"
  // telegraph has to be visible before the hull detail is — and the
  // rammer's is denser still: the thing that never stops thrusting at you
  // should look like it. The bomber's is sparser: it barely moves.
  trails.push_back(new GLTrail(this, variant == INTERCEPTOR ? 0.02
                                     : variant == RAMMER    ? 0.015
                                     : variant == BOMBER    ? 0.08
                                                            : 0.05));

  if (variant == INTERCEPTOR) {
    // Amber, against the standard ship's green: the variants must read
    // apart at a glance (late-game design rule: readability).
    color[0] = 1.0f; color[1] = 0.62f; color[2] = 0.1f;
  } else if (variant == BOMBER) {
    // Violet: the third family in the wave palette.
    color[0] = 0.75f; color[1] = 0.35f; color[2] = 1.0f;
  } else if (variant == RAMMER) {
    // Crimson: red-means-ram is vocabulary the player learned from the
    // seeker (gen 12) and the hunter (gen 18).
    color[0] = 0.95f; color[1] = 0.15f; color[2] = 0.2f;
  } else {
    color[0] = color[2] = 0.0;
    color[1] = 255/255.0;
  }

  if (variant == BOMBER) {
    // A squat wide-bodied barge with a blunt prow — nothing dart-like
    // about it, so the rear line reads at a glance.
    MeshBuilder mb;
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(0.0f, 0.0f, 0.0f);
    mb.vertex( 0.0f,  0.9f); mb.vertex(-0.9f,  0.3f); mb.vertex(-0.9f, -0.6f);
    mb.vertex( 0.0f, -0.9f); mb.vertex( 0.9f, -0.6f); mb.vertex( 0.9f,  0.3f);
    mb.end();
    body_fill.upload(mb);

    mb.clear();
    mb.begin(GL_LINE_LOOP);
    mb.color(color[0], color[1], color[2]);
    mb.vertex( 0.0f,  0.9f); mb.vertex(-0.9f,  0.3f); mb.vertex(-0.9f, -0.6f);
    mb.vertex( 0.0f, -0.9f); mb.vertex( 0.9f, -0.6f); mb.vertex( 0.9f,  0.3f);
    mb.end();
    // The mortar tube: a short barrel line off the prow.
    mb.begin(GL_LINES);
    mb.color(color[0], color[1], color[2]);
    mb.vertex(0.0f, 0.9f); mb.vertex(0.0f, 1.25f);
    mb.end();
    body_outline.upload(mb);
    // jets mesh stays empty — enemy has no thruster effect
  } else if (variant == RAMMER) {
    // A hammerhead: a wide swept head bar leading, the body tapering to a
    // waist and flaring slightly at the tail — all the mass is at the
    // front because the front is the weapon. The silhouette is concave,
    // so the fill is three convex fans (head bar + body + tail flare)
    // under one line-loop outline.
    MeshBuilder mb;
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(0.0f, 0.0f, 0.0f);
    mb.vertex(-0.95f, 1.2f); mb.vertex( 0.95f, 1.2f);
    mb.vertex( 0.95f, 0.8f); mb.vertex(-0.95f, 0.8f);
    mb.end();
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(0.0f, 0.0f, 0.0f);
    mb.vertex(-0.32f, 0.8f); mb.vertex( 0.32f, 0.8f);
    mb.vertex( 0.12f, -0.7f); mb.vertex(-0.12f, -0.7f);
    mb.end();
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(0.0f, 0.0f, 0.0f);
    mb.vertex(-0.12f, -0.7f); mb.vertex( 0.12f, -0.7f);
    mb.vertex( 0.28f, -1.1f); mb.vertex(-0.28f, -1.1f);
    mb.end();
    body_fill.upload(mb);

    mb.clear();
    mb.begin(GL_LINE_LOOP);
    mb.color(color[0], color[1], color[2]);
    mb.vertex(-0.95f, 1.2f); mb.vertex( 0.95f, 1.2f);
    mb.vertex( 0.95f, 0.8f); mb.vertex( 0.32f, 0.8f);
    mb.vertex( 0.12f, -0.7f); mb.vertex( 0.28f, -1.1f);
    mb.vertex(-0.28f, -1.1f); mb.vertex(-0.12f, -0.7f);
    mb.vertex(-0.32f, 0.8f); mb.vertex(-0.95f, 0.8f);
    mb.end();
    body_outline.upload(mb);
    // jets mesh stays empty — enemy has no thruster effect
  } else {
    // Interceptor: a longer, narrower dart; standard: the classic arrowhead.
    const bool interceptor = variant == INTERCEPTOR;
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
