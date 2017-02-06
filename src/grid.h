#ifndef GRID_H
#define GRID_H

#include <array>

// A simple two dimensional grid of class T, height H and width W.
// We represent it efficiently as a single row major array.
template<class T, int H, int W> class Grid {
public:
  typedef std::array<T, H * W> Container;
  
  // Initialize GridState by copying the underlying contents.
  Grid(const Grid& state) : mState(state.mState) {}
  
  // Underlying contents have undefined value.
  Grid() {}

  const T& get(int y, int x) const { return mState[y * W + x]; }
  void set(int y, int x, const T &value) { mState[y * W + x] = value; }
  typename Container::const_iterator begin() const { return mState.begin(); }
  typename Container::const_iterator end() const { return mState.end(); }
  void fill(const T &value) { mState.fill(value); }
  bool operator==(const Grid<T, H, W>& rhs) const { return mState == rhs.mState;}
  
private:
  Container mState;
  T& operator[](int pos) const { return mState[pos]; }
};

#endif
