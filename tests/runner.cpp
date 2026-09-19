#include "RunnerPhysics.hpp"
#include <iostream>
#include <stdexcept>
using versus::RunnerJump;
void check(bool value,char const* text){if(!value)throw std::runtime_error(text);std::cout<<"PASS "<<text<<'\n';}
float apex(bool hold,float dt){RunnerJump j;j.press();float best=0;for(int i=0;i<int(2.f/dt);++i){if(!hold&&i==1)j.release();j.step(dt);best=std::max(best,j.height);}check(j.height==0,"jump returns to ground");return best;}
int main(){
 auto shortJump=apex(false,1.f/120.f),longJump=apex(true,1.f/120.f);
 check(longJump>shortJump+20.f,"holding jumps higher than tapping");
 check(std::abs(longJump-apex(true,1.f/60.f))<5.f,"jump height stable across frame rates");
 RunnerJump j;j.press();j.step(.01f);auto v=j.velocity;j.press();check(j.velocity==v,"key repeat cannot add jump impulse");
 j.release();j.press();check(j.velocity<=120.f,"no midair double jump");
 for(int i=0;i<300;++i)j.step(.01f);check(j.height==0&&j.velocity==0,"holding does not auto jump on landing");
 j.release();j.press();check(j.velocity>0,"new tap jumps after landing");
}
