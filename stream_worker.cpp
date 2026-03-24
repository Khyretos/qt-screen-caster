// stream_worker.cpp - xdg-desktop-portal D-Bus screen capture with PipeWire
// linking
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <csignal>
#include <iostream>

// ======================================================================
// PortalScreenCast – calls xdg-desktop-portal ScreenCast via D-Bus
// ======================================================================
class PortalScreenCast : public QObject {
  Q_OBJECT

  static constexpr const char *SVC = "org.freedesktop.portal.Desktop";
  static constexpr const char *OBJ = "/org/freedesktop/portal/desktop";
  static constexpr const char *SC_IF = "org.freedesktop.portal.ScreenCast";
  static constexpr const char *RQ_IF = "org.freedesktop.portal.Request";

public:
  explicit PortalScreenCast(QObject *parent = nullptr)
      : QObject(parent), m_bus(QDBusConnection::sessionBus()), m_seq(0) {}

  void requestCapture() { createSession(); }

signals:
  void nodeReady(const QString &nodeId);
  void failed(const QString &reason);

private slots:
  void onCreateSessionResponse(uint response, const QVariantMap &results) {
    if (response != 0) {
      emit failed(
          QString("CreateSession cancelled (response=%1)").arg(response));
      return;
    }
    m_sessionHandle =
        results.value("session_handle").value<QDBusObjectPath>().path();
    if (m_sessionHandle.isEmpty())
      m_sessionHandle = results.value("session_handle").toString();

    qDebug() << "[Portal] ✅ Session created:" << m_sessionHandle;
    selectSources();
  }

  void onSelectSourcesResponse(uint response, const QVariantMap &results) {
    Q_UNUSED(results)
    if (response != 0) {
      emit failed(
          QString("SelectSources cancelled (response=%1)").arg(response));
      return;
    }
    qDebug() << "[Portal] ✅ Sources selected";
    startCapture();
  }

  void onStartResponse(uint response, const QVariantMap &results) {
    if (response != 0) {
      emit failed(QString("Start cancelled (response=%1)").arg(response));
      return;
    }

    QVariant streamsVar = results.value("streams");
    if (!streamsVar.isValid()) {
      emit failed("No 'streams' key in Start response");
      return;
    }

    quint32 firstNodeId = 0;
    const QDBusArgument arg = streamsVar.value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
      arg.beginStructure();
      quint32 nodeId = 0;
      arg >> nodeId;
      QVariantMap props;
      arg >> props;
      arg.endStructure();
      qDebug() << "[Portal] 🎬 Stream node:" << nodeId << props;
      if (firstNodeId == 0)
        firstNodeId = nodeId;
    }
    arg.endArray();

    if (firstNodeId == 0) {
      emit failed("Could not parse any node ID from portal streams");
      return;
    }

    qDebug() << "[Portal] ✅ PipeWire node ID:" << firstNodeId;
    emit nodeReady(QString::number(firstNodeId));
  }

