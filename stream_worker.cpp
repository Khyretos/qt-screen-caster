// stream_worker.cpp - Fixed video pipeline, robust cleanup
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMediaCaptureSession>
#include <QProcess>
#include <QPushButton>
#include <QScreenCapture>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>
#include <QVBoxLayout>
#include <csignal>

// ======================================================================
// LiveNodeDialog – Manual refresh only, selection preserved
// ======================================================================
class LiveNodeDialog : public QDialog {
  Q_OBJECT

public:
  struct NodeInfo {
    QString id;
    QString nodeName;
    QString binary;
    QString pid;

    QString displayString() const {
      return QString("%1 - %2 (PID: %3) [%4]")
          .arg(id)
          .arg(binary.isEmpty() ? nodeName : binary)
          .arg(pid.isEmpty() ? "?" : pid)
          .arg(nodeName);
    }
  };

  explicit LiveNodeDialog(QWidget *parent = nullptr);
  ~LiveNodeDialog();

  void setNodes(const QList<NodeInfo> &nodes);
  void stopRefreshing();

signals:
  void nodeSelected(const QString &nodeId);
  void cancelled();

private slots:
  void onRefreshClicked();
  void onSelectClicked();
  void onCancelClicked();

private:
  void fetchNodes();
  QList<NodeInfo> parseNodes(const QByteArray &jsonData);
  void updateListWidget(const QList<NodeInfo> &nodes);

  QListWidget *listWidget;
  QPushButton *selectButton;
  QPushButton *refreshButton;
  QPushButton *cancelButton;
  QProcess *currentFetchProcess;
  QString lastSelectedId;
};

LiveNodeDialog::LiveNodeDialog(QWidget *parent)
    : QDialog(parent), currentFetchProcess(nullptr) {
  setWindowTitle("🎬 Select PipeWire Video Source");
  setMinimumWidth(700);
  setModal(true);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel("🔍 No video source automatically detected.\n"
                               "Click 'Refresh' to scan for available sources, "
                               "then select one and press 'Select'."));

  listWidget = new QListWidget(this);
  layout->addWidget(listWidget);

  QHBoxLayout *btnLayout = new QHBoxLayout();
  refreshButton = new QPushButton("🔄 Refresh", this);
  selectButton = new QPushButton("✅ Select", this);
  selectButton->setEnabled(false);
  cancelButton = new QPushButton("❌ Cancel", this);
  btnLayout->addWidget(refreshButton);
  btnLayout->addStretch();
  btnLayout->addWidget(selectButton);
  btnLayout->addWidget(cancelButton);
  layout->addLayout(btnLayout);

  connect(refreshButton, &QPushButton::clicked, this,
          &LiveNodeDialog::onRefreshClicked);
  connect(selectButton, &QPushButton::clicked, this,
          &LiveNodeDialog::onSelectClicked);
  connect(cancelButton, &QPushButton::clicked, this,
          &LiveNodeDialog::onCancelClicked);
  connect(listWidget, &QListWidget::itemSelectionChanged, [this]() {
    selectButton->setEnabled(!listWidget->selectedItems().isEmpty());
    if (listWidget->currentItem())
      lastSelectedId = listWidget->currentItem()->data(Qt::UserRole).toString();
  });
}

LiveNodeDialog::~LiveNodeDialog() { stopRefreshing(); }

void LiveNodeDialog::setNodes(const QList<NodeInfo> &nodes) {
  updateListWidget(nodes);
}

void LiveNodeDialog::stopRefreshing() {
  if (currentFetchProcess &&
      currentFetchProcess->state() != QProcess::NotRunning) {
    currentFetchProcess->kill();
    currentFetchProcess->waitForFinished();
  }
}

void LiveNodeDialog::onRefreshClicked() {
  if (currentFetchProcess &&
      currentFetchProcess->state() != QProcess::NotRunning)
    return;
  fetchNodes();
}

