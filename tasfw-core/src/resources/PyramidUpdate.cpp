#include "tasfw/resources/PyramidUpdate.hpp"
#include <math.h>
#include <sm64/Surface.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Math.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/SurfaceTerrains.hpp>
#include <sm64/Camera.hpp>

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
	Object* marioObjLibSm64 = *(Object**)(resource.addr("gMarioObject"));
	marioObj.posX = marioObjLibSm64->oPosX;
	marioObj.posY = marioObjLibSm64->oPosY;
	marioObj.posZ = marioObjLibSm64->oPosZ;
	marioObj.platformIsPyramid = marioObjLibSm64->platform == pyramidLibSm64;

	//Initialize Mario state
	MarioState* marioStateLibSm64 = (MarioState*)(resource.addr("gMarioStates"));
	marioState.posX = marioStateLibSm64->pos[0];
	marioState.posY = marioStateLibSm64->pos[1];
	marioState.posZ = marioStateLibSm64->pos[2];
	marioState.action = marioStateLibSm64->action;

	//Add lava
	AddStaticGeometry();

	//Copy camera yaw
	Camera* sm64Camera = *(Camera**)(resource.addr("gCamera"));
	camera.yaw = sm64Camera->yaw;
}

void PyramidUpdateMem::AddStaticGeometry()
{
	staticFloors.resize(2);
	staticFloors[0] = Sm64Surface();
	staticFloors[1] = Sm64Surface();

	//floor 1
	staticFloors[0].vertex1[0] = -8191;
	staticFloors[0].vertex2[0] = 8192;
	staticFloors[0].vertex3[0] = -8191;

	staticFloors[0].vertex1[1] = -3071;
	staticFloors[0].vertex2[1] = -3071;
	staticFloors[0].vertex3[1] = -3071;

	staticFloors[0].vertex1[2] = 8192;
	staticFloors[0].vertex2[2] = -8191;
	staticFloors[0].vertex3[2] = -8191;

	staticFloors[0].normal.x = 0;
	staticFloors[0].normal.y = 1;
	staticFloors[0].normal.z = 0;

	staticFloors[0].originOffset = 3071;

	staticFloors[0].lowerY = -3076;
	staticFloors[0].upperY = -3066;

	staticFloors[0].type = SURFACE_BURNING;
	staticFloors[0].objectIsPyramid = false;

	//floor 2
	staticFloors[1].vertex1[0] = -8191;
	staticFloors[1].vertex2[0] = 8192;
	staticFloors[1].vertex3[0] = 8192;

	staticFloors[1].vertex1[1] = -3071;
	staticFloors[1].vertex2[1] = -3071;
	staticFloors[1].vertex3[1] = -3071;

	staticFloors[1].vertex1[2] = 8192;
	staticFloors[1].vertex2[2] = 8192;
	staticFloors[1].vertex3[2] = -8191;

	staticFloors[1].normal.x = 0;
	staticFloors[1].normal.y = 1;
	staticFloors[1].normal.z = 0;

	staticFloors[1].originOffset = 3071;

	staticFloors[1].lowerY = -3076;
	staticFloors[1].upperY = -3066;

	staticFloors[1].type = SURFACE_BURNING;
	staticFloors[1].objectIsPyramid = false;
}

PyramidUpdateMem::Sm64Object* PyramidUpdateMem::Sm64Surface::object(PyramidUpdateMem& state)
{
	if (objectIsPyramid)
		return &state.pyramid;

	return nullptr;
}

PyramidUpdateMem::Sm64Object* PyramidUpdateMem::Sm64Object::platform(PyramidUpdateMem& state)
{
	if (platformIsPyramid)
		return &state.pyramid;

	return nullptr;
}

PyramidUpdateMem::Sm64Surface* PyramidUpdateMem::Sm64MarioState::floor(PyramidUpdateMem& state)
{
	if (floorId == -1)
		return nullptr;

	if (floorId >= static_cast<int64_t>(state.pyramid.surfaces[1].size()))
		throw std::runtime_error("Surface id " + std::to_string(floorId) + " larger than max id " + std::to_string(state.pyramid.surfaces[1].size() - 1));

	return &state.pyramid.surfaces[1][floorId];
}

void PyramidUpdateMem::LoadSurfaces(Object* pyramidLibSm64, Sm64Object& pyramid)
{
	short vertexData[600];
	short* collisionData = (short*)pyramidLibSm64->collisionData;
	collisionData++;
	GetVertices(&collisionData, vertexData);

	int surfaceCount = CountSurfaces(collisionData);

	pyramid.surfaces[0].resize(surfaceCount);
	pyramid.surfaces[1].resize(surfaceCount);
	pyramid.surfaces[2].resize(surfaceCount);

	Sm64Surface* surfaces[3] = { &(*pyramid.surfaces[0].begin()), &(*pyramid.surfaces[1].begin()), &(*pyramid.surfaces[2].begin()) };
	while (*collisionData != TERRAIN_LOAD_CONTINUE)
		LoadObjectSurfaces(&collisionData, vertexData, surfaces);
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

void PyramidUpdateMem::LoadObjectSurfaces(short** data, short* vertexData, Sm64Surface** surfaceArrays)
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
		ReadSurfaceData(vertexData, data, surfaceArrays, surfaceType);

		if (hasForce)
			*data += 4;
		else
			*data += 3;
	}
}