private:
  QString senderToken() const {
    QString s = m_bus.baseService();
    s.remove(':');
    s.replace('.', '_');
    return s;
  }

  QString makeToken() {
    return QString("qt_caster_%1_%2")
        .arg(static_cast<qint64>(QCoreApplication::applicationPid()))
        .arg(++m_seq);
  }

  void subscribeResponse(const QString &token, const char *slot) {
    QString path = QString("/org/freedesktop/portal/desktop/request/%1/%2")
                       .arg(senderToken())
                       .arg(token);
    bool ok = m_bus.connect(SVC, path, RQ_IF, "Response", this, slot);
    if (!ok)
      qWarning() << "[Portal] ⚠️  Could not subscribe to Response at" << path;
  }

  void createSession() {
    QString token = makeToken();
    QString session = makeToken();
    subscribeResponse(token, SLOT(onCreateSessionResponse(uint, QVariantMap)));

    QVariantMap opts;
    opts["handle_token"] = token;
    opts["session_handle_token"] = session;

    QDBusInterface iface(SVC, OBJ, SC_IF, m_bus);
    if (!iface.isValid()) {
      emit failed("Cannot reach org.freedesktop.portal.Desktop "
                  "– is xdg-desktop-portal running?");
      return;
    }
    QDBusMessage reply = iface.call("CreateSession", opts);
    if (reply.type() == QDBusMessage::ErrorMessage)
      qWarning() << "[Portal] CreateSession D-Bus error:"
                 << reply.errorMessage();
  }

  void selectSources() {
    QString token = makeToken();
    subscribeResponse(token, SLOT(onSelectSourcesResponse(uint, QVariantMap)));

    QVariantMap opts;
    opts["handle_token"] = token;
    opts["types"] = QVariant::fromValue<uint>(3); // 1=monitor 2=window 3=both
    opts["multiple"] = QVariant::fromValue<bool>(false);

    QDBusInterface iface(SVC, OBJ, SC_IF, m_bus);
    iface.call("SelectSources",
               QVariant::fromValue(QDBusObjectPath(m_sessionHandle)), opts);
  }

  void startCapture() {
    QString token = makeToken();
    subscribeResponse(token, SLOT(onStartResponse(uint, QVariantMap)));

    QVariantMap opts;
    opts["handle_token"] = token;

    QDBusInterface iface(SVC, OBJ, SC_IF, m_bus);
    iface.call("Start", QVariant::fromValue(QDBusObjectPath(m_sessionHandle)),
               QString(""), // parent_window (empty = no parent)
               opts);
  }

  QDBusConnection m_bus;
  QString m_sessionHandle;
  int m_seq;
};

// ======================================================================
// StreamWorker
// ======================================================================
class StreamWorker : public QObject {
  Q_OBJECT

public:
  StreamWorker(const QJsonObject &config, QObject *parent = nullptr);
  ~StreamWorker();
  void start();

private slots:
  void cleanupAllProcesses();
  void connectPipeWireNodes(); // NEW: explicit connection via pw-link

private:
  void startVideoStream();
  void startAudioStream();
  void startPulseAudioStream();
  void startIncomingAudioStream();
  void launchGStreamerVideo(const QString &nodeId);
  void setupSignalHandlers();
  int createVirtualSink(const QString &sinkName);
  void removeVirtualSink();

  QJsonObject config;
  QString streamType;
  QString streamName;
  QProcess *gstProcess;
  int virtualSinkModule;         // module index of the null sink (virtual)
  QString virtualSinkActualName; // actual name used for the sink
};

static void signalHandler(int sig) {
  if (qApp) {
    QMetaObject::invokeMethod(
        qApp,
        [sig]() {
          qDebug() << "🛑 Received signal" << sig << "- quitting gracefully";
          QApplication::quit();
        },
        Qt::QueuedConnection);
  }
}

StreamWorker::StreamWorker(const QJsonObject &config, QObject *parent)
    : QObject(parent), config(config), gstProcess(nullptr),
      virtualSinkModule(-1) {
  streamType = config["type"].toString();
  streamName = config["streamName"].toString();
  qDebug() << "[Worker] ✨ Worker initialized for:" << streamType
           << "stream:" << streamName;
  setupSignalHandlers();
}

StreamWorker::~StreamWorker() {
  qDebug() << "🧹 Cleaning up StreamWorker for:" << streamName;
  cleanupAllProcesses();
}

void StreamWorker::setupSignalHandlers() {
  struct sigaction sa;
  sa.sa_handler = signalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);
}

void StreamWorker::cleanupAllProcesses() {
  if (gstProcess) {
    if (gstProcess->state() == QProcess::Running) {
      qDebug() << "🔚 Terminating GStreamer...";
      gstProcess->terminate();
      if (!gstProcess->waitForFinished(2000)) {
        gstProcess->kill();
        gstProcess->waitForFinished(1000);
      }
    }
    gstProcess->deleteLater();
    gstProcess = nullptr;
  }

  if (virtualSinkModule != -1) {
    removeVirtualSink();
  }
}

void StreamWorker::start() {
  if (streamType == "video")
    startVideoStream();
  else if (streamType == "audio")
    startAudioStream();
  else {
    qCritical() << "❌ Unknown stream type:" << streamType;
    QApplication::exit(1);
  }
}

