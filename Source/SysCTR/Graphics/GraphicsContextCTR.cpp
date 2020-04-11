#include "stdafx.h"
#include "Graphics/GraphicsContext.h"

#include <3ds.h>

#include <GL/picaGL.h>

#include "Config/ConfigOptions.h"
#include "Core/ROM.h"
#include "Debug/DBGConsole.h"
#include "Debug/Dump.h"
#include "Graphics/ColourValue.h"
#include "Graphics/PngUtil.h"
#include "SysPSP/Graphics/VideoMemoryManager.h"
#include "Utility/IO.h"
#include "Utility/Preferences.h"
#include "Utility/Profiler.h"
#include "Utility/VolatileMem.h"

extern void HandleEndOfFrame();

#define SCR_WIDTH 400
#define SCR_HEIGHT 240

#define MAX_INDEXES 20000
uint16_t *gIndexes;
float *gVertexBuffer;
uint32_t *gColorBuffer;
float *gTexCoordBuffer;
float *gVertexBufferPtr;
uint32_t *gColorBufferPtr;
float *gTexCoordBufferPtr;

class IGraphicsContext : public CGraphicsContext
{
public:
	IGraphicsContext();
	virtual ~IGraphicsContext();

	bool				Initialise();
	bool				IsInitialised() const { return mInitialised; }

	void				SwitchToChosenDisplay();
	void				SwitchToLcdDisplay();
	void				StoreSaveScreenData();

	void				ClearAllSurfaces();

	void				ClearToBlack();
	void				ClearZBuffer();
	void				ClearColBuffer(const c32 &colour);
	void				ClearColBufferAndDepth(const c32 &colour);

	void				BeginFrame();
	void				EndFrame();
	void				UpdateFrame(bool wait_for_vbl);
	void				GetScreenSize(u32 * width, u32 * height) const;

	void				SetDebugScreenTarget( ETargetSurface buffer );

	void				ViewportType(u32 *d_width, u32 *d_height) const;
	void				DumpScreenShot();
	void				DumpNextScreen()			{ mDumpNextScreen = 2; }

private:
	void				SaveScreenshot(const char* filename, s32 x, s32 y, u32 width, u32 height);

private:
	bool				mInitialised;

	u32					mDumpNextScreen;
};

//*************************************************************************************
//
//*************************************************************************************
template<> bool CSingleton< CGraphicsContext >::Create()
{
#ifdef DAEDALUS_ENABLE_ASSERTS
	DAEDALUS_ASSERT_Q(mpInstance == nullptr);
#endif
	mpInstance = new IGraphicsContext();
	return mpInstance->Initialise();
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

IGraphicsContext::IGraphicsContext()
	:	mInitialised(false)
	,	mDumpNextScreen(false)
{
	pglInit();
	
	int i;
	gIndexes = (uint16_t*)malloc(sizeof(uint16_t)*MAX_INDEXES);
	for (i = 0; i < MAX_INDEXES; i++){
		gIndexes[i] = i;
	}
	gVertexBufferPtr = (float*)malloc(0x400000);
	gColorBufferPtr = (uint32_t*)malloc(0x200000);
	gTexCoordBufferPtr = (float*)malloc(0x200000);
	gVertexBuffer = gVertexBufferPtr;
	gColorBuffer = gColorBufferPtr;
	gTexCoordBuffer = gTexCoordBufferPtr;
}

IGraphicsContext::~IGraphicsContext()
{
	pglExit();
}

bool IGraphicsContext::Initialise()
{
	mInitialised = true;
	return true;
}

void IGraphicsContext::ClearAllSurfaces()
{
}

void IGraphicsContext::ClearToBlack()
{
	glDepthMask(GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth( 1.0f );
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void IGraphicsContext::ClearZBuffer()
{
	glDepthMask(GL_TRUE);
	glClearDepth( 1.0f );
	glClear( GL_DEPTH_BUFFER_BIT );
}

void IGraphicsContext::ClearColBuffer(const c32 & colour)
{
	glClearColor( colour.GetRf(), colour.GetGf(), colour.GetBf(), colour.GetAf() );
	glClear( GL_COLOR_BUFFER_BIT );
}

void IGraphicsContext::ClearColBufferAndDepth(const c32 & colour)
{
	glDepthMask(GL_TRUE);
	glClearDepth( 1.0f );
	glClearColor( colour.GetRf(), colour.GetGf(), colour.GetBf(), colour.GetAf() );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
}

void IGraphicsContext::BeginFrame()
{
	aptMainLoop();

	glEnable(GL_TEXTURE_2D);

	glEnableClientState(GL_VERTEX_ARRAY);

	gVertexBuffer = gVertexBufferPtr;
	gColorBuffer = gColorBufferPtr;
	gTexCoordBuffer = gTexCoordBufferPtr;
	//vglIndexPointerMapped(gIndexes);
}

void IGraphicsContext::EndFrame()
{
	glFinish();
	HandleEndOfFrame();
}

void IGraphicsContext::UpdateFrame(bool wait_for_vbl)
{
	DAEDALUS_PROFILE( "IGraphicsContext::UpdateFrame" );
	
	pglSwapBuffers();
//	if( gCleanSceneEnabled ) //TODO: This should be optional
	{
		ClearColBuffer( c32(0xff000000) ); // ToDo : Use gFillColor instead?
	}
}

void IGraphicsContext::SetDebugScreenTarget(ETargetSurface buffer)
{

}

void IGraphicsContext::ViewportType(u32 *d_width, u32 *d_height) const
{
	*d_width = SCR_WIDTH;
	*d_height = SCR_HEIGHT;
}

void IGraphicsContext::SaveScreenshot(const char* filename, s32 x, s32 y, u32 width, u32 height)
{
}

void IGraphicsContext::DumpScreenShot()
{
}

void IGraphicsContext::StoreSaveScreenData()
{
}

void IGraphicsContext::GetScreenSize(u32 * p_width, u32 * p_height) const
{
	*p_width = SCR_WIDTH;
	*p_height = SCR_HEIGHT;
}
