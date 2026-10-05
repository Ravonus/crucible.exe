#include "harness.h"
void core_tests(void);
void save_tests(void);
int main(void) {
  core_tests();
  save_tests();
  printf("%d checks, %d failed\n", t_checks, t_fails);
  return t_fails ? 1 : 0;
}
