#include "tasfw/resources/PyramidUpdate.hpp"
#include <sm64/Surface.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Math.hpp>

PyramidUpdateMem::PyramidUpdateMem(const LibSm64& resource, Object* pyramidLibSm64)
{
	frame = resource.getCurrentFrame();

	//Initialize pyramid
	pyramid.posX = pyramidLibSm64->oPosX;
	pyramid.posY = pyramidLibSm64->oPosY;
	pyramid.posZ = pyramidLibSm64->oPosZ;
	pyramid.tiltingPyramidNormalX = pyramidLibSm64->oTiltingPyramidNormalX;
	pyramid.tiltingPyramidNormalY = pyramidLibSm64->oTiltingPyramidNormalY;
	pyramid.tiltingPyramidNormalZ = pyramidLibSm64->oTiltingPyramidNormalZ;
	pyramid.tiltingPyramidMarioOnPlatform = pyramidLibSm64->oTiltingPyramidMarioOnPlatform;
	std::copy((f32*)&pyramidLibSm64->transform, (f32*)&pyramidLibSm64->transform + 4 * 4, (f32*)&pyramid.transform);
	LoadSurfaces(pyramidLibSm64, pyramid);

	//Initialize Mario object
	Object* marioObjLibSm64 = (Object*)(resource.addr("gMarioObject"));
	marioObj.posX = marioObjLibSm64->oPosX;
	marioObj.posY = marioObjLibSm64->oPosY;
	marioObj.posZ = marioObjLibSm64->oPosZ;
	marioObjLibSm64->platform == pyramidLibSm64 ? marioObj.platform = &pyramid : marioObj.platform = nullptr;

	//Initialize Mario state
	MarioState* marioStateLibSm64 = (MarioState*)(resource.addr("gMarioStates"));
	marioState.posX = marioStateLibSm64->posX;
	marioState.posY = marioStateLibSm64->posY;
	marioState.posZ = marioStateLibSm64->posZ;
}

void PyramidUpdateMem::LoadSurfaces(Object* pyramidLibSm64, Sm64Object& pyramid)
{
	Sm64Surface* s;
	short vertexData[600];
	short* collisionData = (short*)pyramidLibSm64->collisionData;
	collisionData++;
	GetVertices(&collisionData, vertexData);

	int surfaceCount = CountSurfaces(collisionData);

	pyramid.surfaces.resize(surfaceCount);

	Sm64Surface* surfaces = &(*pyramid.surfaces.begin());
	while (*collisionData != TERRAIN_LOAD_CONTINUE)
		LoadObjectSurfaces(&pyramid, &collisionData, vertexData, &surfaces);
}

void PyramidUpdateMem::GetVertices(short** data, short* vertexData)
{
	short* vertices;
	int numVertices;

	numVertices = *(*data);
	(*data)++;

	vertices = *data;

	// Go through all vertices, rotating and translating them to transform the
	// object.
	while (numVertices--)
	{
		*vertexData++ = *(vertices++);
		*vertexData++ = *(vertices++);
		*vertexData++ = *(vertices++);
	}

	*data = vertices;
}

int PyramidUpdateMem::CountSurfaces(short* data)
{
	int surfaceCount = 0;

	while (*data != TERRAIN_LOAD_CONTINUE)
	{
		int surfaceType;
		int i;
		int numSurfaces;
		short hasForce;

		surfaceType = *data;
		data++;

		numSurfaces = *data;
		data++;

		hasForce = SurfaceHasForce(surfaceType);

		if (hasForce)
		{
			data += 4 * numSurfaces;
		}
		else
		{
			data += 3 * numSurfaces;
		}

		surfaceCount += numSurfaces;
	}

	return surfaceCount;
}

short PyramidUpdateMem::SurfaceHasForce(short surfaceType)
{
	short hasForce = false;

	switch (surfaceType)
	{
	case SURFACE_0004:	// Unused
	case SURFACE_FLOWING_WATER:
	case SURFACE_DEEP_MOVING_QUICKSAND:
	case SURFACE_SHALLOW_MOVING_QUICKSAND:
	case SURFACE_MOVING_QUICKSAND:
	case SURFACE_HORIZONTAL_WIND:
	case SURFACE_INSTANT_MOVING_QUICKSAND:
		hasForce = true;
		break;

	default:
		break;
	}
	return hasForce;
}