// ======================================================================
// Video – uses D-Bus portal
// ======================================================================
void StreamWorker::startVideoStream() {
  qDebug() << "🎥 Starting video stream:" << streamName;
  connect(qApp, &QCoreApplication::aboutToQuit, this,
          &StreamWorker::cleanupAllProcesses);

  PortalScreenCast *portal = new PortalScreenCast(this);

  connect(portal, &PortalScreenCast::nodeReady, this,
          [this, portal](const QString &nodeId) {
            std::cout << "NODE_ID: " << nodeId.toStdString() << std::endl;
            qDebug() << "NODE_ID:" << nodeId;
            portal->deleteLater();
            launchGStreamerVideo(nodeId);
          });

  connect(portal, &PortalScreenCast::failed, this,
          [portal](const QString &reason) {
            qCritical() << "❌ Portal failed:" << reason;
            portal->deleteLater();
            QApplication::exit(1);
          });

  portal->requestCapture();
}

void StreamWorker::launchGStreamerVideo(const QString &nodeId) {
  qDebug() << "🚀 Launching GStreamer for video stream:" << streamName
           << "node:" << nodeId;

  gstProcess = new QProcess(this);

  int width = config["width"].toInt();
  int height = config["height"].toInt();
  int bitrate = config["bitrate"].toInt();
  QString host = config["host"].toString();
  int port = config["port"].toInt();
  bool useVC = config["useVideoConvert"].toBool();
  QString encoder = config["encoder"].toString();

  QStringList args;
  args << "pipewiresrc" << QString("path=%1").arg(nodeId);

  if (useVC)
    args << "!" << "videoconvert";

  if (width > 0 && height > 0)
    args << "!" << "videoscale" << "!"
         << QString("video/x-raw,width=%1,height=%2").arg(width).arg(height);

  if (encoder == "x264enc") {
    args << "!" << "x264enc"
         << QString("speed-preset=%1").arg(config["speedPreset"].toString())
         << QString("bitrate=%1").arg(bitrate);
    if (!config["tune"].toString().isEmpty())
      args << QString("tune=%1").arg(config["tune"].toString());
  } else if (encoder == "vaapih264enc") {
    args << "!" << "vaapih264enc" << QString("bitrate=%1").arg(bitrate)
         << QString("quality-level=%1").arg(config["qualityLevel"].toInt());
    QString rc = config["rateControl"].toString();
    if (rc == "cbr")
      args << "rate-control=cbr";
    else if (rc == "vbr")
      args << "rate-control=vbr";
    else if (rc == "cqp")
      args << "rate-control=cqp";
  } else if (encoder == "nvh264enc") {
    args << "!" << "nvh264enc" << QString("bitrate=%1").arg(bitrate)
         << "preset=low-latency" << "rc-mode=cbr";
  } else if (encoder == "qsvh264enc") {
    args << "!" << "qsvh264enc" << QString("bitrate=%1").arg(bitrate)
         << "target-usage=balanced" << "rate-control=cbr";
  } else {
    qCritical() << "❌ Unknown encoder:" << encoder;
    QApplication::exit(1);
    return;
  }

  args << "!" << "rtph264pay" << "pt=96"
       << "!" << "udpsink" << QString("host=%1").arg(host)
       << QString("port=%1").arg(port);

  qDebug() << "🎬 GStreamer arguments:" << args;

  gstProcess->setProcessChannelMode(QProcess::MergedChannels);
  connect(gstProcess, &QProcess::readyRead, [this]() {
    QByteArray data = gstProcess->readAll();
    if (!data.isEmpty())
      qDebug().noquote() << "[GStreamer 🎥]" << data.trimmed();
  });
  connect(gstProcess, &QProcess::started,
          []() { qDebug() << "✅ GStreamer started successfully"; });
  connect(gstProcess,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          [this](int code, QProcess::ExitStatus status) {
            qDebug() << "🏁 GStreamer finished:" << code << status;
            if (virtualSinkModule != -1) {
              removeVirtualSink();
            }
            QApplication::exit(code);
          });
  connect(gstProcess, &QProcess::errorOccurred, [](QProcess::ProcessError err) {
    qCritical() << "❌ GStreamer error:" << err;
    QApplication::exit(1);
  });

  gstProcess->start("gst-launch-1.0", args);
}