void PyramidUpdateMem::ReadSurfaceData(short* vertexData, short** vertexIndices, Sm64Surface** surfaceArrays, int surfaceType)
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

	//Walls, Floors, and Ceilings
	int surfaceIndex = 0;
	if (ny > 0.01)
		surfaceIndex = 1;
	else if (ny < -0.01)
		surfaceIndex = 2;

	Sm64Surface* surface = surfaceArrays[surfaceIndex];

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

	surface->type = surfaceType;
	surface->objectIsPyramid = true;

	//Should be safe as vector was resized to equal surface count for all 3 surface types.
	surfaceArrays[surfaceIndex]++;
}

bool PyramidUpdateMem::FloorIsSlope(Sm64Surface* floor, u32 action)
{
	float normY;

	switch (PyramidUpdateMem::GetFloorClass(floor, action))
	{
	case SURFACE_VERY_SLIPPERY:
		normY = 0.9961947f;	 // ~cos(5 deg)
		break;

	case SURFACE_SLIPPERY:
		normY = 0.9848077f;	 // ~cos(10 deg)
		break;

	default:
		normY = 0.9659258f;	 // ~cos(15 deg)
		break;

	case SURFACE_NOT_SLIPPERY:
		normY = 0.9396926f;	 // ~cos(20 deg)
		break;
	}

	return floor->normal.y <= normY;
}

short PyramidUpdateMem::GetFloorClass(Sm64Surface* floor, u32 action)
{
	short floorClass = SURFACE_CLASS_DEFAULT;

	if (floor != nullptr)
	{
		switch (floor->type)
		{
		case SURFACE_NOT_SLIPPERY:
		case SURFACE_HARD_NOT_SLIPPERY:
		case SURFACE_SWITCH:
			floorClass = SURFACE_CLASS_NOT_SLIPPERY;
			break;

		case SURFACE_SLIPPERY:
		case SURFACE_NOISE_SLIPPERY:
		case SURFACE_HARD_SLIPPERY:
		case SURFACE_NO_CAM_COL_SLIPPERY:
			floorClass = SURFACE_CLASS_SLIPPERY;
			break;

		case SURFACE_VERY_SLIPPERY:
		case SURFACE_ICE:
		case SURFACE_HARD_VERY_SLIPPERY:
		case SURFACE_NOISE_VERY_SLIPPERY_73:
		case SURFACE_NOISE_VERY_SLIPPERY_74:
		case SURFACE_NOISE_VERY_SLIPPERY:
		case SURFACE_NO_CAM_COL_VERY_SLIPPERY:
			floorClass = SURFACE_CLASS_VERY_SLIPPERY;
			break;
		}

		// Crawling allows Mario to not slide on certain steeper surfaces.
		if (action == ACT_CRAWLING && floor && floor->normal.y > 0.5f && floorClass == SURFACE_CLASS_DEFAULT)
		{
			floorClass = SURFACE_CLASS_NOT_SLIPPERY;
		}
	}

	return floorClass;
}

PyramidUpdate::PyramidUpdate()
{
    slotManager._saveMemLimit = 1024 * 1024 * 1024; //1 GB
}

void PyramidUpdate::save(PyramidUpdateMem& state) const
{
    state = _state;
}

void PyramidUpdate::load(const PyramidUpdateMem& state)
{
    _state = state;
}

std::size_t PyramidUpdate::getStateSize(const PyramidUpdateMem& state) const
{
	return 2 * sizeof(PyramidUpdateMem) + 3 * sizeof(PyramidUpdateMem::Sm64Surface) * (state.pyramid.surfaces[0].capacity() + state.marioObj.surfaces[0].capacity());
}

uint32_t PyramidUpdate::getCurrentFrame() const
{
	return _state.frame;
}

void PyramidUpdate::advance()
{
	UpdatePyramid();
	TransformSurfaces(0); // Walls
	TransformSurfaces(1); // Floors
	TransformSurfaces(2); // Ceilings

	Vec3f marioPos = { _state.marioState.posX, _state.marioState.posY , _state.marioState.posZ };
	int64_t dynamicFloorId = -1;
	float dynamicY = FindFloor(&marioPos, &(*_state.pyramid.surfaces[1].begin()), _state.pyramid.surfaces[1].size(), &dynamicFloorId);

	int64_t staticFloorId = -1;
	float staticY = FindFloor(&marioPos, &(*_state.staticFloors.begin()), _state.staticFloors.size(), &staticFloorId);

	if (dynamicY > staticY)
	{
		_state.marioState.floorId = dynamicFloorId;
		_state.marioState.isFloorStatic = false;
	}
	else
	{
		_state.marioState.floorId = staticFloorId;
		_state.marioState.isFloorStatic = true;
	}

	_state.frame++;
}

