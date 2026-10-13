#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace sns::app::forge {
enum class BridgeState { Unconfigured, Available, Connecting, Connected, Working, Error };
enum class BridgeOperation { InspectScene, OpenScene, ImportModel, ExportIntermediate, Synchronize };
struct BridgeStatus {
  BridgeState state = BridgeState::Unconfigured;
  std::string verified_version;
  std::string diagnostic;
};
struct BridgeResult {
  bool verified = false;
  std::vector<std::filesystem::path> outputs;
  std::string diagnostic;
};
// Future service boundary: operation enum + file inputs, never arbitrary Python or shell text.
// No implementation is registered; UI therefore cannot advertise a connected bridge.
class BlenderBridge {
 public:
  virtual ~BlenderBridge() = default;
  virtual BridgeStatus status() const = 0;
  virtual void run(BridgeOperation operation, const std::filesystem::path& input,
                   std::function<void(BridgeResult)> completed) = 0;
  virtual void cancel() = 0;
};
}  // namespace sns::app::forge