void PyramidUpdateMem::LoadObjectSurfaces(Sm64Object* pyramid, short** data, short* vertexData, Sm64Surface** surfaces)
{
	int surfaceType;
	int i;
	int numSurfaces;
	short hasForce;

	surfaceType = *(*data);
	(*data)++;

	numSurfaces = *(*data);
	(*data)++;

	hasForce = SurfaceHasForce(surfaceType);

	for (i = 0; i < numSurfaces; i++)
	{
		ReadSurfaceData(vertexData, data, *surfaces);
		(*surfaces)->type = surfaceType;
		(*surfaces)->object = pyramid;

		if (hasForce)
		{
			*data += 4;
		}
		else
		{
			*data += 3;
		}

		(*surfaces)++;
	}
}

void PyramidUpdateMem::ReadSurfaceData(short* vertexData, short** vertexIndices, Sm64Surface* surface)
{
	int x1, y1, z1;
	int x2, y2, z2;
	int x3, y3, z3;
	int maxY, minY;
	float nx, ny, nz;
	float mag;
	short offset1, offset2, offset3;

	offset1 = 3 * (*vertexIndices)[0];
	offset2 = 3 * (*vertexIndices)[1];
	offset3 = 3 * (*vertexIndices)[2];

	x1 = *(vertexData + offset1 + 0);
	y1 = *(vertexData + offset1 + 1);
	z1 = *(vertexData + offset1 + 2);

	x2 = *(vertexData + offset2 + 0);
	y2 = *(vertexData + offset2 + 1);
	z2 = *(vertexData + offset2 + 2);

	x3 = *(vertexData + offset3 + 0);
	y3 = *(vertexData + offset3 + 1);
	z3 = *(vertexData + offset3 + 2);

	// (v2 - v1) x (v3 - v2)
	nx = (y2 - y1) * (z3 - z2) - (z2 - z1) * (y3 - y2);
	ny = (z2 - z1) * (x3 - x2) - (x2 - x1) * (z3 - z2);
	nz = (x2 - x1) * (y3 - y2) - (y2 - y1) * (x3 - x2);
	mag = sqrtf(nx * nx + ny * ny + nz * nz);

	// Could have used min_3 and max_3 for this...
	minY = y1;
	if (y2 < minY)
	{
		minY = y2;
	}
	if (y3 < minY)
	{
		minY = y3;
	}

	maxY = y1;
	if (y2 > maxY)
	{
		maxY = y2;
	}
	if (y3 > maxY)
	{
		maxY = y3;
	}

	mag = (f32)(1.0 / mag);
	nx *= mag;
	ny *= mag;
	nz *= mag;

	surface->vertex1[0] = x1;
	surface->vertex2[0] = x2;
	surface->vertex3[0] = x3;

	surface->vertex1[1] = y1;
	surface->vertex2[1] = y2;
	surface->vertex3[1] = y3;

	surface->vertex1[2] = z1;
	surface->vertex2[2] = z2;
	surface->vertex3[2] = z3;

	surface->normal.x = nx;
	surface->normal.y = ny;
	surface->normal.z = nz;

	surface->originOffset = -(nx * x1 + ny * y1 + nz * z1);

	surface->lowerY = minY - 5;
	surface->upperY = maxY + 5;
}

PyramidUpdate::PyramidUpdate()
{
    slotManager._saveMemLimit = 1024 * 1024 * 1024; //1 GB
}

void PyramidUpdate::save(PyramidUpdateMem& state) const
{
    state = _state;
}

void PyramidUpdate::load(PyramidUpdateMem& state)
{
    _state = state;
}

std::size_t PyramidUpdate::getStateSize(const PyramidUpdateMem& state) const
{
	return 2 * sizeof(PyramidUpdateMem) + state.pyramid.surfaces.capacity() + state.marioObj.surfaces.capacity();
}

