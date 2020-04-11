#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>

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
#include "Utility/ROMFile.h"

#define DAEDALUS_CTR_PATH(p)	"sdmc:/3ds/DaedalusX64/" p

EAudioPluginMode enable_audio = APM_ENABLED_SYNC;
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
	if(hidKeysHeld() == KEY_SELECT)
		CPU_Halt("Exiting");
}

const char *SelectRom()
{
	std::vector<std::string> files = {};

	std::string			full_path;

	IO::FindHandleT		find_handle;
	IO::FindDataT		find_data;
	
	if(IO::FindFileOpen( DAEDALUS_CTR_PATH("Roms/"), &find_handle, find_data ))
	{
		do
		{
			const char * rom_filename( find_data.Name );

			if(IsRomfilename( rom_filename ))
			{
				files.push_back(rom_filename);
			}
		}

		while(IO::FindFileNext( find_handle, find_data ));

		IO::FindFileClose( find_handle );
	}

	if(files.size() == 0)
		return nullptr;

	int cursor = 0;

	while(aptMainLoop())
	{
		hidScanInput();

		printf("\x1b[1;1H");

		for(int i = 0; i < files.size(); i++)
		{
			if(cursor == i)
				printf("\x1b[34m%s\x1b[0m\n", files.at(i).c_str());
			else
				printf("%s\n", files.at(i).c_str());
		}

		if((hidKeysDown() & KEY_DOWN) && cursor != files.size() - 1)
			cursor++;
		if((hidKeysDown() & KEY_UP) && cursor != 0)
			cursor--;
		if(hidKeysDown() & KEY_A)
			return files.at(cursor).c_str();
	}

}

int main(int argc, char* argv[])
{

	Initialize();

	const char *rom = SelectRom();
	
	char fullpath[512];

	sprintf(fullpath, "%s%s", DAEDALUS_CTR_PATH("Roms/"), rom);

	System_Open(fullpath);

	consoleClear();
	
	CPU_Run();

	System_Close();
	System_Finalize();

	return 0;
}