void LiveNodeDialog::fetchNodes() {
  refreshButton->setEnabled(false);
  refreshButton->setText("🔄 Refreshing...");

  currentFetchProcess = new QProcess(this);
  QString cmd = "pw-dump | jq -c '[.[] | select(.info.props.\"media.class\" == "
                "\"Video/Source\") | "
                "{id: .id, nodeName: .info.props.\"node.name\", binary: "
                ".info.props.\"application.process.binary\", "
                "pid: .info.props.\"application.process.id\"}]'";
  currentFetchProcess->start("bash", QStringList() << "-c" << cmd);
  connect(currentFetchProcess,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          [this](int exitCode, QProcess::ExitStatus exitStatus) {
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
              QList<NodeInfo> nodes =
                  parseNodes(currentFetchProcess->readAllStandardOutput());
              updateListWidget(nodes);
            } else {
              qCritical() << "❌ Failed to fetch node list";
            }
            refreshButton->setEnabled(true);
            refreshButton->setText("🔄 Refresh");
            currentFetchProcess->deleteLater();
            currentFetchProcess = nullptr;
          });
}

QList<LiveNodeDialog::NodeInfo>
LiveNodeDialog::parseNodes(const QByteArray &jsonData) {
  QList<NodeInfo> nodes;
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(jsonData, &error);
  if (error.error != QJsonParseError::NoError || !doc.isArray()) {
    qCritical() << "❌ Failed to parse pw-dump JSON:" << error.errorString();
    return nodes;
  }

  QJsonArray array = doc.array();
  for (const QJsonValue &val : array) {
    QJsonObject obj = val.toObject();
    NodeInfo info;
    info.id = QString::number(obj["id"].toInt());
    info.nodeName = obj["nodeName"].toString();
    info.binary = obj["binary"].toString();
    info.pid = obj["pid"].toString();
    if (info.id.isEmpty() || info.id == "0")
      continue;
    nodes.append(info);
  }

  std::sort(nodes.begin(), nodes.end(),
            [](const NodeInfo &a, const NodeInfo &b) {
              return a.id.toInt() > b.id.toInt();
            });
  return nodes;
}

void LiveNodeDialog::updateListWidget(const QList<NodeInfo> &nodes) {
  listWidget->clear();
  for (const NodeInfo &node : nodes) {
    QListWidgetItem *item = new QListWidgetItem(node.displayString());
    item->setData(Qt::UserRole, node.id);
    listWidget->addItem(item);
    if (node.id == lastSelectedId) {
      item->setSelected(true);
      listWidget->setCurrentItem(item);
    }
  }
  selectButton->setEnabled(!listWidget->selectedItems().isEmpty());
}

void LiveNodeDialog::onSelectClicked() {
  if (listWidget->currentItem()) {
    QString nodeId = listWidget->currentItem()->data(Qt::UserRole).toString();
    emit nodeSelected(nodeId);
    accept();
  }
}

void LiveNodeDialog::onCancelClicked() {
  emit cancelled();
  reject();
}

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
  void onScreenCaptureActive(bool active);
  void onScreenCaptureError(QScreenCapture::Error error,
                            const QString &errorString);
  void cleanupAllProcesses();
  void captureBeforePortalSnapshot();
  void startPortalNodeMonitor();
  void checkForNewNode();
  void runVideoNodeDetection();

