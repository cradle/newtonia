#ifndef BEHAVIOUR_H
#define BEHAVIOUR_H

class Ship;

class Behaviour {
public:
  Behaviour(Ship *ship) : ship(ship), done(false) {};
  virtual ~Behaviour() {};
  
  virtual void step(int delta) = 0;
  bool is_done();
  
protected:
  Ship *ship;
  bool done;
};

inline bool Behaviour::is_done() {
  return done;
}

#endif