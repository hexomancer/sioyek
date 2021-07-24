#include <iostream>
#include <filesystem>

int main(){
  std::filesystem::path path(".");
  std::cout << path << "\n";
  return 0;
}
