// Bounded presentation extrapolation; never a guest-physics update.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SGT24H_PREDICTION_H
#define INCLUDED_SGT24H_PREDICTION_H
#include "Position.h"
#include <algorithm>
namespace Sgt24h {
struct PredictSample {Position p;uint64_t time=0,epoch=0;unsigned authority=0,slot=0;};
// Caller establishes native lineage and supplies limits in native units.
// No tuned SGT speed/distance constants are assumed here.
inline bool Predict(const PredictSample& a,const PredictSample& b,uint64_t now,
                    uint64_t maxGap,uint64_t horizon,double maxSpeed,
                    double maxShift,Position& out) {
 if(!a.epoch || a.epoch!=b.epoch || a.authority!=b.authority || a.slot!=b.slot ||
    a.authority>=Cabinets || a.slot>=Cabinets || !a.time || b.time<=a.time ||
    b.time-a.time>maxGap || now<b.time || now-b.time>horizon ||
    !std::isfinite(maxSpeed) || maxSpeed<=0 || !std::isfinite(maxShift) || maxShift<=0)return false;
 const double dt=double(b.time-a.time)/1000000., ahead=double(now-b.time)/1000000.;
 double v[3],norm=0;
 for(unsigned i=0;i<3;++i) {
  if(!std::isfinite(a.p.component[i]) || !std::isfinite(b.p.component[i]))return false;
  v[i]=(double(b.p.component[i])-a.p.component[i])/dt;norm+=v[i]*v[i];
 }
 norm=std::sqrt(norm);if(!std::isfinite(norm) || norm>maxSpeed)return false;
 const double scale=norm*ahead>maxShift?maxShift/(norm*ahead):1.;
 Position next;
 for(unsigned i=0;i<3;++i) {
  next.component[i]=float(double(b.p.component[i])+v[i]*ahead*scale);
  if(!std::isfinite(next.component[i]))return false;
 }
 out=next;return true;
}
}
#endif
