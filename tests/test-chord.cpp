#include "chord.hpp"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main() {
    Chord c;
    c.update(314,true,true,100);
    CHECK(c.update(315,true,true,200));
    CHECK(!c.due(true,1199));
    CHECK(c.due(true,1200));
    CHECK(!c.due(true,2200));
    c.update(314,false,true,2300); c.update(314,true,true,2400);
    CHECK(!c.due(true,4000)); // require both released after firing
    c.update(314,false,true,4100); c.update(315,false,true,4100);
    c.update(314,true,false,4200); c.update(315,true,true,4300);
    CHECK(!c.due(true,6000)); // outside-home first press blocks
    c.reset(); c.update(314,true,true,0); c.update(315,true,true,10);
    CHECK(!c.due(false,1010)); CHECK(!c.due(true,2010));
    c.reset(true,true); CHECK(!c.due(true,10000));
    c.update(314,false,true,10001); c.update(315,false,true,10001);
    c.update(314,true,true,11000); c.update(315,true,true,11010);
    c.block(); CHECK(!c.due(true,13000));
    c.reset(); c.update(314,true,true,0); c.update(315,true,true,10);
    c.update(315,false,true,500); CHECK(!c.due(true,1010));
    c.reset(); Chord other;
    c.update(314,true,true,0); other.update(315,true,true,0);
    CHECK(!c.due(true,3000)); CHECK(!other.due(true,3000));
    c.reset(); c.update(314,true,true,100); c.update(315,true,true,200);
    CHECK(c.due(true,1200));
    std::puts("PASS chord timing, release, page guard, reconnect and device isolation");
}
