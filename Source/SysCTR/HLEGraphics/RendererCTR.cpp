#include "stdafx.h"
#include "RendererCTR.h"

#include <GL/picaGL.h>

//#include "Combiner/BlendConstant.h"
//#include "Combiner/CombinerTree.h"
//#include "Combiner/RenderSettings.h"
#include "Core/ROM.h"
#include "Debug/Dump.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/NativeTexture.h"
#include "HLEGraphics/CachedTexture.h"
#include "HLEGraphics/DLDebug.h"
#include "HLEGraphics/RDPStateManager.h"
#include "HLEGraphics/TextureCache.h"
#include "Math/MathUtil.h"
#include "OSHLE/ultra_gbi.h"
#include "Utility/IO.h"
#include "Utility/Profiler.h"

BaseRenderer *gRenderer    = nullptr;
RendererCTR  *gRendererCTR = nullptr;

extern float *gVertexBuffer;
extern uint32_t *gColorBuffer;
extern float *gTexCoordBuffer;

struct ScePspFMatrix4
{
	float m[16];
};


ScePspFMatrix4		gProjection;
void sceGuSetMatrix(int type, const ScePspFMatrix4 * mtx)
{
	if (type == GL_PROJECTION)
	{
		memcpy(&gProjection, mtx, sizeof(gProjection));
	}
}

static void InitBlenderMode()
{
	u32 cycle_type    = gRDPOtherMode.cycle_type;
	u32 cvg_x_alpha   = gRDPOtherMode.cvg_x_alpha;
	u32 alpha_cvg_sel = gRDPOtherMode.alpha_cvg_sel;
	u32 blendmode     = gRDPOtherMode.blender;

	// NB: If we're running in 1cycle mode, ignore the 2nd cycle.
	u32 active_mode = (cycle_type == CYCLE_2CYCLE) ? blendmode : (blendmode & 0xcccc);

	enum BlendType
	{
		kBlendModeOpaque,
		kBlendModeAlphaTrans,
		kBlendModeFade,
	};
	BlendType type = kBlendModeOpaque;

	// FIXME(strmnnrmn): lots of these need fog!

	switch (active_mode)
	{
	case 0x0040: // In * AIn + Mem * 1-A
		// MarioKart (spinning logo).
		type = kBlendModeAlphaTrans;
		break;
	case 0x0050: // In * AIn + Mem * 1-A | In * AIn + Mem * 1-A
		// Extreme-G.
		type = kBlendModeAlphaTrans;
		break;
	case 0x0440: // In * AFog + Mem * 1-A
		// Bomberman64. alpha_cvg_sel: 1 cvg_x_alpha: 1
		type = kBlendModeAlphaTrans;
		break;
	case 0x04d0: // In * AFog + Fog * 1-A | In * AIn + Mem * 1-A
		// Conker.
		type = kBlendModeAlphaTrans;
		break;
	case 0x0150: // In * AIn + Mem * 1-A | In * AFog + Mem * 1-A
		// Spiderman.
		type = kBlendModeAlphaTrans;
		break;
	case 0x0c08: // In * 0 + In * 1
		// MarioKart (spinning logo)
		// This blend mode doesn't use the alpha value
		type = kBlendModeOpaque;
		break;
	case 0x0c18: // In * 0 + In * 1 | In * AIn + Mem * 1-A
		// StarFox main menu.
		type = kBlendModeAlphaTrans;
		break;
	case 0x0c40: // In * 0 + Mem * 1-A
		// Extreme-G.
		type = kBlendModeFade;
		break;
	case 0x0f0a: // In * 0 + In * 1 | In * 0 + In * 1
		// Zelda OoT.
		type = kBlendModeOpaque;
		break;
	case 0x4c40: // Mem * 0 + Mem * 1-A
		//Waverace - alpha_cvg_sel: 0 cvg_x_alpha: 1
		type = kBlendModeFade;
		break;
	case 0x8410: // Bl * AFog + In * 1-A | In * AIn + Mem * 1-A
		// Paper Mario.
		type = kBlendModeAlphaTrans;
		break;
	case 0xc410: // Fog * AFog + In * 1-A | In * AIn + Mem * 1-A
		// Donald Duck (Dust)
		type = kBlendModeAlphaTrans;
		break;
	case 0xc440: // Fog * AFog + Mem * 1-A
		// Banjo Kazooie
		// Banjo Tooie sun glare
		// FIXME: blends fog over existing?
		type = kBlendModeAlphaTrans;
		break;
	case 0xc800: // Fog * AShade + In * 1-A
		//Bomberman64. alpha_cvg_sel: 0 cvg_x_alpha: 1
		type = kBlendModeOpaque;
		break;
	case 0xc810: // Fog * AShade + In * 1-A | In * AIn + Mem * 1-A
		// AeroGauge (ingame)
		type = kBlendModeAlphaTrans;
		break;
	// case 0x0321: // In * 0 + Bl * AMem
	// 	// Hmm - not sure about what this is doing. Zelda OoT pause screen.
	// 	type = kBlendModeAlphaTrans;
	// 	break;

	default:
#ifdef DAEDALUS_DEBUG_DISPLAYLIST
		DebugBlender( cycle_type, active_mode, alpha_cvg_sel, cvg_x_alpha );
		DL_PF( "		 Blend: SRCALPHA/INVSRCALPHA (default: 0x%04x)", active_mode );
#endif
		break;
	}

	// NB: we only have alpha in the blender is alpha_cvg_sel is 0 or cvg_x_alpha is 1.
	bool have_alpha = !alpha_cvg_sel || cvg_x_alpha;

	if (type == kBlendModeAlphaTrans && !have_alpha)
		type = kBlendModeOpaque;

	switch (type)
	{
	case kBlendModeOpaque:
		glDisable(GL_BLEND);
		break;
	case kBlendModeAlphaTrans:
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);
		break;
	case kBlendModeFade:
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);
		break;
	}
}

