#ifndef _GUI_H_
#define _GUI_H_

#include <memory>
namespace Online { class PlayerHost; }
#include <string>
#include <vector>

namespace Util
{
  namespace Config
  {
    class Node;
  }
}

struct GuiOptions {
  std::string relay, romFile, gameXML, returnNotice;
  bool asyncEnabled = false;
};
struct GuiResult {
  std::vector<std::string> rom_files;
  std::string relay, ticket, player, gameRelay, directoryRelay;
  std::shared_ptr<Online::PlayerHost> playerHost;
  bool online = false, failed = false, secretCars = false, hornet = false, rallyEight = false, realPlayers = false, noCollisions = false, spectate = false;
  uint32_t nativeRoomOptions = 0, nativeCarNumber = 0;
};
// Runs the settings GUI. fileConfig is the complete contents of the config
// file; only its global section is edited, all other sections are preserved
// when the file is written back out.
GuiResult RunGUI(const std::string& configPath, const Util::Config::Node& fileConfig, const GuiOptions& options = {});

#endif // !_GUI_H_