// ======================================================================
// Audio Streams with Virtual Sink Support
// ======================================================================
void StreamWorker::startAudioStream() {
  qDebug() << "🎤 Starting audio stream for:" << streamName;
  if (config["direction"].toString("outgoing") == "incoming")
    startIncomingAudioStream();
  else
    startPulseAudioStream();
}

int StreamWorker::createVirtualSink(const QString &sinkName) {
  QString name = sinkName;
  if (name.isEmpty()) {
    name = QString("%1").arg(streamName);
    name.replace(QRegularExpression("[^a-zA-Z0-9_-]"), "_");
  }
  virtualSinkActualName = name;

  // Set sink properties (playback) and source properties (monitor)
  QString sinkProps =
      "node.description='" + name + "', media.class=Audio/Source/Virtual";
  QString sourceProps = "node.description='" + name +
                        " Monitor', media.class=Audio/Source/Virtual";

  QProcess pactl;
  pactl.start("pactl", QStringList() << "load-module" << "module-null-sink"
                                     << "sink_name=" + name
                                     << "sink_properties=" + sinkProps
                                     << "source_properties=" + sourceProps
                                     << "channels=2");
  if (!pactl.waitForStarted() || !pactl.waitForFinished()) {
    qWarning() << "Failed to create virtual sink with pactl";
    return -1;
  }
  QByteArray output = pactl.readAllStandardOutput();
  int moduleIndex = output.trimmed().toInt();
  if (moduleIndex > 0) {
    qDebug() << "✅ Created virtual sink:" << name
             << "module index:" << moduleIndex;
    return moduleIndex;
  }
  qWarning() << "Failed to parse module index from pactl output:" << output;
  return -1;
}

void StreamWorker::removeVirtualSink() {
  if (virtualSinkModule == -1)
    return;

  QProcess pactl;
  pactl.start("pactl", QStringList() << "unload-module"
                                     << QString::number(virtualSinkModule));
  if (pactl.waitForStarted() && pactl.waitForFinished()) {
    qDebug() << "✅ Removed virtual sink module" << virtualSinkModule;
  } else {
    qWarning() << "Failed to remove virtual sink module" << virtualSinkModule;
  }
  virtualSinkModule = -1;
}

void StreamWorker::connectPipeWireNodes() {
  if (virtualSinkModule == -1)
    return;

  int channels = config["channels"].toInt();
  if (channels <= 0)
    channels = 2; // fallback

  // Wait for both nodes to appear in the graph
  QTimer::singleShot(500, this, [this, channels]() {
    QString clientName = streamName;
    QString sinkName = virtualSinkActualName;

    // Channel suffixes based on common PulseAudio naming
    QStringList suffixes;
    if (channels == 1) {
      suffixes << "FL"; // Mono often uses FL, but could be MONO; try FL first
    } else if (channels == 2) {
      suffixes << "FL" << "FR";
    } else if (channels == 6) { // 5.1
      suffixes << "FL" << "FR" << "FC" << "LFE" << "RL" << "RR";
    } else {
      // Generic: use standard suffixes for up to 8 channels
      suffixes << "FL" << "FR" << "FC" << "LFE" << "RL" << "RR" << "RLC"
               << "RRC";
      suffixes = suffixes.mid(0, channels);
    }

    for (const QString &suffix : suffixes) {
      QString source = QString("%1:output_%2").arg(clientName).arg(suffix);
      QString sink = QString("%1:input_%2").arg(sinkName).arg(suffix);

      QProcess pwlink;
      pwlink.start("pw-link", QStringList() << source << sink);
      if (pwlink.waitForStarted() && pwlink.waitForFinished()) {
        if (pwlink.exitCode() == 0) {
          qDebug() << "✅ Connected" << source << "→" << sink;
        } else {
          qWarning() << "Failed to connect" << source << "→" << sink << ":"
                     << pwlink.readAllStandardError();
        }
      } else {
        qWarning() << "Failed to start pw-link for" << source;
      }
    }
  });
}

