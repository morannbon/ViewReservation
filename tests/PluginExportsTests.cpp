#include <windows.h>
#include <cstdio>
int wmain(int argc,wchar_t **argv) {
 if(argc!=2) return 1;
 HMODULE module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
 if(!module) {std::printf("load error=%lu\n",GetLastError());return 2;}
 const char *names[]={"TVTGetVersion","TVTGetPluginInfo","TVTInitialize","TVTFinalize"};
 int result=0;
 for(auto name:names) {bool ok=GetProcAddress(module,name)!=nullptr;std::printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)result=3;}
 FreeLibrary(module);return result;
}