RendererCTR::RendererCTR()
{
}

RendererCTR::~RendererCTR()
{
}

void RendererCTR::RestoreRenderStates()
{
	// Initialise the device to our default state

	// No fog
	glDisable(GL_FOG);

	// We do our own culling
	glDisable(GL_CULL_FACE);

	u32 width, height;
	CGraphicsContext::Get()->GetScreenSize(&width, &height);

	glScissor(0,0, width,height);
	glEnable(GL_SCISSOR_TEST);

	// We do our own lighting
	glDisable(GL_LIGHTING);

	glBlendEquation(GL_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glDisable( GL_BLEND );

	// Default is ZBuffer disabled
	glDepthMask(GL_FALSE);		// GL_FALSE to disable z-writes
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_DEPTH_TEST);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void RendererCTR::PrepareRenderState(const float (&mat_project)[16], bool disable_zbuffer )
{
	if ( disable_zbuffer )
	{
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
	}
	else
	{
		// Decal mode
/*		if( gRDPOtherMode.zmode == 3 )
		{
			glPolygonOffset(-1.0, -1.0);
		}
		else
		{
			glPolygonOffset(0.0, 0.0);
		}*/

		
		// Enable or Disable ZBuffer test
		if ( (mTnL.Flags.Zbuffer & gRDPOtherMode.z_cmp) | gRDPOtherMode.z_upd )
		{
			glEnable(GL_DEPTH_TEST);
		}
		else
		{
			glDisable(GL_DEPTH_TEST);
		}

		// GL_TRUE to disable z-writes
		glDepthMask( gRDPOtherMode.z_upd ? GL_TRUE : GL_FALSE );
	}
	
	u32 cycle_mode = gRDPOtherMode.cycle_type;

	// Initiate Blender
	//
	if(cycle_mode < CYCLE_COPY && gRDPOtherMode.force_bl)
	{
		InitBlenderMode();
	}
	else
	{
		glDisable( GL_BLEND );
	}
	
	// Initiate Alpha test
	//
	if( (gRDPOtherMode.alpha_compare == G_AC_THRESHOLD) && !gRDPOtherMode.alpha_cvg_sel )
	{
		u8 alpha_threshold = mBlendColour.GetA();
		float alpha_val = (float)alpha_threshold / 255.0f;
		glAlphaFunc( (alpha_threshold | g_ROM.ALPHA_HACK) ? GL_GEQUAL : GL_GREATER, alpha_val);
		glEnable(GL_ALPHA_TEST);
	}
	else if (gRDPOtherMode.cvg_x_alpha)
	{
		// Going over 0x70 breaks OOT, but going lesser than that makes lines on games visible...ex: Paper Mario.
		// Also going over 0x30 breaks the birds in Tarzan :(. Need to find a better way to leverage this.
		glAlphaFunc(GL_GREATER, 0.4392f);
		glEnable(GL_ALPHA_TEST);
	}
	else
	{
		glDisable(GL_ALPHA_TEST);
	}
	
	// Second texture is sampled in 2 cycle mode if text_lod is clear (when set,
	// gRDPOtherMode.text_lod enables mipmapping, but we just set lod_frac to 0.
	bool use_t1 = cycle_mode == CYCLE_2CYCLE;

	bool install_textures[] = { true, use_t1 };
	
	// TODO: Add 2 cycle mode support
	if (install_textures[0]) {
		CNativeTexture *texture = mBoundTexture[0];
		if (texture != NULL) {
			texture->InstallTexture();
			
			if( (gRDPOtherMode.text_filt != G_TF_POINT) | (gGlobalPreferences.ForceLinearFilter) )
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			}
			else
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			}
			
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mTexWrap[0].u);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mTexWrap[0].v);
		}
	}
	
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf((float*)mat_project);
}

