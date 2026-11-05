#pragma once
#include <cmath>
#include <numbers>

namespace wi {
// Clockwise from up, centered sectors; ties belong to the clockwise sector.
inline int stickDirection(double x,double y){
    double angle=std::atan2(x,y)*180.0/std::numbers::pi;
    if(angle<0)angle+=360;
    return int(std::floor((angle+11.25)/22.5))%16;
}
inline const char* stickDirectionName(int direction){
    static constexpr const char* names[]={"上","上偏右","右上","右偏上","右","右偏下","右下","下偏右","下","下偏左","左下","左偏下","左","左偏上","左上","上偏左"};
    return names[direction];
}
}