void StreamWorker::startIncomingAudioStream() {
  qDebug() << "📥 Starting incoming audio stream:" << streamName;

  // Create virtual sink if requested
  bool createVirtual = config["virtualSink"].toBool(false);
  QString requestedSinkName = config["virtualSinkName"].toString();
  if (createVirtual) {
    virtualSinkModule = createVirtualSink(requestedSinkName);
    if (virtualSinkModule == -1) {
      qCritical() << "Failed to create virtual sink, aborting";
      QApplication::exit(1);
      return;
    }
  }

  gstProcess = new QProcess(this);
  connect(qApp, &QCoreApplication::aboutToQuit, this,
          &StreamWorker::cleanupAllProcesses);

  QString host = config["host"].toString();
  int port = config["port"].toInt();
  QString codec = config["codec"].toString();
  int latencyTime = config["latencyTime"].toInt(0);
  int bufferTime = config["bufferTime"].toInt(0);
  int bufferSize = config["bufferSize"].toInt(0);
  int sampleRate = config["sampleRate"].toInt();

  QStringList args;
  args << "udpsrc" << QString("port=%1").arg(port)
       << QString("address=%1").arg(host);
  if (bufferSize > 0)
    args << QString("buffer-size=%1").arg(bufferSize);

  if (codec == "opus") {
    args << "caps=\"application/x-rtp, media=audio,encoding-name=OPUS, "
            "clock-rate=48000, payload=96\""
         << "!" << "rtpopusdepay" << "!" << "opusdec"
         << "!" << "audioconvert" << "!" << "audioresample";
  } else if (codec == "aac") {
    args << "caps=application/x-rtp,media=audio,encoding-name=MP4A-LATM"
         << "!" << "rtpmp4gdepay" << "!" << "avdec_aac"
         << "!" << "audioconvert" << "!" << "audioresample";
  } else {
    qCritical() << "❌ Unknown incoming codec:" << codec;
    QApplication::exit(1);
    return;
  }

  // Add queues to avoid latency warnings
  args << "!" << "queue" << "max-size-buffers=0" << "max-size-time=0"
       << "!" << "queue" << "leaky=2" << "max-size-buffers=10"
       << "max-size-time=0";

  // Output to the created virtual sink or a user‑specified device
  if (createVirtual) {
    args << "!" << "pulsesink"
         << QString("device=%1").arg(virtualSinkActualName);
  } else {
    QString userSink = config["sinkDevice"].toString();
    if (!userSink.isEmpty())
      args << "!" << "pulsesink" << QString("device=%1").arg(userSink);
    else
      args << "!" << "pulsesink";
  }
  args << QString("client-name=%1").arg(streamName)
       << QString("stream-properties=props,media.name=%1").arg(streamName);
  if (latencyTime > 0)
    args << QString("latency-time=%1").arg(latencyTime);
  if (bufferTime > 0)
    args << QString("buffer-time=%1").arg(bufferTime);

  qDebug() << "🎵 Incoming GStreamer arguments:" << args;

  gstProcess->setProcessChannelMode(QProcess::MergedChannels);
  connect(gstProcess, &QProcess::readyRead, [this]() {
    QByteArray d = gstProcess->readAll();
    if (!d.isEmpty())
      qDebug().noquote() << "[GStreamer 📥]" << d.trimmed();
  });
  connect(gstProcess, &QProcess::started,
          []() { qDebug() << "✅ Incoming GStreamer started"; });
  connect(gstProcess,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          [this](int code, QProcess::ExitStatus status) {
            qDebug() << "🏁 Incoming GStreamer finished:" << code << status;
            QApplication::exit(code);
          });
  connect(gstProcess, &QProcess::errorOccurred, [](QProcess::ProcessError err) {
    qCritical() << "❌ Incoming GStreamer error:" << err;
    QApplication::exit(1);
  });

  gstProcess->start("gst-launch-1.0", args);

  if (createVirtual) {
    connectPipeWireNodes();
  }
}

