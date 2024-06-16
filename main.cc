#include <iostream>
#include <thread>
#include <vector>

#include "memory_pool.hpp"

class Test {
  int a, b;
  char c;

 public:
  Test(int a_, int b_, char c_) : a(a_), b(b_), c(c_) {
    printf("construct a: %d, b: %d, c:%d\n", a, b, c);
  }
  ~Test() { printf("destruct a: %d, b: %d, c:%d\n", a, b, c); }
};

memory_pool::MemoryPool<Test> mp;
std::vector<Test*> v1;
std::vector<Test*> v2;

int main() {
  std::thread t1([] {
    for (int i = 0; i < 30; i++) {
      v1.push_back(mp.Create(i, i + 1, 'a' - i));
    }
  });
  std::thread t2([] {
    for (int i = 0; i < 35; i++) {
      v2.push_back(mp.Create(i, i - 1, 'z' - i));
    }
  });

  t1.join();
  t2.join();

  getchar();

  std::thread t3([] {
    for (auto& p : v1) {
      mp.Destroy(p);
    }
  });
  std::thread t4([] {
    for (auto& p : v2) {
      mp.Destroy(p);
    }
  });
  t3.join();
  t4.join();
  return 0;
}