uint32_t PyramidUpdate::getCurrentFrame() const
{
	return _state.frame;
}

void PyramidUpdate::advance()
{
	PyramidLoop();
	TransformSurfaces();

	_state.frame++;
}

void* PyramidUpdate::addr(const char* symbol) const
{
	return nullptr;
}

void PyramidUpdate::PyramidLoop()
{
	f32 dx;
	f32 dy;
	f32 dz;
	f32 d;

	Vec3f dist;
	Vec3f posBeforeRotation;
	Vec3f posAfterRotation;

	// Mario's position
	f32 mx = _state.marioState.posX;
	f32 my = _state.marioState.posY;
	f32 mz = _state.marioState.posZ;

	s32 marioOnPlatform = FALSE;
	Mat4* transform = &_state.pyramid.transform;

	if (_state.marioObj.platform == &_state.pyramid) {
		dist[0] = _state.marioObj.posX - _state.pyramid.posX;
		dist[1] = _state.marioObj.posY - _state.pyramid.posY;
		dist[2] = _state.marioObj.posZ - _state.pyramid.posZ;
		linear_mtxf_mul_vec3f(*transform, posBeforeRotation, dist);

		dx = _state.marioObj.posX - _state.pyramid.posX;
		dy = 500.0f;
		dz = _state.marioObj.posZ - _state.pyramid.posZ;
		d = sqrtf(dx * dx + dy * dy + dz * dz);

		//! Always true since dy = 500, making d >= 500.
		if (d != 0.0f) {
			// Normalizing
			d = 1.0 / d;
			dx *= d;
			dy *= d;
			dz *= d;
		}
		else {
			dx = 0.0f;
			dy = 1.0f;
			dz = 0.0f;
		}

		if (_state.pyramid.tiltingPyramidMarioOnPlatform == TRUE)
			marioOnPlatform++;

		_state.pyramid.tiltingPyramidMarioOnPlatform = TRUE;
	}
	else {
		dx = 0.0f;
		dy = 1.0f;
		dz = 0.0f;
		_state.pyramid.tiltingPyramidMarioOnPlatform = FALSE;
	}

	// Approach the normals by 0.01f towards the new goal, then create a transform matrix and orient the object. 
	// Outside of the other conditionals since it needs to tilt regardless of whether Mario is on.
	_state.pyramid.tiltingPyramidNormalX = ApproachByIncrement(dx, _state.pyramid.tiltingPyramidNormalX, 0.01f);
	_state.pyramid.tiltingPyramidNormalY = ApproachByIncrement(dy, _state.pyramid.tiltingPyramidNormalY, 0.01f);
	_state.pyramid.tiltingPyramidNormalZ = ApproachByIncrement(dz, _state.pyramid.tiltingPyramidNormalZ, 0.01f);
	CreateTransformFromNormals(*transform, _state.pyramid.tiltingPyramidNormalX, _state.pyramid.tiltingPyramidNormalY, _state.pyramid.tiltingPyramidNormalZ);

	// If Mario is on the platform, adjust his position for the platform tilt.
	if (marioOnPlatform != FALSE) {
		linear_mtxf_mul_vec3f(*transform, posAfterRotation, dist);
		mx += posAfterRotation[0] - posBeforeRotation[0];
		my += posAfterRotation[1] - posBeforeRotation[1];
		mz += posAfterRotation[2] - posBeforeRotation[2];
		_state.marioState.posX = mx;
		_state.marioState.posY = my;
		_state.marioState.posZ = mz;
	}
}

float PyramidUpdate::ApproachByIncrement(float goal, float src, float inc) {
	f32 newVal;

	if (src <= goal) {
		if (goal - src < inc) {
			newVal = goal;
		}
		else {
			newVal = src + inc;
		}
	}
	else if (goal - src > -inc) {
		newVal = goal;
	}
	else {
		newVal = src - inc;
	}

	return newVal;
}

