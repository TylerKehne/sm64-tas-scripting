#pragma once
#include <Scattershot.hpp>
#include <tasfw/scripts/BitFSPyramidOscillation.hpp>

class SShotState_BitfsDr
{
public:
    uint8_t x;
    uint8_t y;
    uint8_t z;
    uint64_t s;

    SShotState_BitfsDr() = default;
    SShotState_BitfsDr(uint8_t x, uint8_t y, uint8_t z, uint64_t s) : x(x), y(y), z(z), s(s) { }
    bool operator==(const SShotState_BitfsDr&) const = default;
};

class Scattershot_BitfsDr : public ScattershotThread<SShotState_BitfsDr, LibSm64>
{
public:
    Scattershot_BitfsDr(Scattershot<SShotState_BitfsDr, LibSm64>& scattershot, int id)
        : ScattershotThread<SShotState_BitfsDr, LibSm64>(scattershot, id) {}

    void SelectMovementOptions()
    {
        AddRandomMovementOption(
            {
                {MovementOption::MAX_MAGNITUDE, 4},
                {MovementOption::ZERO_MAGNITUDE, 0},
                {MovementOption::SAME_MAGNITUDE, 0},
                {MovementOption::RANDOM_MAGNITUDE, 1}
            });

        AddRandomMovementOption(
            {
                {MovementOption::MATCH_FACING_YAW, 1},
                {MovementOption::ANTI_FACING_YAW, 2},
                {MovementOption::SAME_YAW, 4},
                {MovementOption::RANDOM_YAW, 8}
            });

        AddRandomMovementOption(
            {
                {MovementOption::SAME_BUTTONS, 0},
                {MovementOption::NO_BUTTONS, 0},
                {MovementOption::RANDOM_BUTTONS, 10}
            });

        AddRandomMovementOption(
            {
                {MovementOption::NO_SCRIPT, 95},
                {MovementOption::PBD, 95},
                {MovementOption::RUN_DOWNHILL, 0},
                {MovementOption::REWIND, 0},
                {MovementOption::TURN_UPHILL, 95}
            });
    }

    bool ApplyMovement()
    {
        return ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
                Camera* camera = *(Camera**)(resource->addr("gCamera"));

                // Scripts
                if (CheckMovementOptions(MovementOption::REWIND))
                {
                    int64_t currentFrame = GetCurrentFrame();
                    int maxRewind = (currentFrame - config.StartFrame) / 2;
                    int rewindFrames = (GetTempRng() % 100) * maxRewind / 100;
                    Load(currentFrame - rewindFrames);
                }

                if (CheckMovementOptions(MovementOption::RUN_DOWNHILL))
                {
                    BitFsPyramidOscillation_ParamsDto params;
                    params.roughTargetAngle = marioState->faceAngle[1];
                    params.ignoreXzSum = true;
                    return Modify<BitFsPyramidOscillation_RunDownhill>(params).asserted;
                }
                else if (CheckMovementOptions(MovementOption::PBD) && Pbd())
                    return true;
                else if (CheckMovementOptions(MovementOption::TURN_UPHILL) && TurnUphill())
                    return true;

                // stick mag
                float intendedMag = 0;
                if (CheckMovementOptions(MovementOption::MAX_MAGNITUDE))
                    intendedMag = 32.0f;
                else if (CheckMovementOptions(MovementOption::ZERO_MAGNITUDE))
                    intendedMag = 0;
                else if (CheckMovementOptions(MovementOption::SAME_MAGNITUDE))
                    intendedMag = marioState->intendedMag;
                else if (CheckMovementOptions(MovementOption::RANDOM_MAGNITUDE))
                    intendedMag = (GetTempRng() % 1024) / 32.0f;

                // Intended yaw
                int16_t intendedYaw = 0;
                if (CheckMovementOptions(MovementOption::MATCH_FACING_YAW))
                    intendedYaw = marioState->faceAngle[1];
                else if (CheckMovementOptions(MovementOption::ANTI_FACING_YAW))
                    intendedYaw = marioState->faceAngle[1] + 0x8000;
                else if (CheckMovementOptions(MovementOption::SAME_YAW))
                    intendedYaw = marioState->intendedYaw;
                else if (CheckMovementOptions(MovementOption::RANDOM_YAW))
                    intendedYaw = GetTempRng();

                // Buttons
                uint16_t buttons = 0;
                if (CheckMovementOptions(MovementOption::SAME_BUTTONS))
                    buttons = GetInputs(GetCurrentFrame() - 1).buttons;
                else if (CheckMovementOptions(MovementOption::NO_BUTTONS))
                    buttons = 0;
                else if (CheckMovementOptions(MovementOption::RANDOM_BUTTONS))
                {
                    buttons |= Buttons::B * (GetTempRng() % 2);
                    //buttons |= Buttons::Z & UpdateHashReturnPrev(rngHash) % 2;
                    //buttons |= Buttons::C_UP & UpdateHashReturnPrev(rngHash) % 2;
                }
                    
                // Calculate and execute input
                auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, intendedMag, camera->yaw);
                AdvanceFrameWrite(Inputs(buttons, stick.first, stick.second));

