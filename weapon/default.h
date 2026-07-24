#ifndef DEFAULT_H
#define DEFAULT_H

#include "base.h"
#include "SDL.h"
#include "SDL_mixer.h"

class Point;

namespace Weapon {
  class Default : public Base {
  public:
    Default(Ship *ship, bool automatic = false, int level = 0, float accuracy = 0.1f, int time_between_shots = 100, int weapon_index = -1, int burst_count = 1, int burst_interval = 40);
    ~Default();

    void shoot(bool on = true) override;
    void step(int delta) override;
    int weapon_index() const { return _weapon_index; }
    bool is_automatic() const override { return automatic; }
    // Netplay: the client's snapshot restore rebuilds the weapon list; the
    // fire cooldown must survive the swap or a held trigger fires an extra
    // shot on every snapshot (a fresh weapon starts ready to fire).
    int cooldown() const { return time_until_next_shot; }
    void set_cooldown(int ms) { time_until_next_shot = ms; }
    // Netplay: burst shots still owed from the current trigger pull — must
    // survive the snapshot rebuild too, or a burst in flight at apply time
    // loses its remaining shots (the host's sim fires all of them).
    int burst_pending() const { return burst_shots_left; }
    void set_burst_pending(int n) { burst_shots_left = n; }

  private:
    void fire();
    void fire_shot(Point direction);

    bool automatic;
    float accuracy;
    int time_until_next_shot, time_between_shots;
    int level;
    int _weapon_index;
    // Burst fire (burst_count > 1): one trigger pull fires burst_count
    // shots spaced burst_interval ms apart, one ammo each; semi-automatic
    // between bursts (time_between_shots gates the next pull).
    int burst_count, burst_interval, burst_shots_left;

    Mix_Chunk *shoot_sound = NULL, *empty_sound = NULL;
  };
}

#endif