void RendererCTR::RenderTriangles(DaedalusVtx *p_vertices, u32 num_vertices, bool disable_zbuffer)
{
	if (mTnL.Flags.Texture)
	{
		UpdateTileSnapshots( mTextureTile );
		
		PrepareRenderState(gProjection.m, disable_zbuffer);
		
		CNativeTexture *texture = mBoundTexture[0];
		if (texture != NULL)
		{
			
			float scale_x {texture->GetScaleX()};
			float scale_y {texture->GetScaleY()};
			
			// Hack to fix the sun in Zelda OOT/MM
			if( g_ROM.ZELDA_HACK && (gRDPOtherMode.L == 0x0c184241) )
			{
				scale_x *= 0.5f;
				scale_y *= 0.5f;
			}
			
			if (mTnL.Flags.Light && mTnL.Flags.TexGen)
			{	
				glDisable(GL_TEXTURE_2D);
			} 
			else
			{
				glEnable(GL_TEXTURE_2D);
				glBegin(GL_TRIANGLES);
				for(u32 i = 0; i < num_vertices; i++)
				{
					glColor4f(p_vertices[i].Colour.GetRf(), p_vertices[i].Colour.GetGf(), p_vertices[i].Colour.GetBf(), p_vertices[i].Colour.GetAf());
					glTexCoord2f(p_vertices[i].Texture.x * scale_x, p_vertices[i].Texture.y * scale_y);
					glVertex3f(p_vertices[i].Position.x, p_vertices[i].Position.y, p_vertices[i].Position.z);
				}
				glEnd();
				glDisable(GL_TEXTURE_2D);

				return;
			}
		}
	}
	else
	{
		PrepareRenderState(gProjection.m, disable_zbuffer);
	}
	
	glBegin(GL_TRIANGLES);
	for(u32 i = 0; i < num_vertices; i++)
	{
		glColor4f(p_vertices[i].Colour.GetRf(), p_vertices[i].Colour.GetGf(), p_vertices[i].Colour.GetBf(), p_vertices[i].Colour.GetAf());
		glVertex3f(p_vertices[i].Position.x, p_vertices[i].Position.y, p_vertices[i].Position.z);
	}
	glEnd();
}

