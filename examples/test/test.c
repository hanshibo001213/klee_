#include <klee/klee.h>

int main() {
  int x = 0;

  klee_make_symbolic(&x, sizeof(x), "x");
  while (x < 3) {
    x++;
  }

  return 0;
}