void PyramidUpdate::CreateTransformFromNormals(Mat4& transform, float xNorm, float yNorm, float zNorm) {
	Vec3f normal;
	Vec3f pos;

	pos[0] = _state.pyramid.posX;
	pos[1] = _state.pyramid.posY;
	pos[2] = _state.pyramid.posZ;

	normal[0] = xNorm;
	normal[1] = yNorm;
	normal[2] = zNorm;

	mtxf_align_terrain_normal(transform, normal, pos, 0);
}

void PyramidUpdate::TransformSurfaces()
{
	int maxY, minY;
	float nx, ny, nz;
	float mag;

	PyramidUpdateMem::Sm64Surface* surfaces = &(*_state.pyramid.surfaces.begin());;
	Mat4* transform = &_state.pyramid.transform;
	for (int i = 0; i < _state.pyramid.surfaces.size(); i++)
	{
		Vec3s v1 = {
			surfaces->vertex1[0], surfaces->vertex1[1], surfaces->vertex1[2] };
		Vec3s v2 = {
			surfaces->vertex2[0], surfaces->vertex2[1], surfaces->vertex2[2] };
		Vec3s v3 = {
			surfaces->vertex3[0], surfaces->vertex3[1], surfaces->vertex3[2] };

		for (int j = 0; j < 3; j++)
		{
			surfaces->vertex1[j] =
				(short)(v1[0] * (*transform)[0][j] + v1[1] * (*transform)[1][j] + v1[2] * (*transform)[2][j] + (*transform)[3][j]);
			surfaces->vertex2[j] =
				(short)(v2[0] * (*transform)[0][j] + v2[1] * (*transform)[1][j] + v2[2] * (*transform)[2][j] + (*transform)[3][j]);
			surfaces->vertex3[j] =
				(short)(v3[0] * (*transform)[0][j] + v3[1] * (*transform)[1][j] + v3[2] * (*transform)[2][j] + (*transform)[3][j]);
		}

		// (v2 - v1) x (v3 - v2)
		nx = (surfaces->vertex2[1] - surfaces->vertex1[1]) *
			(surfaces->vertex3[2] - surfaces->vertex2[2]) -
			(surfaces->vertex2[2] - surfaces->vertex1[2]) *
			(surfaces->vertex3[1] - surfaces->vertex2[1]);
		ny = (surfaces->vertex2[2] - surfaces->vertex1[2]) *
			(surfaces->vertex3[0] - surfaces->vertex2[0]) -
			(surfaces->vertex2[0] - surfaces->vertex1[0]) *
			(surfaces->vertex3[2] - surfaces->vertex2[2]);
		nz = (surfaces->vertex2[0] - surfaces->vertex1[0]) *
			(surfaces->vertex3[1] - surfaces->vertex2[1]) -
			(surfaces->vertex2[1] - surfaces->vertex1[1]) *
			(surfaces->vertex3[0] - surfaces->vertex2[0]);
		mag = sqrtf(nx * nx + ny * ny + nz * nz);

		// Could have used min_3 and max_3 for this...
		minY = surfaces->vertex1[1];
		if (surfaces->vertex2[1] < minY)
		{
			minY = surfaces->vertex2[1];
		}
		if (surfaces->vertex3[1] < minY)
		{
			minY = surfaces->vertex3[1];
		}

		maxY = surfaces->vertex1[1];
		if (surfaces->vertex2[1] > maxY)
		{
			maxY = surfaces->vertex2[1];
		}
		if (surfaces->vertex3[1] > maxY)
		{
			maxY = surfaces->vertex3[1];
		}

		mag = (float)(1.0 / mag);
		nx *= mag;
		ny *= mag;
		nz *= mag;

		surfaces->normal.x = nx;
		surfaces->normal.y = ny;
		surfaces->normal.z = nz;

		surfaces->originOffset =
			-(nx * surfaces->vertex1[0] + ny * surfaces->vertex1[1] +
				nz * surfaces->vertex1[2]);

		surfaces->lowerY = minY - 5;
		surfaces->upperY = maxY + 5;

		surfaces++;
	}
}