private:
  void startVideoStream();
  void startAudioStream();
  // void findPipeWireNodeAndStartAudio();
  void startPulseAudioStream();
  void launchGStreamerVideo(const QString &nodeId);
  void launchGStreamerAudio(const QString &nodeId);
  void setupSignalHandlers();
  void startIncomingAudioStream();
  QList<LiveNodeDialog::NodeInfo>
  parsePipeWireVideoNodes(const QByteArray &jsonData);
  void showNodeSelectionDialog(const QList<LiveNodeDialog::NodeInfo> &nodes);

  QJsonObject config;
  QString streamType;
  QString streamName;

  QMediaCaptureSession *session;
  QScreenCapture *screenCapture;
  QProcess *gstProcess;
  QProcess *pwDumpProcess;

  // Portal detection
  QList<LiveNodeDialog::NodeInfo> m_beforePortalNodes;
  QTimer *m_monitorTimer;
  int m_monitorAttempts;

  bool detectionInProgress;
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
    : QObject(parent), config(config), session(nullptr), screenCapture(nullptr),
      gstProcess(nullptr), pwDumpProcess(nullptr), m_monitorTimer(nullptr),
      m_monitorAttempts(0), detectionInProgress(false) {
  streamType = config["type"].toString();
  streamName = config["streamName"].toString();

  qDebug() << "✅ StreamWorker created for:" << streamName
           << "type:" << streamType;
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
      qDebug() << "🔚 Terminating GStreamer process...";
      gstProcess->terminate();
      if (!gstProcess->waitForFinished(2000)) {
        qDebug() << "⚠️ GStreamer did not terminate, killing...";
        gstProcess->kill();
        gstProcess->waitForFinished(1000);
      }
    }
    gstProcess->deleteLater();
    gstProcess = nullptr;
  }

  if (pwDumpProcess) {
    if (pwDumpProcess->state() != QProcess::NotRunning) {
      pwDumpProcess->kill();
      pwDumpProcess->waitForFinished();
    }
    pwDumpProcess->deleteLater();
    pwDumpProcess = nullptr;
  }

  if (m_monitorTimer) {
    m_monitorTimer->stop();
    m_monitorTimer->deleteLater();
    m_monitorTimer = nullptr;
  }

  if (session) {
    session->deleteLater();
    session = nullptr;
  }
  if (screenCapture) {
    screenCapture->deleteLater();
    screenCapture = nullptr;
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
// Video Stream – FIXED PIPELINE (no stream-properties, added videorate)
// ======================================================================
void StreamWorker::startVideoStream() {
  qDebug() << "🎥 Starting video stream for:" << streamName;

  session = new QMediaCaptureSession(this);
  screenCapture = new QScreenCapture(this);
  session->setScreenCapture(screenCapture);

  connect(screenCapture, &QScreenCapture::activeChanged, this,
          &StreamWorker::onScreenCaptureActive);
  connect(screenCapture, &QScreenCapture::errorOccurred, this,
          &StreamWorker::onScreenCaptureError);
  connect(qApp, &QCoreApplication::aboutToQuit, this,
          &StreamWorker::cleanupAllProcesses);

  screenCapture->setActive(false);
  QTimer::singleShot(100, [this]() {
    qDebug() << "🪟 Activating screen capture - portal will appear";
    screenCapture->setActive(true);
  });
}

void StreamWorker::onScreenCaptureActive(bool active) {
  qDebug() << "🪟 Screen capture active changed:" << active;

  if (active) {
    captureBeforePortalSnapshot();
  } else {
    qDebug() << "❌ Screen capture deactivated or cancelled";
    QApplication::exit(1);
  }
}

void StreamWorker::onScreenCaptureError(QScreenCapture::Error error,
                                        const QString &errorString) {
  qCritical() << "❌ Screen capture error:" << error << errorString;
  QApplication::exit(1);
}

void StreamWorker::captureBeforePortalSnapshot() {
  qDebug() << "📸 Taking snapshot of video nodes before portal node appears...";
  QProcess *snapshot = new QProcess(this);
  QString cmd = "pw-dump | jq -c '[.[] | select(.info.props.\"media.class\" == "
                "\"Video/Source\") | "
                "{id: .id, nodeName: .info.props.\"node.name\", binary: "
                ".info.props.\"application.process.binary\", "
                "pid: .info.props.\"application.process.id\"}]'";
  snapshot->start("bash", QStringList() << "-c" << cmd);
  connect(
      snapshot, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      [this, snapshot](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
          m_beforePortalNodes =
              parsePipeWireVideoNodes(snapshot->readAllStandardOutput());
          qDebug() << "✅ Snapshot taken:" << m_beforePortalNodes.size()
                   << "nodes";
          startPortalNodeMonitor();
        } else {
          qCritical() << "⚠️ Failed to take snapshot, using fallback delay";
          QTimer::singleShot(1500, this, &StreamWorker::runVideoNodeDetection);
        }
        snapshot->deleteLater();
      });
}

void StreamWorker::startPortalNodeMonitor() {
  qDebug() << "👀 Starting portal node monitor...";
  m_monitorAttempts = 0;
  m_monitorTimer = new QTimer(this);
  m_monitorTimer->setInterval(200);
  connect(m_monitorTimer, &QTimer::timeout, this,
          &StreamWorker::checkForNewNode);
  m_monitorTimer->start();
}

