#include "worker_client.hpp"

#include <windows.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>

#include "sns/ipc/json_rpc_client.hpp"
#include "sns/ipc/protocol.hpp"
#include "sns/ipc/transport.hpp"
#include "sns/ipc/worker_process.hpp"

namespace sns::app {

using namespace std::chrono_literals;

WorkerClient::WorkerClient(QString executable, QObject* parent)
    : QObject(parent), executable_(std::move(executable)) {
  pool_.setMaxThreadCount(4);
}

WorkerClient::~WorkerClient() {
  *alive_ = false;
  stop();
  pool_.waitForDone(5000);
}

void WorkerClient::set_state(State state, const QString& status) {
  state_ = state;
  status_ = status;
  emit stateChanged();
}

void WorkerClient::start() {
  if (state_ == State::Starting || state_ == State::Ready) return;
  if (!QFileInfo::exists(executable_)) {
    set_state(State::Failed, tr("No se encuentra el worker: %1").arg(executable_));
    return;
  }

  const auto pipe = qs(ipc::WorkerProcess::make_pipe_name());
  process_ = new QProcess(this);
  process_->setProgram(executable_);
  process_->setArguments({"--pipe", pipe, "--parent-pid", QString::number(QCoreApplication::applicationPid())});
  process_->setWorkingDirectory(QFileInfo(executable_).absolutePath());
  process_->setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
  connect(process_, &QProcess::readyReadStandardError, this, &WorkerClient::read_stderr);
  connect(process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
    client_.reset();
    if (state_ != State::Stopped) set_state(State::Failed, tr("El worker terminó (código %1)").arg(code));
  });
  set_state(State::Starting, tr("Iniciando worker…"));
  process_->start();
  if (!process_->waitForStarted(10000)) {
    set_state(State::Failed, tr("No se pudo iniciar el worker: %1").arg(process_->errorString()));
    return;
  }

  const auto pid = static_cast<std::uint32_t>(process_->processId());
  QPointer<WorkerClient> self(this);
  pool_.start([self, pipe, pid, alive = alive_] {
    std::shared_ptr<ipc::JsonRpcClient> client;
    ipc::HandshakeResult hello;
    QString error;
    try {
      auto transport = ipc::NamedPipeTransport::connect(ss(pipe), 30s, pid);
      client = std::make_shared<ipc::JsonRpcClient>(std::move(transport));
      client->set_notification_handler([self, alive](const std::string& method, const Json& params) {
        if (!*alive) return;
        const auto m = qs(method);
        const auto p = to_map(params);
        QMetaObject::invokeMethod(self, [self, m, p] { if (self) emit self->notification(m, p); }, Qt::QueuedConnection);
      });
      client->set_disconnect_handler([self, alive](const std::string& reason) {
        if (!*alive) return;
        const auto r = qs(reason);
        QMetaObject::invokeMethod(self, [self, r] {
          if (self && self->state_ == State::Ready) self->set_state(State::Failed, tr("Conexión con el worker perdida: %1").arg(r));
        }, Qt::QueuedConnection);
      });
      client->start();
      hello = ipc::handshake(*client, "SkyrimNpcStudio", SNS_VERSION);
    } catch (const std::exception& ex) {
      error = qs(ex.what());
    }
    if (!*alive) return;
    QMetaObject::invokeMethod(self, [self, client, hello, error] {
      if (!self || self->state_ != State::Starting) {
        if (client) client->close();  // stopped while the handshake was in flight
        return;
      }
      if (!error.isEmpty()) {
        self->set_state(State::Failed, tr("Handshake fallido: %1").arg(error));
        return;
      }
      self->client_ = client;
      self->worker_version_ = qs(hello.worker_version);
      self->mutagen_version_ = qs(hello.mutagen_version);
      self->set_state(State::Ready, tr("Worker %1 listo (Mutagen %2, protocolo %3.%4)")
                                        .arg(self->worker_version_, self->mutagen_version_)
                                        .arg(hello.protocol_major)
                                        .arg(hello.protocol_minor));
      emit self->ready();
    }, Qt::QueuedConnection);
  });
}

void WorkerClient::stop() {
  if (state_ == State::Stopped) return;
  state_ = State::Stopped;
  if (auto client = client_) {
    try {
      client->call(ipc::protocol::method::kShutdown, nullptr, 2s);
    } catch (const std::exception&) {
      // The worker may already be gone; it also exits on disconnect.
    }
    client->close();
  }
  client_.reset();
  if (process_) {
    process_->disconnect(this);
    if (!process_->waitForFinished(3000)) {
      process_->kill();  // asynchronous: wait for it so the QProcess is never destroyed while running
      process_->waitForFinished(3000);
    }
    delete process_;
    process_ = nullptr;
  }
  status_ = tr("Worker detenido");
  emit stateChanged();
}

WorkerClient::CancelToken WorkerClient::call(const QString& method, Json params, std::chrono::milliseconds timeout,
                                             QObject* context, ResultHandler on_result, ErrorHandler on_error) {
  auto cancel = std::make_shared<std::atomic<bool>>(false);
  auto client = client_;
  QPointer<QObject> ctx(context);
  if (!client) {
    QMetaObject::invokeMethod(context, [ctx, on_error] {
      if (ctx && on_error) on_error(ipc::protocol::error::kClientDisconnected, tr("El worker no está conectado"), nullptr);
    }, Qt::QueuedConnection);
    return cancel;
  }

  pool_.start([client, method = ss(method), params = std::move(params), timeout, cancel, ctx, on_result, on_error] {
    try {
      auto result = client->call(method, params, timeout, cancel.get());
      QMetaObject::invokeMethod(ctx, [ctx, on_result, result = std::move(result)] {
        if (ctx && on_result) on_result(result);
      }, Qt::QueuedConnection);
    } catch (const ipc::RpcError& e) {
      const auto message = qs(e.what());
      QMetaObject::invokeMethod(ctx, [ctx, on_error, code = e.code(), message, data = e.data()] {
        if (ctx && on_error) on_error(code, message, data);
      }, Qt::QueuedConnection);
    } catch (const std::exception& e) {
      const auto message = qs(e.what());
      QMetaObject::invokeMethod(ctx, [ctx, on_error, message] {
        if (ctx && on_error) on_error(ipc::protocol::error::kInternalError, message, nullptr);
      }, Qt::QueuedConnection);
    }
  });
  return cancel;
}

void WorkerClient::read_stderr() {
  stderr_buffer_ += process_->readAllStandardError();
  qsizetype newline;
  while ((newline = stderr_buffer_.indexOf('\n')) >= 0) {
    const auto line = QString::fromUtf8(stderr_buffer_.left(newline)).trimmed();
    stderr_buffer_.remove(0, newline + 1);
    if (line.isEmpty()) continue;
    const auto level = line.contains("[ERROR]") ? "error" : line.contains("[WARN]") ? "warning" : "info";
    emit logLine(QString::fromLatin1(level), line);
  }
}

}  // namespace sns::app
