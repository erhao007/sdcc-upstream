#include <stdio.h>

/* This test used to fail on x86_64 on linux with sse registers */

struct Point {
   float x;
   float y;
};

struct Rect {
   struct Point top_left;
   struct Point size;
};

float foo(struct Point p, struct Rect r) {
   (void)p;
   return r.size.x;
}

int main(int argc, char **argv) {
#if defined(__SDCC_mcs251)
   static __xdata
#endif
   struct Point p = {1, 2};
#if defined(__SDCC_mcs251)
   static __xdata
#endif
   struct Rect r = {{3, 4}, {5, 6}};
   (void)argc;
   (void)argv;
   printf("%f\n", foo(p, r));
   return 0;
}