void StreamWorker::checkForNewNode() {
  m_monitorAttempts++;
  if (m_monitorAttempts > 50) {
    qDebug()
        << "⏰ Portal node detection timed out, falling back to full detection";
    m_monitorTimer->stop();
    runVideoNodeDetection();
    return;
  }

  QProcess *check = new QProcess(this);
  QString cmd = "pw-dump | jq -c '[.[] | select(.info.props.\"media.class\" == "
                "\"Video/Source\") | "
                "{id: .id, nodeName: .info.props.\"node.name\", binary: "
                ".info.props.\"application.process.binary\", "
                "pid: .info.props.\"application.process.id\"}]'";
  check->start("bash", QStringList() << "-c" << cmd);
  connect(check, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          [this, check](int exitCode, QProcess::ExitStatus exitStatus) {
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
              QList<LiveNodeDialog::NodeInfo> current =
                  parsePipeWireVideoNodes(check->readAllStandardOutput());

              QSet<QString> beforeIds;
              for (const auto &n : m_beforePortalNodes)
                beforeIds.insert(n.id);

              QList<LiveNodeDialog::NodeInfo> newNodes;
              for (const auto &n : current) {
                if (!beforeIds.contains(n.id))
                  newNodes.append(n);
              }

              for (const auto &n : newNodes) {
                if (n.nodeName.toLower().contains("screen") ||
                    n.nodeName.toLower().contains("portal")) {
                  qDebug() << "✅ New portal node detected:" << n.id;
                  m_monitorTimer->stop();
                  launchGStreamerVideo(n.id);
                  return;
                }
              }
            }
            check->deleteLater();
          });
}

void StreamWorker::runVideoNodeDetection() {
  qDebug() << "🔍 Running full video node detection...";
  detectionInProgress = true;

  pwDumpProcess = new QProcess(this);
  QString cmd = "pw-dump | jq -c '[.[] | select(.info.props.\"media.class\" == "
                "\"Video/Source\") | "
                "{id: .id, nodeName: .info.props.\"node.name\", binary: "
                ".info.props.\"application.process.binary\", "
                "pid: .info.props.\"application.process.id\"}]'";
  pwDumpProcess->start("bash", QStringList() << "-c" << cmd);
  connect(pwDumpProcess,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          [this](int exitCode, QProcess::ExitStatus exitStatus) {
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
              QList<LiveNodeDialog::NodeInfo> nodes = parsePipeWireVideoNodes(
                  pwDumpProcess->readAllStandardOutput());

              QString autoNodeId;
              for (const auto &node : nodes) {
                if (node.nodeName.toLower().contains("screen") ||
                    node.nodeName.toLower().contains("portal")) {
                  autoNodeId = node.id;
                  break;
                }
              }

              if (!autoNodeId.isEmpty()) {
                qDebug() << "✅ Automatically detected video node:"
                         << autoNodeId;
                launchGStreamerVideo(autoNodeId);
              } else {
                qDebug() << "⚠️ No node with 'screen' or 'portal' found. "
                            "Showing selection dialog...";
                showNodeSelectionDialog(nodes);
              }
            } else {
              qCritical() << "❌ pw-dump failed:" << exitStatus << exitCode;
              QApplication::exit(1);
            }
            pwDumpProcess->deleteLater();
            pwDumpProcess = nullptr;
            detectionInProgress = false;
          });
}

void StreamWorker::showNodeSelectionDialog(
    const QList<LiveNodeDialog::NodeInfo> &nodes) {
  LiveNodeDialog *dialog = new LiveNodeDialog();
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setNodes(nodes);

  connect(dialog, &LiveNodeDialog::nodeSelected, this,
          [this](const QString &nodeId) {
            qDebug() << "✅ User selected video node:" << nodeId;
            launchGStreamerVideo(nodeId);
          });
  connect(dialog, &LiveNodeDialog::cancelled, this, []() {
    qDebug() << "❌ Node selection cancelled.";
    QApplication::exit(1);
  });

  dialog->show();
}