                return true;
            }).executed;
    }

    SShotState_BitfsDr GetStateBin()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidmBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];
        //if (pyramid->behavior != pyramidmBehavior)

        uint64_t s = 0;
        if (marioState->action == ACT_BRAKING) s = 0;
        if (marioState->action == ACT_DIVE) s = 1;
        if (marioState->action == ACT_DIVE_SLIDE) s = 2;
        if (marioState->action == ACT_FORWARD_ROLLOUT) s = 3;
        if (marioState->action == ACT_FREEFALL_LAND_STOP) s = 4;
        if (marioState->action == ACT_FREEFALL) s = 5;
        if (marioState->action == ACT_FREEFALL_LAND) s = 6;
        if (marioState->action == ACT_TURNING_AROUND) s = 7;
        if (marioState->action == ACT_FINISH_TURNING_AROUND) s = 8;
        if (marioState->action == ACT_WALKING) s = 9;

        s *= 30;
        s += (int)((40 - marioState->vel[1]) / 4);

        float norm_regime_min = .69;
        //float norm_regime_max = .67;
        float target_xnorm = -.30725;
        float target_znorm = .3665;
        float x_delt = pyramid->oTiltingPyramidNormalX - target_xnorm;
        float z_delt = pyramid->oTiltingPyramidNormalZ - target_znorm;
        float x_remainder = x_delt * 100 - floor(x_delt * 100);
        float z_remainder = z_delt * 100 - floor(z_delt * 100);


        //if((fabs(pyraXNorm) + fabs(pyraZNorm) < norm_regime_min) ||
        //   (fabs(pyraXNorm) + fabs(pyraZNorm) > norm_regime_max)){  //coarsen for bad norm regime
        if ((x_remainder > .001 && x_remainder < .999) || (z_remainder > .001 && z_remainder < .999) ||
            (fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ) < norm_regime_min)) //coarsen for not target norm envelope
        { 
            s *= 14;
            s += (int)((pyramid->oTiltingPyramidNormalX + 1) * 7);

            s *= 14;
            s += (int)((pyramid->oTiltingPyramidNormalZ + 1) * 7);

            s += 1000000 + 1000000 * (int)floor((marioState->forwardVel + 20) / 8);
            s += 100000000 * (int)floor((float)marioState->faceAngle[1] / 16384.0);

            s *= 2;
            s += 1; //mark bad norm regime

            return SShotState_BitfsDr(
                (uint8_t)floor((marioState->pos[0] + 2330) / 200),
                (uint8_t)floor((marioState->pos[1] + 3200) / 400),
                (uint8_t)floor((marioState->pos[2] + 1090) / 200),
                s);
        }

        s *= 200;
        s += (int)((pyramid->oTiltingPyramidNormalX + 1) * 100);

        float xzSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);

        s *= 10;
        xzSum += (int)((xzSum - norm_regime_min) * 100);
        //s += (int)((pyraZNorm + 1)*100);

        s *= 30;
        s += (int)((pyramid->oTiltingPyramidNormalY - .7) * 100);

        //fifd: Hspd mapped into sections {0-1, 1-2, ...}
        s += 30000000 + 30000000 * (int)floor((marioState->forwardVel + 20));

        //fifd: Yaw mapped into sections
        //s += 100000000 * (int)floor((float)marioYawFacing / 2048.0);
        s += ((uint64_t)1200000000) * (int)floor((float)marioState->faceAngle[1] / 4096.0);

        s *= 2; //mark good norm regime

        return SShotState_BitfsDr(
            (uint8_t)floor((marioState->pos[0] + 2330) / 10),
            (uint8_t)floor((marioState->pos[1] + 3200) / 50),
            (uint8_t)floor((marioState->pos[2] + 1090) / 10),
            s);
    }

    bool ValidateState()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidmBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        if (marioState->pos[0] < -2330 || marioState->pos[0] > -1550)
            return false;

        if (marioState->pos[2] < -1090 || marioState->pos[2] > -300)
            return false;

        if (marioState->pos[1] > -2760)
            return false;

        if (pyramid->oTiltingPyramidNormalZ < -.15 || pyramid->oTiltingPyramidNormalX > 0.15)
            return false; //stay in desired quadrant

        if (marioState->action != ACT_BRAKING && marioState->action != ACT_DIVE && marioState->action != ACT_DIVE_SLIDE &&
            marioState->action != ACT_FORWARD_ROLLOUT && marioState->action != ACT_FREEFALL_LAND_STOP && marioState->action != ACT_FREEFALL &&
            marioState->action != ACT_FREEFALL_LAND && marioState->action != ACT_TURNING_AROUND &&
            marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING) //not useful action, such as lava boost
        {
            return false;
        } 

        if (marioState->action == ACT_FREEFALL && marioState->vel[1] > -20.0)
            return false;//freefall without having done nut spot chain

        if (marioState->floorHeight > -3071 && marioState->pos[1] > marioState->floorHeight + 4 &&
            marioState->vel[1] != 22.0)
            return false;//above pyra by over 4 units

        if (marioState->floorHeight == -3071 && marioState->action != ACT_FREEFALL)
            return false; //diving/dring above lava

        float xNorm = pyramid->oTiltingPyramidNormalX;
        float zNorm = pyramid->oTiltingPyramidNormalZ;

        if (marioState->action == ACT_FORWARD_ROLLOUT && fabs(xNorm) > .3 && fabs(xNorm) + fabs(zNorm) > .65 &&
            marioState->pos[0] + marioState->pos[2] > (-1945 - 715)) //make sure Mario is going toward the right/east edge
        {  
            #pragma omp critical
            {
                char fileName[128];
                printf("\ndr\n");
                sprintf(fileName, "C:\\Users\\Tyler\\Documents\\repos\\sm64_tas_scripting\\res\\bitfs_dr_%f_%f_%f_%f.m64",
                    pyramid->oTiltingPyramidNormalX, pyramid->oTiltingPyramidNormalY, pyramid->oTiltingPyramidNormalZ, marioState->vel[1]);
                ExportM64(fileName);
            }
        }

        //check on hspd > 1 confirms we're in dr land rather than quickstopping,
        //which gives the same action
        if (marioState->action == ACT_FREEFALL_LAND_STOP && marioState->pos[1] > -2980 && marioState->forwardVel > 1
            && fabs(xNorm) > .29 && fabs(marioState->pos[0]) > -1680)
        {
            char fileName[128];
            printf("\ndrland\n");
            //sprintf(fileName, "C:\\Users\\Tyler\\Documents\\repos\\scattershot\\x64\\Debug\\m64s\\drland\\bitfs_dr_%f_%f_%f_%f_%d.m64",
            //    pyramid->oTiltingPyramidNormalX, pyramid->oTiltingPyramidNormalY, pyramid->oTiltingPyramidNormalZ, marioState->vel[1], omp_get_thread_num());
            //Utils::writeFile(fileName, "C:\\Users\\Tyler\\Documents\\repos\\scattershot\\x64\\Debug\\4_units_from_edge.m64", m64Diff, config.StartFrame, frame + 1);
        }

        return true;
    }

    float GetStateFitness()
    {
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        return pyramid->oTiltingPyramidNormalY;
    }

