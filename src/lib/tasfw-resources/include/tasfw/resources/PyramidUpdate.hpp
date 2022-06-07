#pragma once
#include <vector>
#include "tasfw/Resource.hpp"
#include <sm64/Types.hpp>
#include "tasfw/resources/LibSm64.hpp"

#ifndef PYRAMIDUPDATE_H
#define PYRAMIDUPDATE_H

class PyramidUpdateMem
{
public:
	class Sm64Object;

	class Sm64Surface
	{
	public:
		s16 type;
		s16 force;
		s8 flags;
		s8 room;
		s16 lowerY;
		s16 upperY;
		Vec3s vertex1;
		Vec3s vertex2;
		Vec3s vertex3;
		struct
		{
			f32 x;
			f32 y;
			f32 z;
		} normal;
		f32 originOffset;
		Sm64Object* object;
	};

	class Sm64Object
	{
	public:
		float posX = 0;
		float posY = 0;
		float posZ = 0;
		float tiltingPyramidNormalX = 0;
		float tiltingPyramidNormalY = 0;
		float tiltingPyramidNormalZ = 0;
		s32 tiltingPyramidMarioOnPlatform = FALSE;
		Sm64Object* platform = nullptr;
		Mat4 transform;
		std::vector<Sm64Surface> surfaces;
	};

	class MarioState
	{
	public:
		float posX = 0;
		float posY = 0;
		float posZ = 0;
	};

	Sm64Object marioObj;
	Sm64Object pyramid;
	MarioState marioState;
	int64_t frame = 0;

	PyramidUpdateMem() = default;
	PyramidUpdateMem(const LibSm64& resource, Object* pyramidLibSm64);

private:
	//TODO: All of this needs to be rewritten when memory access is standardized
	void LoadSurfaces(Object* pyramidLibSm64, Sm64Object& pyramid);
	void GetVertices(short** data, short* vertexData);
	int CountSurfaces(short* data);
	short SurfaceHasForce(short surfaceType);
	void LoadObjectSurfaces(Sm64Object* pyramid, short** data, short* vertexData, Sm64Surface** surfaces);
	void ReadSurfaceData(short* vertexData, short** vertexIndices, Sm64Surface* surface);
};

class PyramidUpdate : public Resource<PyramidUpdateMem>
{
public:
	PyramidUpdate();
	void save(PyramidUpdateMem& state) const;
	void load(PyramidUpdateMem& state);
	void advance();
	void* addr(const char* symbol) const;
	std::size_t getStateSize(const PyramidUpdateMem& state) const;
	uint32_t getCurrentFrame() const;

private:
	PyramidUpdateMem _state;
	void PyramidLoop();
	void TransformSurfaces();
	float ApproachByIncrement(float goal, float src, float inc);
	void CreateTransformFromNormals(Mat4& transform, float xNorm, float yNorm, float zNorm);
};

#endif