void StreamWorker::launchGStreamerVideo(const QString &nodeId) {
  qDebug() << "🚀 Launching GStreamer for video stream:" << streamName;

  gstProcess = new QProcess(this);
  connect(qApp, &QCoreApplication::aboutToQuit, this,
          &StreamWorker::cleanupAllProcesses);

  int width = config["width"].toInt();
  int height = config["height"].toInt();
  int bitrate = config["bitrate"].toInt();
  QString host = config["host"].toString();
  int port = config["port"].toInt();
  bool useVideoConvert = config["useVideoConvert"].toBool();

  QString encoder = config["encoder"].toString();
  bool isSoftware = (encoder == "x264enc");

  QStringList args;
  args << "pipewiresrc" << QString("path=%1").arg(nodeId);

  // ---- Colour conversion & scaling ----
  if (useVideoConvert)
    args << "!" << "videoconvert";

  if (width > 0 && height > 0) {

    args << "!" << "videoscale"
         << "!"
         << QString("video/x-raw,width=%1,height=%2").arg(width).arg(height);
  }

  // ---- Encoder selection ----
  if (encoder == "x264enc") {
    // Software x264
    args << "!" << "x264enc"
         << QString("speed-preset=%1").arg(config["speedPreset"].toString())
         << QString("bitrate=%1").arg(bitrate);
    if (config.contains("tune") && !config["tune"].toString().isEmpty())
      args << QString("tune=%1").arg(config["tune"].toString());
  } else if (encoder == "vaapih264enc") {
    // VAAPI hardware encoder
    args << "!" << "vaapih264enc" << QString("bitrate=%1").arg(bitrate);
    // Quality: lower number = better, range 0-51
    int quality = config["qualityLevel"].toInt();
    args << QString("quality-level=%1").arg(quality);
    // Rate control
    QString rc = config["rateControl"].toString();
    if (rc == "cbr")
      args << "rate-control=cbr";
    else if (rc == "vbr")
      args << "rate-control=vbr";
    else if (rc == "cqp")
      args << "rate-control=cqp";
  } else if (encoder == "nvh264enc") {
    // NVENC (NVIDIA)
    args << "!" << "nvh264enc" << QString("bitrate=%1").arg(bitrate);
    // Preset: low-latency, high-performance, etc.
    args << "preset=low-latency";
    // Quality: use rc-mode and other options
    args << "rc-mode=cbr";
  } else if (encoder == "qsvh264enc") {
    // Intel QuickSync
    args << "!" << "qsvh264enc" << QString("bitrate=%1").arg(bitrate);
    args << "target-usage=balanced";
    args << "rate-control=cbr";
  } else {
    qCritical() << "❌ Unknown encoder:" << encoder;
    QApplication::exit(1);
    return;
  }

  // ---- Payload ----
  args << "!" << "rtph264pay"
       << "pt=96";

  // ---- UDP sink ----
  args << "!" << "udpsink" << QString("host=%1").arg(host)
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
          [](int exitCode, QProcess::ExitStatus exitStatus) {
            qDebug() << "🏁 GStreamer finished:" << exitCode << exitStatus;
            QApplication::exit(exitCode);
          });
  connect(gstProcess, &QProcess::errorOccurred,
          [](QProcess::ProcessError error) {
            qCritical() << "❌ GStreamer error:" << error;
            QApplication::exit(1);
          });

  gstProcess->start("gst-launch-1.0", args);
}

