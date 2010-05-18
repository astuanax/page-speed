// Copyright 2010 and onwards Google Inc.
// Author: sligocki@google.com (Shawn Ligocki)

#include "net/instaweb/util/public/hasher.h"

namespace net_instaweb {

Hasher::~Hasher() {
}

std::string Hasher::Hash(const StringPiece& content) {
  Reset();
  Add(content);
  std::string hash;
  ComputeHash(&hash);
  return hash;
}

}  // namespace net_instaweb
