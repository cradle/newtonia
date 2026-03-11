#ifndef EXTRA_LIFE_H
#define EXTRA_LIFE_H

#include "object.h"

class ExtraLife : public Object {
public:
  ExtraLife(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  bool is_removable() const override;
  bool collected;
};

#endif