// ======================================================================
// Audio Stream – Already working (channels up to 8)
// ======================================================================
void StreamWorker::startIncomingAudioStream() {
  qDebug() << "📥 Starting incoming audio stream:" << streamName;

  gstProcess = new QProcess(this);
  connect(qApp, &QCoreApplication::aboutToQuit, this,
          &StreamWorker::cleanupAllProcesses);

  QString host = config["host"].toString();
  int port = config["port"].toInt();
  QString codec = config["codec"].toString();
  int channels = config["channels"].toInt();
  int sampleRate = config["sampleRate"].toInt();
  QString sinkDevice = config["sinkDevice"].toString();

  QStringList args;
  args << "udpsrc" << QString("port=%1").arg(port)
       << QString("address=%1").arg(host);

  // Add buffer parameters to udpsrc
  int bufferSize = config["bufferSize"].toInt(0);
  int latencyTime = config["latencyTime"].toInt(0);
  int bufferTime = config["bufferTime"].toInt(0);

  if (bufferSize > 0)
    args << QString("buffer-size=%1").arg(bufferSize);

  // Set caps based on codec
  if (codec == "opus") {
    args << "caps=application/x-rtp,media=audio,encoding-name=OPUS";
    args << "!" << "rtpopusdepay"
         << "!" << "opusdec"
         << "!" << "audioconvert"
         << "!" << "audioresample";
  } else if (codec == "aac") {
    args << "caps=application/x-rtp,media=audio,encoding-name=MP4A-LATM";
    args << "!" << "rtpmp4gdepay"
         << "!" << "avdec_aac"
         << "!" << "audioconvert"
         << "!" << "audioresample";
  } else {
    qCritical() << "❌ Unknown codec for incoming stream:" << codec;
    QApplication::exit(1);
    return;
  }

  // Force channel count and sample rate (optional, depends on stream)
  // args << "!" <<
  // QString("audio/x-raw,channels=%1,rate=%2").arg(channels).arg(sampleRate);
  // gst-launch-1.0 audiotestsrc ! audioconvert ! pulsesink
  // stream-properties="media.name=custom-probe-stream"=virtsink

  // PulseAudio sink
  QStringList pulsesinkArgs;
  pulsesinkArgs
      << "pulsesink"
      << QString("client-name=%1").arg(streamName) // PulseAudio client name
      << QString("stream-properties=props,media.name=%1")
             .arg(streamName); // Stream name

  // pulsesink also supports latency-time and buffer-time
  if (latencyTime > 0)
    pulsesinkArgs << QString("latency-time=%1").arg(latencyTime);
  if (bufferTime > 0)
    pulsesinkArgs << QString("buffer-time=%1").arg(bufferTime);

  args << "!" << pulsesinkArgs;

  qDebug() << "🎵 Incoming GStreamer arguments:" << args;

  gstProcess->setProcessChannelMode(QProcess::MergedChannels);
  connect(gstProcess, &QProcess::readyRead, [this]() {
    QByteArray data = gstProcess->readAll();
    if (!data.isEmpty())
      qDebug().noquote() << "[GStreamer 📥]" << data.trimmed();
  });
  connect(gstProcess, &QProcess::started,
          []() { qDebug() << "✅ Incoming GStreamer started"; });
  connect(
      gstProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      [](int exitCode, QProcess::ExitStatus exitStatus) {
        qDebug() << "🏁 Incoming GStreamer finished:" << exitCode << exitStatus;
        QApplication::exit(exitCode);
      });
  connect(gstProcess, &QProcess::errorOccurred,
          [](QProcess::ProcessError error) {
            qCritical() << "❌ Incoming GStreamer error:" << error;
            QApplication::exit(1);
          });

  gstProcess->start("gst-launch-1.0", args);
}

void StreamWorker::startAudioStream() {
  qDebug() << "🎤 Starting audio stream for:" << streamName;

  QString direction = config["direction"].toString("outgoing");
  if (direction == "incoming") {
    startIncomingAudioStream();
  } else {
    // findPipeWireNodeAndStartAudio(); // existing outgoing logic
    startPulseAudioStream();
  }
}