void RendererCTR::TexRect(u32 tile_idx, const v2 & xy0, const v2 & xy1, TexCoord st0, TexCoord st1)
{
	// FIXME(strmnnrmn): in copy mode, depth buffer is always disabled. Might not need to check this explicitly.
	UpdateTileSnapshots( tile_idx );

	// NB: we have to do this after UpdateTileSnapshot, as it set up mTileTopLeft etc.
	// We have to do it before PrepareRenderState, because those values are applied to the graphics state.
	PrepareTexRectUVs(&st0, &st1);

	PrepareRenderState(mScreenToDevice.mRaw, gRDPOtherMode.depth_source ? false : true);
	
	v2 uv0( (float)st0.s / 32.f, (float)st0.t / 32.f );
	v2 uv1( (float)st1.s / 32.f, (float)st1.t / 32.f );

	v2 screen0;
	v2 screen1;
	ConvertN64ToScreen( xy0, screen0 );
	ConvertN64ToScreen( xy1, screen1 );

	const f32 depth = gRDPOtherMode.depth_source ? mPrimDepth : 0.0f;

	CNativeTexture *texture = mBoundTexture[0];
	float scale_x {texture->GetScaleX()};
	float scale_y {texture->GetScaleY()};

	glEnable(GL_TEXTURE_2D);

	glBegin(GL_TRIANGLE_STRIP);
	glColor4f(1.0f,1.0f,1.0f,1.0f);
	glTexCoord2f(uv0.x * scale_x, uv0.y * scale_y);
	glVertex3f(screen0.x, screen0.y, depth);
	glTexCoord2f(uv1.x * scale_x, uv0.y * scale_y);
	glVertex3f(screen1.x, screen0.y, depth);
	glTexCoord2f(uv0.x * scale_x, uv1.y * scale_y);
	glVertex3f(screen0.x, screen1.y, depth);
	glTexCoord2f(uv1.x * scale_x, uv1.y * scale_y);
	glVertex3f(screen1.x, screen1.y, depth);
	glEnd();

	glDisable(GL_TEXTURE_2D);
}

void RendererCTR::TexRectFlip(u32 tile_idx, const v2 & xy0, const v2 & xy1, TexCoord st0, TexCoord st1)
{
	// FIXME(strmnnrmn): in copy mode, depth buffer is always disabled. Might not need to check this explicitly.
	UpdateTileSnapshots( tile_idx );

	// NB: we have to do this after UpdateTileSnapshot, as it set up mTileTopLeft etc.
	// We have to do it before PrepareRenderState, because those values are applied to the graphics state.
	PrepareTexRectUVs(&st0, &st1);

	PrepareRenderState(mScreenToDevice.mRaw, gRDPOtherMode.depth_source ? false : true);

	v2 uv0( (float)st0.s / 32.f, (float)st0.t / 32.f );
	v2 uv1( (float)st1.s / 32.f, (float)st1.t / 32.f );

	v2 screen0;
	v2 screen1;
	ConvertN64ToScreen( xy0, screen0 );
	ConvertN64ToScreen( xy1, screen1 );

	const f32 depth = gRDPOtherMode.depth_source ? mPrimDepth : 0.0f;

	CNativeTexture *texture = mBoundTexture[0];
	float scale_x {texture->GetScaleX()};
	float scale_y {texture->GetScaleY()};

	glEnable(GL_TEXTURE_2D);

	glBegin(GL_TRIANGLE_STRIP);
	glColor4f(1.0f,1.0f,1.0f,1.0f);
	glTexCoord2f(uv0.x * scale_x, uv0.y * scale_y);
	glVertex3f(screen0.x, screen0.y, depth);
	glTexCoord2f(uv1.x * scale_x, uv0.y * scale_y);
	glVertex3f(screen1.x, screen0.y, depth);
	glTexCoord2f(uv0.x * scale_x, uv1.y * scale_y);
	glVertex3f(screen0.x, screen1.y, depth);
	glTexCoord2f(uv1.x * scale_x, uv1.y * scale_y);
	glVertex3f(screen1.x, screen1.y, depth);
	glEnd();

	glDisable(GL_TEXTURE_2D);
}

