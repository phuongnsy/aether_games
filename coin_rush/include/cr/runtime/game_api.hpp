// The narrow interface screens drive game flow through — a handful of verbs,
// not the game class (no friendship). Implemented by the app composition root.
#pragma once

namespace game {

class GameApi {
 public:
  virtual ~GameApi() = default;

  // Flow verbs.
  virtual void StartLevel(int index) = 0;
  virtual void NextLevel() = 0;
  virtual void ToMenu() = 0;
  virtual void RequestQuit() = 0;

  // Read-only state the screens display.
  [[nodiscard]] virtual int LevelCount() const = 0;
  [[nodiscard]] virtual int CurrentLevel() const = 0;
  [[nodiscard]] virtual bool HasNextLevel() const = 0;
};

}  // namespace game