void StreamWorker::startPulseAudioStream() {
  qDebug() << "📤 Starting outgoing audio stream with pulsesrc:" << streamName;

  gstProcess = new QProcess(this);
  connect(qApp, &QCoreApplication::aboutToQuit, this,
          &StreamWorker::cleanupAllProcesses);

  QString host = config["host"].toString();
  int port = config["port"].toInt();
  QString codec = config["codec"].toString();
  int bitrate = config["bitrate"].toInt();
  int channels = config["channels"].toInt();
  int sampleRate = config["sampleRate"].toInt();
  QString opusApplication = config["opusApplication"].toString();

  QStringList args;
  // Use pulsesrc with custom name
  args << "pulsesrc"
       << QString("client-name=%1").arg(streamName); // PulseAudio client name
  args << QString("stream-properties=props,media.name=%1").arg(streamName);

  // Optional: specify device if needed (default source)
  // args << "device=alsa_input.pci-0000_00_1f.3.analog-stereo";

  args << "!" << "audioconvert"
       << "!" << "audioresample"
       << "!"
       << QString("audio/x-raw,channels=%1,rate=%2")
              .arg(channels)
              .arg(sampleRate);

  // Codec-specific
  if (codec == "opus") {
    args << "!" << "opusenc" << QString("bitrate=%1").arg(bitrate * 1000) << "!"
         << "rtpopuspay"
         << "pt=97";
  } else if (codec == "aac") {
    args << "!" << "avenc_aac" << QString("bitrate=%1").arg(bitrate * 1000)
         << "!" << "rtpmp4gpay"
         << "pt=96";
  } else {
    qCritical() << "❌ Unknown codec:" << codec;
    QApplication::exit(1);
    return;
  }

  // udpsink with optional buffer parameters
  QStringList udpsinkArgs;
  udpsinkArgs << "udpsink" << QString("host=%1").arg(host)
              << QString("port=%1").arg(port);

  int bufferSize = config["bufferSize"].toInt(0);
  int latencyTime = config["latencyTime"].toInt(0);
  int bufferTime = config["bufferTime"].toInt(0);

  if (bufferSize > 0)
    udpsinkArgs << QString("buffer-size=%1").arg(bufferSize);
  // if (latencyTime > 0)
  //     udpsinkArgs << QString("latency-time=%1").arg(latencyTime);
  // if (bufferTime > 0)
  // udpsinkArgs << QString("buffer-time=%1").arg(bufferTime);

  args << "!" << udpsinkArgs;

  qDebug() << "🎵 GStreamer arguments:" << args;

  gstProcess->setProcessChannelMode(QProcess::MergedChannels);
  connect(gstProcess, &QProcess::readyRead, [this]() {
    QByteArray data = gstProcess->readAll();
    if (!data.isEmpty())
      qDebug().noquote() << "[GStreamer 📤]" << data.trimmed();
  });
  connect(gstProcess, &QProcess::started,
          []() { qDebug() << "✅ Outgoing audio started"; });
  connect(gstProcess,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          [](int exitCode, QProcess::ExitStatus exitStatus) {
            qDebug() << "🏁 Outgoing audio finished:" << exitCode << exitStatus;
            QApplication::exit(exitCode);
          });
  connect(gstProcess, &QProcess::errorOccurred,
          [](QProcess::ProcessError error) {
            qCritical() << "❌ Outgoing audio error:" << error;
            QApplication::exit(1);
          });

  gstProcess->start("gst-launch-1.0", args);
}

// void StreamWorker::findPipeWireNodeAndStartAudio()
// {
//     qDebug() << "🔍 Finding PipeWire audio node...";

//     pwDumpProcess = new QProcess(this);
//     QString command = "pw-dump | jq -r '[.[] |
//     select(.info.props.\"media.class\" == \"Audio/Source\") | select(.id !=
//     null) | .id] | .[0] // empty'"; pwDumpProcess->start("bash",
//     QStringList() << "-c" << command);

//     connect(pwDumpProcess, QOverload<int,
//     QProcess::ExitStatus>::of(&QProcess::finished),
//             [this](int exitCode, QProcess::ExitStatus exitStatus)
//             {
//                 if (exitStatus == QProcess::NormalExit && exitCode == 0)
//                 {
//                     QString nodeId =
//                     QString(pwDumpProcess->readAllStandardOutput()).trimmed();
//                     if (!nodeId.isEmpty() && nodeId != "null" &&
//                     nodeId.toInt() > 0)
//                     {
//                         qDebug() << "✅ Found PipeWire audio node:" <<
//                         nodeId; launchGStreamerAudio(nodeId);
//                     }
//                     else
//                     {
//                         qCritical() << "❌ Could not find valid PipeWire
//                         audio node"; QApplication::exit(1);
//                     }
//                 }
//                 else
//                 {
//                     qCritical() << "❌ pw-dump failed:" << exitStatus <<
//                     exitCode; QApplication::exit(1);
//                 }
//                 pwDumpProcess->deleteLater();
//                 pwDumpProcess = nullptr;
//             });
// }

// void StreamWorker::launchGStreamerAudio(const QString &nodeId)
// {
//     qDebug() << "🚀 Launching GStreamer for audio stream:" << streamName;