void StreamWorker::startPulseAudioStream() {
  qDebug() << "📤 Starting outgoing audio stream:" << streamName;

  bool createVirtual = config["virtualSink"].toBool(false);
  QString requestedSinkName = config["virtualSinkName"].toString();
  if (createVirtual) {
    virtualSinkModule = createVirtualSink(requestedSinkName);
    if (virtualSinkModule == -1) {
      qCritical() << "Failed to create virtual sink, aborting";
      QApplication::exit(1);
      return;
    }
  }

  gstProcess = new QProcess(this);
  connect(qApp, &QCoreApplication::aboutToQuit, this,
          &StreamWorker::cleanupAllProcesses);

  QString host = config["host"].toString();
  int port = config["port"].toInt();
  QString codec = config["codec"].toString();
  int bitrate = config["bitrate"].toInt();
  int channels = config["channels"].toInt();
  int sampleRate = config["sampleRate"].toInt();
  int bufferSize = config["bufferSize"].toInt(0);

  QStringList args;

  // Use the monitor source of the virtual sink if created
  if (createVirtual) {
    args << "pulsesrc"
         << QString("device=%1.monitor").arg(virtualSinkActualName);
  } else {
    args << "pulsesrc";
  }
  args << QString("client-name=%1").arg(streamName)
       << QString("stream-properties=props,media.name=%1").arg(streamName)
       << "!" << "audioconvert" << "!" << "audioresample"
       << "!"
       << QString("audio/x-raw,channels=%1,rate=%2")
              .arg(channels)
              .arg(sampleRate);

  if (codec == "opus") {
    args << "!" << "opusenc" << QString("bitrate=%1").arg(bitrate * 1000) << "!"
         << "rtpopuspay" << "pt=97";
  } else if (codec == "aac") {
    args << "!" << "avenc_aac" << QString("bitrate=%1").arg(bitrate * 1000)
         << "!" << "rtpmp4gpay" << "pt=96";
  } else {
    qCritical() << "❌ Unknown outgoing codec:" << codec;
    QApplication::exit(1);
    return;
  }

  args << "!" << "queue" << "max-size-buffers=0" << "max-size-time=0";

  QStringList udpArgs;
  udpArgs << "udpsink" << QString("host=%1").arg(host)
          << QString("port=%1").arg(port);
  if (bufferSize > 0)
    udpArgs << QString("buffer-size=%1").arg(bufferSize);
  args << "!" << udpArgs;

  qDebug() << "🎵 GStreamer arguments:" << args;

  gstProcess->setProcessChannelMode(QProcess::MergedChannels);
  connect(gstProcess, &QProcess::readyRead, [this]() {
    QByteArray d = gstProcess->readAll();
    if (!d.isEmpty())
      qDebug().noquote() << "[GStreamer 📤]" << d.trimmed();
  });
  connect(gstProcess, &QProcess::started,
          []() { qDebug() << "✅ Outgoing audio started"; });
  connect(gstProcess,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          [this](int code, QProcess::ExitStatus status) {
            qDebug() << "🏁 Outgoing audio finished:" << code << status;
            QApplication::exit(code);
          });
  connect(gstProcess, &QProcess::errorOccurred, [](QProcess::ProcessError err) {
    qCritical() << "❌ Outgoing audio error:" << err;
    QApplication::exit(1);
  });

  gstProcess->start("gst-launch-1.0", args);
}

// ======================================================================
// main()
// ======================================================================
int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  QCommandLineParser parser;
  parser.addPositionalArgument("config", "JSON configuration string");
  parser.process(app);

  QStringList args = parser.positionalArguments();
  if (args.isEmpty()) {
    qCritical() << "❌ Usage: qt-caster-worker <json-config>";
    return 1;
  }

  QJsonDocument doc = QJsonDocument::fromJson(args[0].toUtf8());
  if (!doc.isObject()) {
    qCritical() << "❌ Invalid JSON configuration";
    return 1;
  }

  StreamWorker worker(doc.object());
  worker.start();

  return app.exec();
}

#include "stream_worker.moc"