void* PyramidUpdate::addr(const char* symbol) const
{
	std::string symbolStr = std::string(symbol);

	if (symbolStr == "gMarioStates")
		return (void*)&_state.marioState;

	if (symbolStr == "gMarioObject")
		return (void*)&_state.marioObj;

	if (symbolStr == "Pyramid")
		return (void*)&_state.pyramid;

	if (symbolStr == "gControllerPads")
		return (void*)&_state.inputs;

	throw std::runtime_error("Unable to resolve symbol \"" + symbolStr + "\"");

	return nullptr;
}

void PyramidUpdate::UpdatePyramid()
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

	s32 marioOnPlatform = false;
	Mat4* transform = &_state.pyramid.transform;

	if (_state.marioObj.platform(_state) == &_state.pyramid) {
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

		if (_state.pyramid.tiltingPyramidMarioOnPlatform == true)
			marioOnPlatform++;

		_state.pyramid.tiltingPyramidMarioOnPlatform = true;
	}
	else {
		dx = 0.0f;
		dy = 1.0f;
		dz = 0.0f;
		_state.pyramid.tiltingPyramidMarioOnPlatform = false;
	}

	// Approach the normals by 0.01f towards the new goal, then create a transform matrix and orient the object. 
	// Outside of the other conditionals since it needs to tilt regardless of whether Mario is on.
	_state.pyramid.tiltingPyramidNormalX = ApproachByIncrement(dx, _state.pyramid.tiltingPyramidNormalX, 0.01f);
	_state.pyramid.tiltingPyramidNormalY = ApproachByIncrement(dy, _state.pyramid.tiltingPyramidNormalY, 0.01f);
	_state.pyramid.tiltingPyramidNormalZ = ApproachByIncrement(dz, _state.pyramid.tiltingPyramidNormalZ, 0.01f);
	CreateTransformFromNormals(*transform, _state.pyramid.tiltingPyramidNormalX, _state.pyramid.tiltingPyramidNormalY, _state.pyramid.tiltingPyramidNormalZ);

	// If Mario is on the platform, adjust his position for the platform tilt.
	if (marioOnPlatform != false) {
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

void PyramidUpdate::TransformSurfaces(int surfaceIndex)
{
	int maxY, minY;
	float nx, ny, nz;
	float mag;

	PyramidUpdateMem::Sm64Surface* surfaces = &(*_state.pyramid.surfaces[surfaceIndex].begin());;
	Mat4* transform = &_state.pyramid.transform;
	for (size_t i = 0; i < _state.pyramid.surfaces[surfaceIndex].size(); i++)
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

float PyramidUpdate::FindFloor(Vec3f* marioPos, PyramidUpdateMem::Sm64Surface* surfaces, int surfaceCount, int64_t* floorId)
{
	int i;
	PyramidUpdateMem::Sm64Surface* surf;
	int x1, z1, x2, z2, x3, z3;
	float nx, ny, nz;
	float oo;
	float height;

	*floorId = -1;

	// Iterate through the list of floors until there are no more floors.
	for (i = 0; i < surfaceCount; i++)
	{
		surf = surfaces;
		surfaces++;

		if (surf->normal.y <= 0.1)
			continue;

		x1 = surf->vertex1[0];
		z1 = surf->vertex1[2];
		x2 = surf->vertex2[0];
		z2 = surf->vertex2[2];

		// Check that the point is within the triangle bounds.
		if (
			(z1 - (*marioPos)[2]) * (x2 - x1) - (x1 - (*marioPos)[0]) * (z2 - z1) < 0)
		{
			continue;
		}

		// To slightly save on computation time, set this later.
		x3 = surf->vertex3[0];
		z3 = surf->vertex3[2];

		if (
			(z2 - (*marioPos)[2]) * (x3 - x2) - (x2 - (*marioPos)[0]) * (z3 - z2) < 0)
		{
			continue;
		}
		if (
			(z3 - (*marioPos)[2]) * (x1 - x3) - (x3 - (*marioPos)[0]) * (z1 - z3) < 0)
		{
			continue;
		}

		nx = surf->normal.x;
		ny = surf->normal.y;
		nz = surf->normal.z;
		oo = surf->originOffset;

		// Find the height of the floor at a given location.
		height = -((*marioPos)[0] * nx + nz * (*marioPos)[2] + oo) / ny;
		// Checks for floor interaction with a 78 unit buffer.
		if ((*marioPos)[1] - (height + -78.0f) < 0.0f)
		{
			continue;
		}

		*floorId = i;
		break;
	}

	return height;
}