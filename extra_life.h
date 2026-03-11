#ifndef EXTRA_LIFE_H
#define EXTRA_LIFE_H

#include "object.h"

class ExtraLife : public Object {
public:
  ExtraLife(WrappedPoint pos);
  void draw() const;
  bool is_removable() const override;
  bool collected;
};

#endif
