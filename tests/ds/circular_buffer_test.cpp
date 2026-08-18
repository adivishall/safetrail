// Fixed-capacity ring: newest-first indexing and overwrite-on-overflow.
#include "../test_harness.hpp"
#include "safetrail/ds/circular_buffer.hpp"
using namespace safetrail::ds;

int main() {
  CircularBuffer<int, 4> b;
  t::ok(b.empty(), "starts empty");
  b.push(10); b.push(20); b.push(30);
  t::ok(b.size() == 3, "size 3 after 3 pushes");
  t::ok(b.newest() == 30, "newest is last pushed");
  t::ok(b.oldest() == 10, "oldest is first pushed");
  t::ok(b[0] == 30 && b[1] == 20 && b[2] == 10, "index 0 = newest, ascending = older");

  b.push(40); b.push(50);   // overflow capacity 4: 10 evicted
  t::ok(b.size() == 4, "size caps at capacity");
  t::ok(b.newest() == 50, "newest is 50 after overflow");
  t::ok(b.oldest() == 20, "oldest is 20 (10 was overwritten)");
  t::ok(b[0]==50 && b[3]==20, "wraparound preserves newest-first order");

  b.clear();
  t::ok(b.empty() && b.size()==0, "clear resets");
  return t::report("ds/circular_buffer");
}
