#include <stdlib.h>
#include <stdio.h>

#include <3ds.h>
#include <GL/picaGL.h>

#include "BuildOptions.h"
#include "Config/ConfigOptions.h"
#include "Core/Cheats.h"
#include "Core/CPU.h"
#include "Core/CPU.h"
#include "Core/Memory.h"
#include "Core/PIF.h"
#include "Core/RomSettings.h"
#include "Core/Save.h"
#include "Debug/DBGConsole.h"
#include "Debug/DebugLog.h"
#include "Graphics/GraphicsContext.h"
#include "HLEGraphics/TextureCache.h"
#include "Input/InputManager.h"
#include "Interface/RomDB.h"
#include "System/Paths.h"
#include "System/System.h"
#include "Test/BatchTest.h"
#include "Utility/IO.h"
#include "Utility/Preferences.h"
#include "Utility/Profiler.h"
#include "Utility/Thread.h"
#include "Utility/Translate.h"
#include "Utility/Timer.h"

#define DAEDALUS_CTR_PATH(p)	"sdmc:/3ds/DaedalusX64/" p
#define LOAD_ROM	DAEDALUS_CTR_PATH("mario.z64")

/*extern "C" {

int __stacksize__ = 4 * 1024;
u32 __ctru_linear_heap_size = 8 * 1024 * 1024;

}*/

void log2file(const char *format, ...) {
	__gnuc_va_list arg;
	int done;
	va_start(arg, format);
	char msg[512];
	done = vsprintf(msg, format, arg);
	va_end(arg);
	sprintf(msg, "%s\n", msg);
	FILE *log = fopen("sdmc:/DaedalusX64.log", "a+");
	if (log != NULL) {
		fwrite(msg, 1, strlen(msg), log);
		fclose(log);
	}
}

static void Initialize()
{
	osSetSpeedupEnable(true);
	
	gfxInit(GSP_RGB565_OES,GSP_RGB565_OES,false);

	gfxSetDoubleBuffering(GFX_TOP, false);
	gfxSetDoubleBuffering(GFX_BOTTOM, false);

	consoleInit(GFX_BOTTOM, NULL);

	strcpy(gDaedalusExePath, DAEDALUS_CTR_PATH(""));
	strcpy(g_DaedalusConfig.mSaveDir, DAEDALUS_CTR_PATH("SaveGames/"));

	bool ret = System_Init();
}

void HandleEndOfFrame()
{
	pglSwapBuffers();

	if(hidKeysDown() == KEY_ZL)
		CPU_Halt("Exiting");
}

int main(int argc, char* argv[])
{

	Initialize();

	System_Open(LOAD_ROM);

	CPU_Run();

	System_Close();
	System_Finalize();

	return 0;
}
