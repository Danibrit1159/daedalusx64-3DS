/*
Copyright (C) 2003 Azimer
Copyright (C) 2001,2006 StrmnNrmn

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

//
//	N.B. This source code is derived from Azimer's Audio plugin (v0.55?)
//	and modified by StrmnNrmn to work with Daedalus PSP. Thanks Azimer!
//	Drop me a line if you get chance :)
//

#include "stdafx.h"
#include <stdio.h>
#include <new>

#include <3ds.h>

#include "Plugins/AudioPlugin.h"
#include "HLEAudio/audiohle.h"

#include "Config/ConfigOptions.h"
#include "Core/CPU.h"
#include "Core/Interrupt.h"
#include "Core/Memory.h"
#include "Core/ROM.h"
#include "Core/RSP_HLE.h"
#include "Debug/DBGConsole.h"
#include "HLEAudio/AudioBuffer.h"
#include "Utility/FramerateLimiter.h"
#include "Utility/Thread.h"

#define RSP_AUDIO_INTR_CYCLES     1
extern u32 gSoundSync;

static const u32	kOutputFrequency {44100};
static const u32	MAX_OUTPUT_FREQUENCY {kOutputFrequency * 4};


static bool audio_open {false};


// Large kAudioBufferSize creates huge delay on sound //Corn
static const u32	kAudioBufferSize {1024 * 2}; // OSX uses a circular buffer length, 1024 * 1024


class AudioPluginCTR : public CAudioPlugin
{
public:

 AudioPluginCTR();
	virtual ~AudioPluginCTR();
	virtual bool			StartEmulation();
	virtual void			StopEmulation();

	virtual void			DacrateChanged( int system_type );
	virtual void			LenChanged();
	virtual u32				ReadLength() {return 0;}
	virtual EProcessResult	ProcessAList();

	//virtual void SetFrequency(u32 frequency);
	virtual void AddBuffer( u8 * start, u32 length);
	virtual void FillBuffer( Sample * buffer, u32 num_samples);

	virtual void StopAudio();
	virtual void StartAudio();

public:
  CAudioBuffer *		mAudioBufferUncached;

private:
	CAudioBuffer * mAudioBuffer;
	bool mKeepRunning;
	bool mExitAudioThread;
	u32 mFrequency;
	s32 mAudioThread;
	s32 mSemaphore;
//	u32 mBufferLenMs;
};

static AudioPluginCTR * ac;

void AudioPluginCTR::FillBuffer(Sample * buffer, u32 num_samples)
{
	mAudioBufferUncached->Drain( buffer, num_samples );
}


EAudioPluginMode gAudioPluginEnabled( APM_DISABLED );


AudioPluginCTR::AudioPluginCTR()
:mKeepRunning (false)
//: mAudioBuffer( kAudioBufferSize )
, mFrequency( 44100 )
//, mAudioThread ( kInvalidThreadHandle )
//, mKeepRunning( false )
//, mBufferLenMs ( 0 )
{
	// Allocate audio buffer with malloc_64 to avoid cached/uncached aliasing
	void * mem = malloc( sizeof( CAudioBuffer ) );
	mAudioBuffer = new( mem ) CAudioBuffer( kAudioBufferSize );
	mAudioBufferUncached = (CAudioBuffer*)MAKE_UNCACHED_PTR(mem);
	// Ideally we could just invalidate this range?
	//dcache_wbinv_range_unaligned( mAudioBuffer, mAudioBuffer+sizeof( CAudioBuffer ) );
}

AudioPluginCTR::~AudioPluginCTR( )
{
	mAudioBuffer->~CAudioBuffer();
	free(mAudioBuffer);
	// pspAudioEnd();
}

bool		AudioPluginCTR::StartEmulation()
{
	return true;
}


void	AudioPluginCTR::StopEmulation()
{
    Audio_Reset();
  	StopAudio();


}

void	AudioPluginCTR::DacrateChanged( int system_type )
{
u32 clock = (system_type == ST_NTSC) ? VI_NTSC_CLOCK : VI_PAL_CLOCK;
u32 dacrate = Memory_AI_GetRegister(AI_DACRATE_REG);
u32 frequency = clock / (dacrate + 1);

#ifdef DAEDALUS_DEBUG_CONSOLE
DBGConsole_Msg(0, "Audio frequency: %d", frequency);
#endif
mFrequency = frequency;
}


void	AudioPluginCTR::LenChanged()
{
	if( gAudioPluginEnabled > APM_DISABLED )
	{
		u32 address = Memory_AI_GetRegister(AI_DRAM_ADDR_REG) & 0xFFFFFF;
		u32	length = Memory_AI_GetRegister(AI_LEN_REG);

		AddBuffer( g_pu8RamBase + address, length );
	}
	else
	{
		StopAudio();
	}
}

EProcessResult	AudioPluginCTR::ProcessAList()
{
	Memory_SP_SetRegisterBits(SP_STATUS_REG, SP_STATUS_HALT);

	EProcessResult	result = PR_NOT_STARTED;

	switch( gAudioPluginEnabled )
	{
		case APM_DISABLED:
			result = PR_COMPLETED;
			break;
		case APM_ENABLED_ASYNC:
			{
				//SHLEStartJob	job;
				//gJobManager.AddJob( &job, sizeof( job ) );
			}
			result = PR_STARTED;
			break;
		case APM_ENABLED_SYNC:
			Audio_Ucode();
			result = PR_COMPLETED;
			break;
	}

	return result;
}


void audioCallback( void * buf, unsigned int length, void * userdata )
{
	AudioPluginCTR * ac( reinterpret_cast< AudioPluginCTR * >( userdata ) );

	ac->FillBuffer( reinterpret_cast< Sample * >( buf ), length );
}


void AudioPluginCTR::StartAudio()
{
	if (mKeepRunning)
		return;

	mKeepRunning = true;

	ac = this;


	//pspAudioInit();
	//pspAudioSetChannelCallback( 0, audioCallback, this );

	// Everything OK
	audio_open = true;
}

void AudioPluginCTR::AddBuffer( u8 *start, u32 length )
{
	if (length == 0)
		return;

	if (!mKeepRunning)
		StartAudio();

	u32 num_samples {length / sizeof( Sample )};

	switch( gAudioPluginEnabled )
	{
	case APM_DISABLED:
		break;

	case APM_ENABLED_ASYNC:
		{
			//SAddSamplesJob	job( mAudioBufferUncached, reinterpret_cast< const Sample * >( start ), num_samples, mFrequency, kOutputFrequency );

			//gJobManager.AddJob( &job, sizeof( job ) );
		}
		break;

	case APM_ENABLED_SYNC:
		{
			//mAudioBufferUncached->AddSamples( reinterpret_cast< const Sample * >( start ), num_samples, mFrequency, kOutputFrequency );
		}
		break;
	}

	/*
	u32 remaining_samples = mAudioBuffer.GetNumBufferedSamples();
	mBufferLenMs = (1000 * remaining_samples) / kOutputFrequency);
	float ms = (float) num_samples * 1000.f / (float)mFrequency;
	#ifdef DAEDALUS_DEBUG_CONSOLE
	DPF_AUDIO("Queuing %d samples @%dHz - %.2fms - bufferlen now %d\n", num_samples, mFrequency, ms, mBufferLenMs);
	#endif
	*/
}

void AudioPluginCTR::StopAudio()
{
	if (!mKeepRunning)
		return;

	mKeepRunning = false;

	audio_open = false;
}

CAudioPlugin *		CreateAudioPlugin()
{
	return new AudioPluginCTR();
}
