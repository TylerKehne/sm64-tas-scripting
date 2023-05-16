#include "PyramidUpdate.hpp"
#include <math.h>
#include <sm64/Surface.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Math.hpp>
#include <sm64/Sm64.hpp>

void PyramidUpdate::UpdateMario()
{
    return;
}

void PyramidUpdate::CopyMarioStateToObject()
{
    _state.marioObj.posX = _state.marioState.posX;
    _state.marioObj.posY = _state.marioState.posY;
    _state.marioObj.posZ = _state.marioState.posZ;
}