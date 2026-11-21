#include "Network/Games/Sgt24h/Prediction.h"
#include <cassert>
using namespace Sgt24h;
int main() {
 PredictSample a{{{0,0,0}},1000000,1,0,1},b{{{1,0,0}},1100000,1,0,1};Position p;
 assert(Predict(a,b,1150000,200000,100000,20,2,p) && p.component[0]==1.5f);
 assert(Predict(a,b,1200000,200000,100000,20,.25,p) && p.component[0]==1.25f);
 assert(!Predict(a,b,1200001,200000,100000,20,2,p));
 assert(!Predict(a,b,1099999,200000,100000,20,2,p));
 assert(!Predict(a,b,1150000,200000,100000,5,2,p));
 assert(!Predict(a,b,1150000,99999,100000,20,2,p));
 auto test=[&](PredictSample changed){p={{7,8,9}};assert(!Predict(a,changed,1150000,200000,100000,20,2,p));assert(p.component[0]==7 && p.component[1]==8 && p.component[2]==9);};
 auto c=b;c.epoch=2;test(c);c=b;c.authority=1;test(c);c=b;c.slot=2;test(c);c=b;c.time=a.time;test(c);c=b;c.p.component[1]=NAN;test(c);
 c=b;c.p=a.p;assert(Predict(a,c,1150000,200000,100000,20,2,p) && p.component[0]==0);
}
