#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QThreadPool>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

#include "json_bridge.hpp"

namespace sns::ipc {
class JsonRpcClient;
}

namespace sns::app {

// Owns the .NET Bethesda worker process and the JSON-RPC connection to it. Calls run on a thread
// pool (they block on the pipe); results are delivered back on the GUI thread.
class WorkerClient : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")
  Q_PROPERTY(State state READ state NOTIFY stateChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
  Q_PROPERTY(QString workerVersion READ workerVersion NOTIFY stateChanged)
  Q_PROPERTY(QString mutagenVersion READ mutagenVersion NOTIFY stateChanged)

 public:
  enum class State { Stopped, Starting, Ready, Failed };
  Q_ENUM(State)

  using ResultHandler = std::function<void(const Json&)>;
  using ErrorHandler = std::function<void(int code, const QString& message, const Json& data)>;
  using CancelToken = std::shared_ptr<std::atomic<bool>>;

  WorkerClient(QString executable, QObject* parent = nullptr);
  ~WorkerClient() override;

  [[nodiscard]] State state() const { return state_; }
  [[nodiscard]] QString statusText() const { return status_; }
  [[nodiscard]] QString workerVersion() const { return worker_version_; }
  [[nodiscard]] QString mutagenVersion() const { return mutagen_version_; }

  Q_INVOKABLE void start();
  Q_INVOKABLE void stop();

  // Sends a request. Handlers run on the GUI thread, and only while `context` is alive.
  CancelToken call(const QString& method, Json params, std::chrono::milliseconds timeout, QObject* context,
                   ResultHandler on_result, ErrorHandler on_error);

 signals:
  void stateChanged();
  void ready();
  void logLine(const QString& level, const QString& text);
  void notification(const QString& method, const QVariantMap& params);

 private:
  void set_state(State state, const QString& status);
  void read_stderr();

  QString executable_;
  QProcess* process_ = nullptr;
  std::shared_ptr<ipc::JsonRpcClient> client_;
  QThreadPool pool_;
  State state_ = State::Stopped;
  QString status_ = tr("Worker detenido");
  QString worker_version_;
  QString mutagen_version_;
  QByteArray stderr_buffer_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

}  // namespace sns::app
