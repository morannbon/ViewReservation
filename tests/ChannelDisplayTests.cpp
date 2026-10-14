#include "../src/DriverCache.h"
#include <cstdio>
struct Case { WORD Nid; TVTest::TuningSpaceType Space; const wchar_t *Name; ChannelDisplayWave Expected; };
int main() {
 const Case cases[] = {
  {0xFF00, TVTest::TUNINGSPACE_UNKNOWN, L"", ChannelDisplayWave::Catv},
  {0xFF00, TVTest::TUNINGSPACE_TERRESTRIAL, L"地上波", ChannelDisplayWave::Catv},
  {0xFF00, TVTest::TUNINGSPACE_BS, L"BS", ChannelDisplayWave::Catv},
  {4, TVTest::TUNINGSPACE_UNKNOWN, L"CATV", ChannelDisplayWave::Bs},
  {4, TVTest::TUNINGSPACE_TERRESTRIAL, L"Mixed", ChannelDisplayWave::Bs},
  {6, TVTest::TUNINGSPACE_UNKNOWN, L"CATV", ChannelDisplayWave::Cs},
  {7, TVTest::TUNINGSPACE_BS, L"Cable", ChannelDisplayWave::Cs},
  {10, TVTest::TUNINGSPACE_UNKNOWN, L"", ChannelDisplayWave::Cs},
  {0x7880, TVTest::TUNINGSPACE_BS, L"CATV", ChannelDisplayWave::Terrestrial},
  {0x7FE8, TVTest::TUNINGSPACE_UNKNOWN, L"", ChannelDisplayWave::Terrestrial},
  {0x787F, TVTest::TUNINGSPACE_UNKNOWN, L"", ChannelDisplayWave::Catv},
  {0x7FE9, TVTest::TUNINGSPACE_UNKNOWN, L"", ChannelDisplayWave::Catv},
  {0, TVTest::TUNINGSPACE_BS, L"", ChannelDisplayWave::Bs},
  {0, TVTest::TUNINGSPACE_110CS, L"", ChannelDisplayWave::Cs},
  {0, TVTest::TUNINGSPACE_TERRESTRIAL, L"", ChannelDisplayWave::Terrestrial}
 };
 for (const auto &c : cases) {
  DriverChannelEntry e{}; e.NetworkID=c.Nid; e.SpaceType=c.Space; e.SpaceName=c.Name;
  e.DriverName=L"BonDriver_BDA.dll"; e.Space=3; e.Channel=17; e.TransportStreamID=123; e.ServiceID=456;
  if (ResolveChannelDisplayWave(e)!=c.Expected) return 1;
  if (e.DriverName!=L"BonDriver_BDA.dll" || e.Space!=3 || e.Channel!=17 || e.TransportStreamID!=123 || e.ServiceID!=456) return 2;
 }
 std::printf("15/15 classification cases passed; tuning coordinates unchanged\n");
 return 0;
}