private:
    bool Pbd()
    {
        return ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
                Camera* camera = *(Camera**)(resource->addr("gCamera"));

                // Validate consitions for dive
                if (marioState->action != ACT_WALKING || marioState->forwardVel < 29.0f)
                    return false;

                int16_t intendedYaw = marioState->faceAngle[1] + GetTempRng() % 32768 - 16384;
                auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
                AdvanceFrameWrite(Inputs(Buttons::B | Buttons::START, stick.first, stick.second));

                //if (marioState->action != ACT_DIVE_SLIDE)
                //    return false;

                AdvanceFrameWrite(Inputs(0, 0, 0));
                AdvanceFrameWrite(Inputs(Buttons::START, 0, 0));
                AdvanceFrameWrite(Inputs(0, 0, 0));

                return true;
            }).executed;
    }

    bool TurnUphill()
    {
        return ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
                Camera* camera = *(Camera**)(resource->addr("gCamera"));
                Object* objectPool = (Object*)(resource->addr("gObjectPool"));
                Object* pyramid = &objectPool[84];

                // Turn 2048 towrds uphill
                auto m64 = M64();
                auto save = ImportedSave(PyramidUpdateMem(*resource, pyramid), GetCurrentFrame());
                auto status = BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle
                    ::MainFromSave<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>(m64, save, 0);
                if (!status.validated)
                    return false;

                int16_t uphillAngle = status.floorAngle + 0x8000;

                //cap intended yaw diff at 2048
                int16_t intendedYaw = uphillAngle;
                if (abs(int16_t(uphillAngle - marioState->faceAngle[1])) >= 16384)
                    intendedYaw = marioState->faceAngle[1] + 2048 * sign(int16_t(uphillAngle - marioState->faceAngle[1]));

                auto inputs = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
                AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));
                return true;
            }).executed;
    }
};
