// ROM-free platform parsing regression tests. GPL-3.0-or-later.
#include "GameLoader.h"
#include "OSD/Logger.h"
#include <cassert>
#include <fstream>
#include <iostream>

class CaptureLogger : public CLogger
{
public:
  std::vector<std::string> errors;
  void DebugLog(const char *, va_list) override {}
  void InfoLog(const char *, va_list) override {}
  void ErrorLog(const char *fmt, va_list args) override
  {
    char text[4096];
    vsnprintf(text, sizeof(text), fmt, args);
    errors.emplace_back(text);
  }
};

int main(int argc, char **argv)
{
  assert(argc == 4); // scratch XML, bundled database, mixed-case database
  auto logger = std::make_shared<CaptureLogger>();
  SetLogger(logger);
  unsigned checked = 0;
  const auto check = [&](const std::string &platform, const std::string &stepping,
                         Game::Platform expectedPlatform, Game::Stepping expectedStepping,
                         bool valid = true, unsigned errors = 0)
  {
    {
      std::ofstream xml(argv[1]);
      xml << "<games><game name=\"test\"><hardware><platform>" << platform
          << "</platform><stepping>" << stepping
          << "</stepping></hardware><roms><region name=\"cpu_rom\" stride=\"1\" chunk_size=\"1\">"
          << "<file name=\"test.bin\" offset=\"0\" /></region></roms></game>"
          << "<game name=\"control\"><hardware><stepping>2.1</stepping></hardware>"
          << "<roms><region name=\"cpu_rom\" stride=\"1\" chunk_size=\"1\">"
          << "<file name=\"control.bin\" offset=\"0\" /></region></roms></game></games>";
      assert(xml.good());
    }
    logger->errors.clear();
    GameLoader loader(argv[1]);
    if (logger->errors.size() != errors)
    {
      std::cerr << "Unexpected diagnostics for " << platform << " / " << stepping << '\n';
      for (const auto &error : logger->errors) std::cerr << error << '\n';
    }
    assert(logger->errors.size() == errors);
    assert(loader.GetGames().count("test") == (valid ? 1u : 0u));
    assert(loader.GetGames().at("control").platform == Game::PLATFORM_MODEL3);
    assert(loader.GetGames().at("control").stepping == Game::Stepping::MODEL3_2_1);
    if (valid)
    {
      const auto &game = loader.GetGames().at("test");
      assert(game.platform == expectedPlatform);
      assert(game.stepping == expectedStepping);
    }
    ++checked;
  };
  for (const std::string platform : {"Real3D PRO-1000", "Real3D Pro-1000", "real3d pro-1000", "REAL3D PRO-1000"})
  {
    check(platform, "1100", Game::PLATFORM_PRO1000, Game::Stepping::PRO1000_1100);
    check(platform, "", Game::PLATFORM_PRO1000, Game::Stepping::PRO1000_1100);
    check(platform, "1200", Game::PLATFORM_PRO1000, Game::Stepping::PRO1000_1200);
    check(platform, "1400", Game::PLATFORM_PRO1000, Game::Stepping::PRO1000_1400);
    // Do not accidentally accept a Model 3 stepping for the PRO-1000.
    check(platform, "2.1", Game::PLATFORM_PRO1000, Game::Stepping::UNKNOWN, false, 1);
  }
  check("", "1.0", Game::PLATFORM_MODEL3, Game::Stepping::MODEL3_1_0);
  check("Sega Model 3", "1.5", Game::PLATFORM_MODEL3, Game::Stepping::MODEL3_1_5);
  check("Sega Model 3", "2.0", Game::PLATFORM_MODEL3, Game::Stepping::MODEL3_2_0);
  check("Sega Model 3", "2.1", Game::PLATFORM_MODEL3, Game::Stepping::MODEL3_2_1);
  check("Sega Model 1", "", Game::PLATFORM_MODEL1, Game::Stepping::MODEL1);
  check("Sega Model 2", "", Game::PLATFORM_MODEL2, Game::Stepping::MODEL2A);
  check("Sega Model 2", "2C", Game::PLATFORM_MODEL2, Game::Stepping::MODEL2C);
  check("Sega Model 3", "1100", Game::PLATFORM_MODEL3, Game::Stepping::UNKNOWN, false, 1);
  // Unknown platforms must still produce a diagnostic and use the Model 3 fallback.
  check("Unknown hardware", "2.1", Game::PLATFORM_MODEL3, Game::Stepping::MODEL3_2_1, true, 1);
  check("Unknown hardware", "1100", Game::PLATFORM_MODEL3, Game::Stepping::UNKNOWN, false, 2);

  // The GUI and emulation startup each construct a loader. Both databases must
  // parse without errors, including the user's spelling in an otherwise real DB.
  for (int pass = 0; pass < 2; ++pass)
  {
    logger->errors.clear();
    GameLoader canonical(argv[2]);
    GameLoader mixed(argv[3]);
    assert(logger->errors.empty());
    assert(!canonical.GetGames().empty());
    assert(canonical.GetGames().size() == mixed.GetGames().size());
    for (const auto &entry : canonical.GetGames())
    {
      const auto &other = mixed.GetGames().at(entry.first);
      assert(entry.second.platform == other.platform);
      assert(entry.second.stepping == other.stepping);
    }
    assert(mixed.GetGames().at("pro1000").platform == Game::PLATFORM_PRO1000);
    assert(mixed.GetGames().at("pro1000").stepping == Game::Stepping::PRO1000_1100);
  }
  SetLogger(nullptr);
  std::cout << "PASS " << checked << " platform/stepping cases and repeated full-database loads\n";
}