void RendererCTR::FillRect(const v2 & xy0, const v2 & xy1, u32 color)
{
	PrepareRenderState(mScreenToDevice.mRaw, gRDPOtherMode.depth_source ? false : true);
	
	v2 screen0;
	v2 screen1;
	ConvertN64ToScreen( xy0, screen0 );
	ConvertN64ToScreen( xy1, screen1 );
	
	const f32 depth = gRDPOtherMode.depth_source ? mPrimDepth : 0.0f;
	
	glBegin(GL_TRIANGLE_STRIP);
	glColor4ubv((GLubyte *)&color);
	glVertex3f(screen0.x, screen0.y, depth);
	glVertex3f(screen1.x, screen0.y, depth);
	glVertex3f(screen0.x, screen1.y, depth);
	glVertex3f(screen1.x, screen1.y, depth);
	glEnd();

	glEnableClientState(GL_COLOR_ARRAY);
}

void RendererCTR::Draw2DTexture(f32 x0, f32 y0, f32 x1, f32 y1,
								f32 u0, f32 v0, f32 u1, f32 v1,
								const CNativeTexture * texture)
{
	gRDPOtherMode.cycle_type = CYCLE_COPY;
	PrepareRenderState(mScreenToDevice.mRaw, false);
	
	glEnable(GL_BLEND);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	
	float sx0 = N64ToScreenX(x0);
	float sy0 = N64ToScreenY(y0);

	float sx1 = N64ToScreenX(x1);
	float sy1 = N64ToScreenY(y1);

	const f32 depth = 0.0f;

	glEnable(GL_TEXTURE_2D);

	glBegin(GL_TRIANGLE_STRIP);
	glColor4f(1.0f,1.0f,1.0f,1.0f);
	glTexCoord2f(u0, v0);
	glVertex3f(sx0, sy0, depth);
	glTexCoord2f(u1, v0);
	glVertex3f(sx1, sy0, depth);
	glTexCoord2f(u0, v1);
	glVertex3f(sx0, sy1, depth);
	glTexCoord2f(u1, v1);
	glVertex3f(sx1, sy1, depth);
	glEnd();

	glDisable(GL_TEXTURE_2D);

}

void RendererCTR::Draw2DTextureR(f32 x0, f32 y0, f32 x1, f32 y1,
								 f32 x2, f32 y2, f32 x3, f32 y3,
								 f32 s, f32 t)
{
	gRDPOtherMode.cycle_type = CYCLE_COPY;
	PrepareRenderState(mScreenToDevice.mRaw, false);
	
	glEnable(GL_BLEND);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	
	float sx0 = N64ToScreenX(x0);
	float sy0 = N64ToScreenY(y0);

	float sx1 = N64ToScreenX(x1);
	float sy1 = N64ToScreenY(y1);

	const f32 depth = 0.0f;

	glEnable(GL_TEXTURE_2D);

	glBegin(GL_TRIANGLE_STRIP);
	glColor4f(1.0f,1.0f,1.0f,1.0f);
	glTexCoord2f(0, 0);
	glVertex3f(N64ToScreenX(x0), N64ToScreenY(y0), depth);
	glTexCoord2f(s, 0);
	glVertex3f(N64ToScreenX(x1), N64ToScreenY(y1), depth);
	glTexCoord2f(s, t);
	glVertex3f(N64ToScreenX(x2), N64ToScreenY(y2), depth);
	glTexCoord2f(0, t);
	glVertex3f(N64ToScreenX(x3), N64ToScreenY(y3), depth);
	glEnd();

	glDisable(GL_TEXTURE_2D);
}

bool CreateRenderer()
{
	gRendererCTR = new RendererCTR();
	gRenderer    = gRendererCTR;
	return true;
}

void DestroyRenderer()
{
	delete gRendererCTR;
	gRendererCTR = nullptr;
	gRenderer    = nullptr;
}
