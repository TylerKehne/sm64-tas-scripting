#pragma once

#include <tasfw/Script.hpp>
#include "LibSm64.hpp"
#include <PyramidUpdate.hpp>
#include <sm64/Camera.hpp>
#include <sm64/ObjectFields.hpp>

#ifndef TILT_TARGET_H
#define TILT_TARGET_H

class TiltTarget : public Script<LibSm64>
{
public:
    TiltTarget(float nX, float nZ) : _targetNX(nX), _targetNZ(nZ) {}
    TiltTarget(float nX, float nY, float nZ) : _targetNX(nX), _targetNY(nY), _targetNZ(nZ) {}

    bool validation()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));

        // verify we are on the platform
        if (marioState->floor == nullptr)
            return false;

        _pyramid = marioState->floor->object;
        if (_pyramid == nullptr || _pyramid->behavior != pyramidBehavior)
            return false;

        if (std::fabs(_pyramid->oTiltingPyramidNormalX) + std::fabs(_pyramid->oTiltingPyramidNormalZ) > 0.6f)
            return false;

        UpdateMinError();

        if (!CheckEquilibrium())
            return false;

        return true;
    }

    bool execution()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidmBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        // 

        //while (true)
        //{

        //}

        return true;
    }

    bool assertion()
    {
        return true;
    }

private:
    // target
    float _targetNX = 0;
    float _targetNY = 0;
    float _targetNZ = 0;

    // off from target by
    float _minErrorX = 0;
    float _minErrorY = 0;
    float _minErrorZ = 0;

    Object* _pyramid = nullptr;

    std::vector<float> CalculateError()
    {
        std::vector<float> error(3);

        error[0] = std::fabs(_targetNX - _pyramid->oTiltingPyramidNormalX);
        error[1] = std::fabs(_targetNY - _pyramid->oTiltingPyramidNormalY);
        error[2] = std::fabs(_targetNZ - _pyramid->oTiltingPyramidNormalZ);

        return error;
    }

    void UpdateMinError()
    {
        auto error = CalculateError();
        if (std::sqrtf(error[0] * error[0] + error[2] * error[2]) < std::sqrtf(_minErrorX * _minErrorX + _minErrorZ * _minErrorZ))
        {
            _minErrorX = error[0];
            _minErrorY = error[1];
            _minErrorZ = error[2];
        }
    }

    bool CheckEquilibrium()
    {
        bool equilibrium = true;
        ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));

                bool isMoving = marioState->forwardVel != 0;
                auto error = CalculateError();

                float marioX = marioState->pos[0];
                float marioY = marioState->pos[1];
                float marioZ = marioState->pos[2];

                AdvanceFrameWrite(Inputs(0, 0, 0));

                isMoving |= marioState->forwardVel != 0;
                if (isMoving)
                    equilibrium = false;

                if (marioX != marioState->pos[0] || marioY != marioState->pos[1] || marioZ != marioState->pos[2])
                    equilibrium = false;

                auto error2 = CalculateError();
                if (error[0] != error2[0] || error[1] != error2[1] || error[2] != error2[2])
                    equilibrium = false;

                return true;
            });

        return equilibrium;
    }

    bool Perturb()
    {


        return true;
    }
};

#endif