//     gstProcess = new QProcess(this);
//     connect(qApp, &QCoreApplication::aboutToQuit, this,
//     &StreamWorker::cleanupAllProcesses);

//     QString host = config["host"].toString();
//     int port = config["port"].toInt();
//     QString codec = config["codec"].toString();
//     int bitrate = config["bitrate"].toInt();
//     int channels = config["channels"].toInt();
//     int sampleRate = config["sampleRate"].toInt();
//     QString opusApplication = config["opusApplication"].toString();

//     QStringList args;
//     // pipewiresrc has a lot of syncing issues.
//     args << "pulsesrc"
//          << QString("client-name=xxx")
//          << QString("name=%1").arg(streamName); // keep for audio

//     args
//         << "!" << QString("audio/x-raw,channels=%1").arg(channels)
//         << "!" << "audioconvert"
//         << "!" << "audioresample";

//     if (codec == "opus")
//     {
//         args << "!" << "opusenc"
//              //  << QString("bitrate=%1").arg(bitrate * 1000)
//              //  << QString("application=%1").arg(opusApplication)
//              << "!" << "rtpopuspay";
//         //  << "pt=97";
//     }
//     // else if (codec == "aac")
//     // {
//     //     args << "!" << "avenc_aac"
//     //          << QString("bitrate=%1").arg(bitrate * 1000)
//     //          << "!" << "rtpmp4gpay"
//     //          << "pt=96";
//     // }
//     // else
//     // {
//     //     qCritical() << "❌ Unknown codec:" << codec;
//     //     QApplication::exit(1);
//     //     return;
//     // }

//     QStringList udpsinkArgs;
//     udpsinkArgs << "udpsink"
//                 << QString("host=%1").arg(host)
//                 << QString("port=%1").arg(port);

//     args << "!" << udpsinkArgs;

//     qDebug() << "🎵 GStreamer arguments:" << args;

//     gstProcess->setProcessChannelMode(QProcess::MergedChannels);
//     connect(gstProcess, &QProcess::readyRead, [this]()
//             {
//         QByteArray data = gstProcess->readAll();
//         if (!data.isEmpty())
//             qDebug().noquote() << "[GStreamer 🎤]" << data.trimmed(); });
//     connect(gstProcess, &QProcess::started, []()
//             { qDebug() << "✅ GStreamer started successfully"; });
//     connect(gstProcess, QOverload<int,
//     QProcess::ExitStatus>::of(&QProcess::finished),
//             [](int exitCode, QProcess::ExitStatus exitStatus)
//             {
//                 qDebug() << "🏁 GStreamer finished:" << exitCode <<
//                 exitStatus; QApplication::exit(exitCode);
//             });
//     connect(gstProcess, &QProcess::errorOccurred, [](QProcess::ProcessError
//     error)
//             {
//         qCritical() << "❌ GStreamer error:" << error;
//         QApplication::exit(1); });

//     gstProcess->start("gst-launch-1.0", args);
// }

// ======================================================================
// Utility: Parse PipeWire video nodes
// ======================================================================
QList<LiveNodeDialog::NodeInfo>
StreamWorker::parsePipeWireVideoNodes(const QByteArray &jsonData) {
  QList<LiveNodeDialog::NodeInfo> nodes;
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(jsonData, &error);
  if (error.error != QJsonParseError::NoError || !doc.isArray())
    return nodes;

  QJsonArray array = doc.array();
  for (const QJsonValue &val : array) {
    QJsonObject obj = val.toObject();
    LiveNodeDialog::NodeInfo info;
    info.id = QString::number(obj["id"].toInt());
    info.nodeName = obj["nodeName"].toString();
    info.binary = obj["binary"].toString();
    info.pid = obj["pid"].toString();
    if (info.id.isEmpty() || info.id == "0")
      continue;
    nodes.append(info);
  }

  std::sort(
      nodes.begin(), nodes.end(),
      [](const LiveNodeDialog::NodeInfo &a, const LiveNodeDialog::NodeInfo &b) {
        return a.id.toInt() > b.id.toInt();
      });
  return nodes;
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

  QJsonObject config = doc.object();

  StreamWorker worker(config);
  worker.start();

  return app.exec();
}

#include "stream_worker.moc"