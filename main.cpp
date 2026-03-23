// main.cpp - Main GUI application with custom names, pin/save, tray fix
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

// ======================================================================
// Video Stream Configuration (with custom name)
// ======================================================================
struct VideoStreamConfig {
  // Core
  int width;
  int height;
  int bitrate;
  QString host;
  int port;
  bool useVideoConvert;
  QString customName; // user-provided stream name

  // Encoder selection
  QString encoder;
  bool useHardware;

  // x264 specific
  QString speedPreset;
  QString tune;
  QString profile;

  // Hardware encoder common
  int qualityLevel;
  QString rateControl;

  QJsonObject toJson() const {
    QJsonObject obj;
    obj["width"] = width;
    obj["height"] = height;
    obj["bitrate"] = bitrate;
    obj["host"] = host;
    obj["port"] = port;
    obj["useVideoConvert"] = useVideoConvert;
    obj["customName"] = customName;
    obj["encoder"] = encoder;
    obj["speedPreset"] = speedPreset;
    obj["tune"] = tune;
    obj["profile"] = profile;
    obj["qualityLevel"] = qualityLevel;
    obj["rateControl"] = rateControl;
    return obj;
  }

  static VideoStreamConfig fromJson(const QJsonObject &obj) {
    VideoStreamConfig cfg;
    cfg.width = obj["width"].toInt();
    cfg.height = obj["height"].toInt();
    cfg.bitrate = obj["bitrate"].toInt();
    cfg.host = obj["host"].toString();
    cfg.port = obj["port"].toInt();
    cfg.useVideoConvert = obj["useVideoConvert"].toBool();
    cfg.customName = obj["customName"].toString();
    cfg.encoder = obj["encoder"].toString();
    cfg.speedPreset = obj["speedPreset"].toString();
    cfg.tune = obj["tune"].toString();
    cfg.profile = obj["profile"].toString();
    cfg.qualityLevel = obj["qualityLevel"].toInt();
    cfg.rateControl = obj["rateControl"].toString();
    return cfg;
  }
};

// ======================================================================
// Audio Stream Configuration (with custom name + virtual sink)
// ======================================================================
struct AudioStreamConfig {
  QString host;
  int port;
  QString codec;
  int bitrate;
  int channels;
  int sampleRate;
  QString opusApplication;
  QString customName;      // user-provided stream name
  QString direction;       // "outgoing" or "incoming"
  QString sinkDevice;      // PulseAudio sink name for incoming streams
  int bufferSize;          // bytes, 0 = default
  int latencyTime;         // ms, 0 = default
  int bufferTime;          // ms, 0 = default
  bool virtualSink;        // new: create a virtual sink
  QString virtualSinkName; // new: name for the virtual sink

  QJsonObject toJson() const {
    QJsonObject obj;
    obj["host"] = host;
    obj["port"] = port;
    obj["codec"] = codec;
    obj["bitrate"] = bitrate;
    obj["channels"] = channels;
    obj["sampleRate"] = sampleRate;
    obj["opusApplication"] = opusApplication;
    obj["customName"] = customName;
    obj["direction"] = direction;
    obj["sinkDevice"] = sinkDevice;
    obj["bufferSize"] = bufferSize;
    obj["latencyTime"] = latencyTime;
    obj["bufferTime"] = bufferTime;
    obj["virtualSink"] = virtualSink;
    obj["virtualSinkName"] = virtualSinkName;
    return obj;
  }

  static AudioStreamConfig fromJson(const QJsonObject &obj) {
    AudioStreamConfig cfg;
    cfg.host = obj["host"].toString();
    cfg.port = obj["port"].toInt();
    cfg.codec = obj["codec"].toString();
    cfg.bitrate = obj["bitrate"].toInt();
    cfg.channels = obj["channels"].toInt();
    cfg.sampleRate = obj["sampleRate"].toInt();
    cfg.opusApplication = obj["opusApplication"].toString();
    cfg.customName = obj["customName"].toString();
    cfg.direction = obj["direction"].toString("outgoing");
    cfg.sinkDevice = obj["sinkDevice"].toString();
    cfg.bufferSize = obj["bufferSize"].toInt(0);
    cfg.latencyTime = obj["latencyTime"].toInt(0);
    cfg.bufferTime = obj["bufferTime"].toInt(0);
    cfg.virtualSink = obj["virtualSink"].toBool(false);
    cfg.virtualSinkName = obj["virtualSinkName"].toString();
    return cfg;
  }

  static AudioStreamConfig defaultConfig() {
    AudioStreamConfig cfg;
    cfg.host = "192.168.178.91";
    cfg.port = 5020;
    cfg.codec = "opus";
    cfg.bitrate = 128;
    cfg.channels = 2;
    cfg.sampleRate = 48000;
    cfg.opusApplication = "audio";
    cfg.direction = "outgoing";
    cfg.sinkDevice = "";
    cfg.bufferSize = 4096;
    cfg.latencyTime = 100;
    cfg.bufferTime = 2000;
    cfg.virtualSink = false;
    cfg.virtualSinkName = "";
    return cfg;
  }
};

// ======================================================================
// Stream Info (added nodeId and virtualSinkModule)
// ======================================================================
struct StreamInfo {
  QString type;
  QString name;
  QProcess *workerProcess;
  VideoStreamConfig videoConfig;
  AudioStreamConfig audioConfig;
  bool pinned;           // is this stream saved for auto-start?
  QString nodeId;        // pipewire node id (video only)
  int virtualSinkModule; // PulseAudio module index (audio only, -1 if none)
};

// ======================================================================
// Video Stream Configuration Dialog (with custom name field)
// ======================================================================
class VideoStreamDialog : public QDialog {
  Q_OBJECT

public:
  VideoStreamDialog(QWidget *parent = nullptr);
  void setConfig(const VideoStreamConfig &config); // new
  VideoStreamConfig getConfig() const;

private slots:
  void onEncoderChanged(int index);
  void onHardwareToggled(bool checked);

private:
  // Core widgets
  QSpinBox *spinWidth;
  QSpinBox *spinHeight;
  QSpinBox *spinBitrate;
  QLineEdit *editHost;
  QSpinBox *spinPort;
  QCheckBox *checkVideoConvert;
  QLineEdit *editCustomName;

  // Encoder selection
  QComboBox *comboEncoder;
  QCheckBox *checkHardware;

  // Software x264 widgets
  QComboBox *comboSpeedPreset;
  QComboBox *comboTune;
  QComboBox *comboProfile;
  QWidget *swX264Widget;

  // Hardware encoder widgets
  QSpinBox *spinQuality;
  QComboBox *comboRateControl;
  QWidget *hwWidget;

  QVBoxLayout *mainLayout;
  QFormLayout *formLayout;
};

VideoStreamDialog::VideoStreamDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle("🎥 Video Stream Configuration");
  setMinimumWidth(550);

  mainLayout = new QVBoxLayout(this);
  formLayout = new QFormLayout();

  // ---- Custom stream name ----
  editCustomName = new QLineEdit();
  editCustomName->setPlaceholderText(
      "Leave empty for auto-name (e.g. qt-caster-video-1)");
  formLayout->addRow("📛 Stream Name:", editCustomName);

  // ---- Resolution ----
  QHBoxLayout *resLayout = new QHBoxLayout();
  spinWidth = new QSpinBox();
  spinWidth->setRange(320, 7680);
  spinWidth->setValue(1920);
  spinWidth->setSuffix(" px");
  spinHeight = new QSpinBox();
  spinHeight->setRange(240, 4320);
  spinHeight->setValue(1080);
  spinHeight->setSuffix(" px");
  resLayout->addWidget(new QLabel("📏 Width:"));
  resLayout->addWidget(spinWidth);
  resLayout->addWidget(new QLabel("📏 Height:"));
  resLayout->addWidget(spinHeight);
  formLayout->addRow("Resolution:", resLayout);

  // ---- Bitrate ----
  spinBitrate = new QSpinBox();
  spinBitrate->setRange(500, 100000);
  spinBitrate->setValue(30000);
  spinBitrate->setSuffix(" kbps");
  formLayout->addRow("⚡ Bitrate:", spinBitrate);

  // ---- Network ----
  editHost = new QLineEdit("192.168.178.91");
  formLayout->addRow("🌐 Destination Host:", editHost);
  spinPort = new QSpinBox();
  spinPort->setRange(1024, 65535);
  spinPort->setValue(5010);
  formLayout->addRow("🔌 Destination Port:", spinPort);

  // ---- Video convert ----
  checkVideoConvert = new QCheckBox("Use videoconvert");
  checkVideoConvert->setChecked(true);
  formLayout->addRow("🔄 Video Processing:", checkVideoConvert);

  // ---- Hardware acceleration toggle ----
  checkHardware = new QCheckBox("Use hardware acceleration (if available)");
  checkHardware->setChecked(false);
  connect(checkHardware, &QCheckBox::toggled, this,
          &VideoStreamDialog::onHardwareToggled);
  formLayout->addRow("⚙️ Acceleration:", checkHardware);

  // ---- Encoder selection ----
  comboEncoder = new QComboBox();
  comboEncoder->addItem("Software (x264)", "x264enc");
  comboEncoder->addItem("VAAPI H.264", "vaapih264enc");
  comboEncoder->addItem("NVENC H.264", "nvh264enc");
  comboEncoder->addItem("QSV H.264", "qsvh264enc");
  connect(comboEncoder, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &VideoStreamDialog::onEncoderChanged);
  formLayout->addRow("🎛 Encoder:", comboEncoder);

  mainLayout->addLayout(formLayout);

  // ---- Encoder-specific option panels ----
  // Software x264 panel
  swX264Widget = new QWidget();
  QFormLayout *swLayout = new QFormLayout(swX264Widget);
  comboSpeedPreset = new QComboBox();
  comboSpeedPreset->addItems({"ultrafast", "superfast", "veryfast", "faster",
                              "fast", "medium", "slow", "slower", "veryslow"});
  comboSpeedPreset->setCurrentText("ultrafast");
  swLayout->addRow("⏩ Speed Preset:", comboSpeedPreset);
  comboTune = new QComboBox();
  comboTune->addItems({"zerolatency", "film", "animation", "grain",
                       "stillimage", "fastdecode"});
  comboTune->setCurrentText("zerolatency");
  swLayout->addRow("🎛 Tune:", comboTune);
  comboProfile = new QComboBox();
  comboProfile->addItems({"baseline", "main", "high"});
  comboProfile->setCurrentText("high");
  swLayout->addRow("📊 Profile:", comboProfile);
  mainLayout->addWidget(swX264Widget);

  // Hardware panel
  hwWidget = new QWidget();
  QFormLayout *hwLayout = new QFormLayout(hwWidget);
  spinQuality = new QSpinBox();
  spinQuality->setRange(0, 51);
  spinQuality->setValue(20);
  spinQuality->setSuffix(" (lower = better)");
  hwLayout->addRow("🎚 Quality:", spinQuality);
  comboRateControl = new QComboBox();
  comboRateControl->addItem("CBR", "cbr");
  comboRateControl->addItem("VBR", "vbr");
  comboRateControl->addItem("CQP", "cqp");
  comboRateControl->setCurrentText("cbr");
  hwLayout->addRow("📈 Rate Control:", comboRateControl);
  mainLayout->addWidget(hwWidget);

  // ---- Button box ----
  QDialogButtonBox *buttonBox =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttonBox->button(QDialogButtonBox::Ok)->setText("✅ Start");
  buttonBox->button(QDialogButtonBox::Cancel)->setText("❌ Cancel");
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttonBox);

  // Initial state
  onHardwareToggled(false);
  onEncoderChanged(0);
}

void VideoStreamDialog::onHardwareToggled(bool checked) {
  comboEncoder->setEnabled(checked);
  if (!checked) {
    comboEncoder->setCurrentIndex(0);
    comboEncoder->setEnabled(false);
  }
}

void VideoStreamDialog::onEncoderChanged(int index) {
  QString encoder = comboEncoder->currentData().toString();
  bool isSoftware = (encoder == "x264enc");
  swX264Widget->setVisible(isSoftware);
  hwWidget->setVisible(!isSoftware);
}

void VideoStreamDialog::setConfig(const VideoStreamConfig &cfg) {
  editCustomName->setText(cfg.customName);
  spinWidth->setValue(cfg.width);
  spinHeight->setValue(cfg.height);
  spinBitrate->setValue(cfg.bitrate);
  editHost->setText(cfg.host);
  spinPort->setValue(cfg.port);
  checkVideoConvert->setChecked(cfg.useVideoConvert);
  checkHardware->setChecked(cfg.useHardware);
  // Find encoder index
  int encoderIndex = comboEncoder->findData(cfg.encoder);
  if (encoderIndex >= 0)
    comboEncoder->setCurrentIndex(encoderIndex);
  comboSpeedPreset->setCurrentText(cfg.speedPreset);
  comboTune->setCurrentText(cfg.tune);
  comboProfile->setCurrentText(cfg.profile);
  spinQuality->setValue(cfg.qualityLevel);
  int rcIndex = comboRateControl->findData(cfg.rateControl);
  if (rcIndex >= 0)
    comboRateControl->setCurrentIndex(rcIndex);
}

VideoStreamConfig VideoStreamDialog::getConfig() const {
  VideoStreamConfig cfg;
  cfg.customName = editCustomName->text().trimmed();
  cfg.width = spinWidth->value();
  cfg.height = spinHeight->value();
  cfg.bitrate = spinBitrate->value();
  cfg.host = editHost->text();
  cfg.port = spinPort->value();
  cfg.useVideoConvert = checkVideoConvert->isChecked();
  cfg.encoder = comboEncoder->currentData().toString();
  cfg.useHardware = checkHardware->isChecked();
  cfg.speedPreset = comboSpeedPreset->currentText();
  cfg.tune = comboTune->currentText();
  cfg.profile = comboProfile->currentText();
  cfg.qualityLevel = spinQuality->value();
  cfg.rateControl = comboRateControl->currentData().toString();
  return cfg;
}

// ======================================================================
// Audio Stream Configuration Dialog (with custom name field + virtual sink)
// ======================================================================
class AudioStreamDialog : public QDialog {
  Q_OBJECT

public:
  AudioStreamDialog(QWidget *parent = nullptr);
  void setConfig(const AudioStreamConfig &config); // new
  AudioStreamConfig getConfig() const;

private slots:
  void onCodecChanged(int index);
  void onDirectionChanged(int index);
  void onVirtualSinkToggled(bool checked);

private:
  QLineEdit *editCustomName;
  QLineEdit *editHost;
  QLabel *hostLabel;
  QSpinBox *spinPort;
  QComboBox *comboCodec;
  QSpinBox *spinBitrate;
  QSpinBox *spinChannels;
  QComboBox *comboSampleRate; // fixed: capitalized S
  QComboBox *comboOpusApplication;
  QComboBox *comboDirection;
  QLineEdit *editSinkDevice;
  QSpinBox *spinBufferSize;
  QSpinBox *spinLatencyTime;
  QSpinBox *spinBufferTime;
  QCheckBox *checkVirtualSink;    // new
  QLineEdit *editVirtualSinkName; // new
};

AudioStreamDialog::AudioStreamDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle("🎤 Audio Stream Configuration");
  setMinimumWidth(450);

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  QFormLayout *formLayout = new QFormLayout();

  // ---- Advanced buffering options ----
  QGroupBox *advancedGroup =
      new QGroupBox("⚙️ Advanced Buffer Settings (0 = default)", this);
  QFormLayout *advancedLayout = new QFormLayout(advancedGroup);

  // ---- Custom stream name ----
  editCustomName = new QLineEdit();
  editCustomName->setPlaceholderText(
      "Leave empty for auto-name (e.g. qt-caster-audio-1)");
  formLayout->addRow("📛 Stream Name:", editCustomName);

  // ---- Direction ----
  comboDirection = new QComboBox();
  comboDirection->addItem("📤 Outgoing (send microphone)", "outgoing");
  comboDirection->addItem("📥 Incoming (receive & play)", "incoming");

  connect(
      comboDirection,
      static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
      this, &AudioStreamDialog::onDirectionChanged);
  formLayout->addRow("🔄 Direction:", comboDirection);

  // ---- Host / Listen Address ----
  hostLabel = new QLabel("🌐 Destination Host:");
  editHost = new QLineEdit();
  editHost->setText("192.168.178.91");
  formLayout->addRow(hostLabel, editHost);

  // ---- Port ----
  spinPort = new QSpinBox();
  spinPort->setRange(1024, 65535);
  spinPort->setValue(5020);
  formLayout->addRow("🔌 Port:", spinPort);

  // ---- Sink Device (only for incoming) ----
  editSinkDevice = new QLineEdit();
  editSinkDevice->setPlaceholderText(
      "e.g. Virtual-Music (leave empty for default sink)");
  editSinkDevice->setEnabled(false); // initially disabled
  formLayout->addRow("🎧 Sink Device:", editSinkDevice);

  // ---- Codec ----
  comboCodec = new QComboBox();
  comboCodec->addItem("🎵 Opus", "opus");
  comboCodec->addItem("🎶 AAC", "aac");
  formLayout->addRow("🎛 Codec:", comboCodec);

  // ---- Bitrate ----
  spinBitrate = new QSpinBox();
  spinBitrate->setRange(16, 512);
  spinBitrate->setValue(128);
  spinBitrate->setSuffix(" kbps");
  formLayout->addRow("⚡ Bitrate:", spinBitrate);

  // ---- Channels ----
  spinChannels = new QSpinBox();
  spinChannels->setRange(1, 8);
  spinChannels->setValue(2);
  spinChannels->setSuffix(" ch");
  formLayout->addRow("🎧 Channels:", spinChannels);

  // ---- Sample Rate ----
  comboSampleRate = new QComboBox();
  comboSampleRate->addItem("48 kHz", 48000);
  comboSampleRate->addItem("44.1 kHz", 44100);
  comboSampleRate->addItem("96 kHz", 96000);
  comboSampleRate->setCurrentIndex(0);
  formLayout->addRow("⏱ Sample Rate:", comboSampleRate);

  // ---- Opus Application (only for Opus) ----
  comboOpusApplication = new QComboBox();
  comboOpusApplication->addItem("🎼 Audio", "audio");
  comboOpusApplication->addItem("🎤 VoIP", "voip");
  comboOpusApplication->addItem("⚡ Low Delay", "lowdelay");
  formLayout->addRow("📱 Application:", comboOpusApplication);

  // ---- Virtual sink (new) ----
  checkVirtualSink = new QCheckBox("Create virtual sink (for routing audio)");
  checkVirtualSink->setChecked(false);
  connect(checkVirtualSink, &QCheckBox::toggled, this,
          &AudioStreamDialog::onVirtualSinkToggled);
  formLayout->addRow("🎚️ Virtual Sink:", checkVirtualSink);
  editVirtualSinkName = new QLineEdit();
  editVirtualSinkName->setPlaceholderText(
      "Leave empty for auto-generated name");
  editVirtualSinkName->setEnabled(false);
  formLayout->addRow("📛 Sink Name:", editVirtualSinkName);

  // Advanced settings
  spinBufferSize = new QSpinBox();
  spinBufferSize->setRange(0, 1048576);
  spinBufferSize->setValue(4096);
  spinBufferSize->setSuffix(" bytes");
  spinBufferSize->setSpecialValueText("Auto");
  advancedLayout->addRow("📦 Buffer Size:", spinBufferSize);

  spinLatencyTime = new QSpinBox();
  spinLatencyTime->setRange(0, 10000);
  spinLatencyTime->setValue(100);
  spinLatencyTime->setSuffix(" ms");
  spinLatencyTime->setSpecialValueText("Auto");
  advancedLayout->addRow("⏱️ Latency Time:", spinLatencyTime);

  spinBufferTime = new QSpinBox();
  spinBufferTime->setRange(0, 10000);
  spinBufferTime->setValue(2000);
  spinBufferTime->setSuffix(" ms");
  spinBufferTime->setSpecialValueText("Auto");
  advancedLayout->addRow("⏱️ Buffer Time:", spinBufferTime);

  connect(comboCodec, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &AudioStreamDialog::onCodecChanged);
  onCodecChanged(0);

  mainLayout->addLayout(formLayout);
  mainLayout->addWidget(advancedGroup);

  QDialogButtonBox *buttonBox =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttonBox->button(QDialogButtonBox::Ok)->setText("✅ Start");
  buttonBox->button(QDialogButtonBox::Cancel)->setText("❌ Cancel");
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttonBox);
}

void AudioStreamDialog::onDirectionChanged(int index) {
  bool incoming = (comboDirection->itemData(index).toString() == "incoming");
  if (incoming) {
    hostLabel->setText("🌐 Listen Address:");
    editHost->setText("0.0.0.0");
    editSinkDevice->setEnabled(true);
  } else {
    hostLabel->setText("🌐 Destination Host:");
    editHost->setText("192.168.178.91");
    editSinkDevice->setEnabled(false);
  }
}

void AudioStreamDialog::onCodecChanged(int index) {
  QString codec = comboCodec->itemData(index).toString();
  comboOpusApplication->setEnabled(codec == "opus");
  if (codec == "opus") {
    spinBitrate->setRange(16, 512);
    spinBitrate->setValue(128);
  } else {
    spinBitrate->setRange(32, 512);
    spinBitrate->setValue(192);
  }
}

void AudioStreamDialog::onVirtualSinkToggled(bool checked) {
  editVirtualSinkName->setEnabled(checked);
  if (!checked)
    editVirtualSinkName->clear();
}

void AudioStreamDialog::setConfig(const AudioStreamConfig &cfg) {
  editCustomName->setText(cfg.customName);
  editHost->setText(cfg.host);
  spinPort->setValue(cfg.port);
  int codecIndex = comboCodec->findData(cfg.codec);
  if (codecIndex >= 0)
    comboCodec->setCurrentIndex(codecIndex);
  spinBitrate->setValue(cfg.bitrate);
  spinChannels->setValue(cfg.channels);
  int srIndex = comboSampleRate->findData(cfg.sampleRate);
  if (srIndex >= 0)
    comboSampleRate->setCurrentIndex(srIndex);
  int appIndex = comboOpusApplication->findData(cfg.opusApplication);
  if (appIndex >= 0)
    comboOpusApplication->setCurrentIndex(appIndex);
  int dirIndex = comboDirection->findData(cfg.direction);
  if (dirIndex >= 0)
    comboDirection->setCurrentIndex(dirIndex);
  editSinkDevice->setText(cfg.sinkDevice);
  spinBufferSize->setValue(cfg.bufferSize);
  spinLatencyTime->setValue(cfg.latencyTime);
  spinBufferTime->setValue(cfg.bufferTime);
  checkVirtualSink->setChecked(cfg.virtualSink);
  editVirtualSinkName->setText(cfg.virtualSinkName);
  onVirtualSinkToggled(cfg.virtualSink);
}

AudioStreamConfig AudioStreamDialog::getConfig() const {
  AudioStreamConfig config;
  config.customName = editCustomName->text().trimmed();
  config.host = editHost->text();
  config.port = spinPort->value();
  config.codec = comboCodec->currentData().toString();
  config.bitrate = spinBitrate->value();
  config.channels = spinChannels->value();
  config.sampleRate = comboSampleRate->currentData().toInt();
  config.opusApplication = comboOpusApplication->currentData().toString();
  config.direction = comboDirection->currentData().toString();
  config.sinkDevice = editSinkDevice->text().trimmed();
  config.bufferSize = spinBufferSize->value();
  config.latencyTime = spinLatencyTime->value();
  config.bufferTime = spinBufferTime->value();
  config.virtualSink = checkVirtualSink->isChecked();
  config.virtualSinkName = editVirtualSinkName->text().trimmed();
  return config;
}

// ======================================================================
// StreamManager - Main Window
// ======================================================================
class StreamManager : public QMainWindow {
  Q_OBJECT

public:
  static StreamManager *instance;     // for signal handler
  static void signalHandler(int sig); // static signal handler

  StreamManager(QWidget *parent = nullptr);
  ~StreamManager();

protected:
  void closeEvent(QCloseEvent *event) override;

private slots:
  void createVideoStream();
  void createAudioStream();
  void onStreamDoubleClicked(int row, int column);
  void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
  void minimizeToTray();
  void quitApplication();
  void onWorkerOutput();
  void onWorkerFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void updateStreamTable();
  void stopStreamFromTray();
  void updateTrayTooltip();
  void onPinToggled(int row, int column);
  void onTrayMenuAboutToShow();
  void cleanupAndQuit();

  // New context menu actions
  void showContextMenu(const QPoint &pos);
  void editStream();
  void visualizeVideoStream();

private:
  void setupSystemTray();
  void setupTable();
  void loadSavedStreams();
  void saveStreamConfig(const StreamInfo &stream);
  void removeSavedStream(const QString &name);
  QString getConfigFilePath() const;
  void terminateAllStreams();
  void stopAndRemoveStream(int row);

  QPushButton *btnCreateVideo;
  QPushButton *btnCreateAudio;
  QPushButton *btnMinimize;
  QPushButton *btnQuit;
  QTableWidget *streamTable;

  QList<StreamInfo> activeStreams;
  int videoStreamCounter;
  int audioStreamCounter;

  QSystemTrayIcon *trayIcon;
  QMenu *trayMenu;
  QTimer *statusTimer;

  QMap<QAction *, int> trayStreamActions;

  bool m_isQuitting;
  int m_contextMenuRow; // added
};

// Static member definition
StreamManager *StreamManager::instance = nullptr;

StreamManager::StreamManager(QWidget *parent)
    : QMainWindow(parent), videoStreamCounter(0), audioStreamCounter(0),
      trayIcon(nullptr), trayMenu(nullptr), statusTimer(nullptr),
      m_isQuitting(false), m_contextMenuRow(-1) {
  instance = this;

  setWindowTitle("🎬 Qt-Caster Stream Manager");
  resize(950, 600);

  QWidget *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

  QHBoxLayout *buttonLayout = new QHBoxLayout();
  btnCreateVideo = new QPushButton("🎥 Create Video Stream", this);
  btnCreateAudio = new QPushButton("🎤 Create Audio Stream", this);
  buttonLayout->addWidget(btnCreateVideo);
  buttonLayout->addWidget(btnCreateAudio);

  streamTable = new QTableWidget(this);
  setupTable();

  QHBoxLayout *bottomButtonLayout = new QHBoxLayout();
  btnMinimize = new QPushButton("⤵ Minimize to Tray", this);
  btnQuit = new QPushButton("⏹ Quit Application", this);
  bottomButtonLayout->addWidget(btnMinimize);
  bottomButtonLayout->addWidget(btnQuit);

  mainLayout->addLayout(buttonLayout);
  mainLayout->addWidget(streamTable);
  mainLayout->addLayout(bottomButtonLayout);

  connect(btnCreateVideo, &QPushButton::clicked, this,
          &StreamManager::createVideoStream);
  connect(btnCreateAudio, &QPushButton::clicked, this,
          &StreamManager::createAudioStream);
  connect(btnMinimize, &QPushButton::clicked, this,
          &StreamManager::minimizeToTray);
  connect(btnQuit, &QPushButton::clicked, this,
          &StreamManager::quitApplication);
  connect(streamTable, &QTableWidget::cellDoubleClicked, this,
          &StreamManager::onStreamDoubleClicked);
  connect(streamTable, &QTableWidget::cellChanged, this,
          &StreamManager::onPinToggled);
  streamTable->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(streamTable, &QTableWidget::customContextMenuRequested, this,
          &StreamManager::showContextMenu);

  setupSystemTray();

  statusTimer = new QTimer(this);
  statusTimer->setInterval(1000);
  connect(statusTimer, &QTimer::timeout, this,
          &StreamManager::updateStreamTable);
  statusTimer->start();

  loadSavedStreams();
}

StreamManager::~StreamManager() {
  if (instance == this)
    instance = nullptr;
}

void StreamManager::setupTable() {
  QStringList headers;
  headers << "📛 Stream" << "🔄 Status" << "🌐 Destination" << "⚙️ Options"
          << "📊 Activity" << "📌 Pin";
  streamTable->setColumnCount(headers.size());
  streamTable->setHorizontalHeaderLabels(headers);
  streamTable->verticalHeader()->setVisible(false);
  streamTable->setAlternatingRowColors(false);
  streamTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  streamTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

  streamTable->horizontalHeader()->setSectionResizeMode(0,
                                                        QHeaderView::Stretch);
  streamTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  streamTable->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  streamTable->horizontalHeader()->setSectionResizeMode(3,
                                                        QHeaderView::Stretch);
  streamTable->horizontalHeader()->setSectionResizeMode(
      4, QHeaderView::ResizeToContents);
  streamTable->horizontalHeader()->setSectionResizeMode(
      5, QHeaderView::ResizeToContents);
}

void StreamManager::setupSystemTray() {
  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    qWarning() << "⚠️ System tray not available – tray icon disabled.";
    trayIcon = nullptr;
    return;
  }

  QString iconPath = QCoreApplication::applicationDirPath() + "/icon.png";
  QIcon icon;
  if (QFile::exists(iconPath)) {
    icon = QIcon(iconPath);
    qDebug() << "✅ Loaded tray icon from:" << iconPath;
  } else {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::darkBlue);
    icon = QIcon(pixmap);
    qDebug() << "ℹ️ Using default tray icon (icon.png not found)";
  }

  trayIcon = new QSystemTrayIcon(icon, this);
  connect(trayIcon, &QSystemTrayIcon::activated, this,
          &StreamManager::onTrayIconActivated);

  trayMenu = new QMenu(this);
  trayIcon->setContextMenu(trayMenu);
  connect(trayMenu, &QMenu::aboutToShow, this,
          &StreamManager::onTrayMenuAboutToShow);

  trayIcon->show();
  updateTrayTooltip();
}

void StreamManager::onTrayMenuAboutToShow() {
  if (!trayMenu)
    return;

  trayMenu->clear();
  trayStreamActions.clear();

  QAction *showAction = trayMenu->addAction("🪟 Show Window");
  connect(showAction, &QAction::triggered, this, &QWidget::show);
  trayMenu->addSeparator();

  QAction *videoAction = trayMenu->addAction("🎥 Create Video Stream");
  QAction *audioAction = trayMenu->addAction("🎤 Create Audio Stream");
  connect(videoAction, &QAction::triggered, this,
          &StreamManager::createVideoStream);
  connect(audioAction, &QAction::triggered, this,
          &StreamManager::createAudioStream);
  trayMenu->addSeparator();

  if (!activeStreams.isEmpty()) {
    QMenu *streamsMenu = trayMenu->addMenu("📡 Active Streams");
    for (int i = 0; i < activeStreams.size(); ++i) {
      const StreamInfo &stream = activeStreams[i];
      bool isRunning = stream.workerProcess &&
                       stream.workerProcess->state() == QProcess::Running;
      QString statusIcon = isRunning ? "🟢" : "🔴";
      QString text = QString("%1 %2").arg(statusIcon).arg(stream.name);
      QAction *streamAction = streamsMenu->addAction(text);
      streamAction->setData(i);
      connect(streamAction, &QAction::triggered, this,
              &StreamManager::stopStreamFromTray);
      trayStreamActions[streamAction] = i;
    }
  }

  trayMenu->addSeparator();
  QAction *quitAction = trayMenu->addAction("⏹ Quit");
  connect(quitAction, &QAction::triggered, this,
          &StreamManager::quitApplication);
}

void StreamManager::stopStreamFromTray() {
  QAction *action = qobject_cast<QAction *>(sender());
  if (!action)
    return;

  int index = action->data().toInt();
  if (index < 0 || index >= activeStreams.size())
    return;

  stopAndRemoveStream(index);
}

void StreamManager::updateTrayTooltip() {
  if (!trayIcon)
    return;

  QString tooltip = "🎬 Qt-Caster Stream Manager\n";
  if (activeStreams.isEmpty()) {
    tooltip += "📡 No active streams";
  } else {
    tooltip += "━━━━━━━━━━━━━━━━━━━━\n";
    for (const StreamInfo &stream : activeStreams) {
      bool isRunning = stream.workerProcess &&
                       stream.workerProcess->state() == QProcess::Running;
      QString statusIcon = isRunning ? "🟢" : "🔴";
      tooltip += QString("%1 %2\n").arg(statusIcon).arg(stream.name);
    }
  }
  trayIcon->setToolTip(tooltip);
}

void StreamManager::closeEvent(QCloseEvent *event) {
  if (m_isQuitting) {
    event->accept();
    return;
  }

  event->ignore();
  hide();
  if (trayIcon) {
    trayIcon->showMessage(
        "🎬 Qt-Caster",
        "Application minimized to tray. Right-click the tray icon for options.",
        QSystemTrayIcon::Information, 2000);
  }
}

void StreamManager::onTrayIconActivated(
    QSystemTrayIcon::ActivationReason reason) {
  if (reason == QSystemTrayIcon::Trigger ||
      reason == QSystemTrayIcon::DoubleClick) {
    show();
    raise();
    activateWindow();
  }
}

void StreamManager::minimizeToTray() {
  hide();
  if (trayIcon) {
    trayIcon->showMessage("🎬 Qt-Caster", "Application minimized to tray.",
                          QSystemTrayIcon::Information, 2000);
  }
}

// ----------------------------------------------------------------------
// Common cleanup logic
// ----------------------------------------------------------------------
void StreamManager::terminateAllStreams() {
  qDebug() << "🧹 Terminating all active streams...";

  // Step 1: Collect all worker processes into a separate list
  QList<QProcess *> processes;
  for (const StreamInfo &stream : activeStreams) {
    if (stream.workerProcess)
      processes.append(stream.workerProcess);
  }

  // Step 2: Terminate each process (no reliance on activeStreams)
  for (QProcess *proc : processes) {
    if (proc->state() == QProcess::Running) {
      qDebug() << "   Stopping process";
      proc->terminate();
      if (!proc->waitForFinished(2000)) {
        proc->kill();
        proc->waitForFinished(1000);
      }
    }
    proc->deleteLater();
  }

  // Step 3: Clear the list and update UI
  activeStreams.clear();
  updateStreamTable();
  updateTrayTooltip();

  qDebug() << "✅ All streams terminated.";
}

void StreamManager::cleanupAndQuit() {
  if (m_isQuitting)
    return;
  m_isQuitting = true;

  qDebug() << "🛑 Starting graceful shutdown...";

  if (trayIcon) {
    trayIcon->hide();
    trayIcon->deleteLater();
    trayIcon = nullptr;
  }

  terminateAllStreams();
  QApplication::quit();
}

void StreamManager::quitApplication() { cleanupAndQuit(); }

// ======================================================================
// Stream Creation with Custom Name
// ======================================================================
void StreamManager::createVideoStream() {
  qDebug() << "🎥 Creating video stream...";

  VideoStreamDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted) {
    qDebug() << "❌ Video stream creation cancelled";
    return;
  }

  VideoStreamConfig config = dialog.getConfig();

  // Determine stream name
  QString streamName;
  if (!config.customName.isEmpty()) {
    streamName = config.customName;
  } else {
    videoStreamCounter++;
    streamName = QString("qt-caster-video-%1").arg(videoStreamCounter);
  }

  QProcess *worker = new QProcess(this);
  QString workerPath =
      QCoreApplication::applicationDirPath() + "/qt-caster-worker";

  QJsonObject configJson = config.toJson();
  configJson["streamName"] = streamName;
  configJson["type"] = "video";
  QString configStr = QJsonDocument(configJson).toJson(QJsonDocument::Compact);

  qDebug() << "🚀 Launching worker:" << workerPath;
  qDebug() << "📄 Config:" << configStr;

  worker->start(workerPath, QStringList() << configStr);

  connect(worker, &QProcess::readyReadStandardOutput, this,
          &StreamManager::onWorkerOutput);
  connect(worker, &QProcess::readyReadStandardError, this,
          &StreamManager::onWorkerOutput);
  connect(worker, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &StreamManager::onWorkerFinished);

  StreamInfo info;
  info.type = "Video";
  info.name = streamName;
  info.workerProcess = worker;
  info.videoConfig = config;
  info.pinned = false;
  info.virtualSinkModule = -1; // not used for video

  activeStreams.append(info);
  updateStreamTable();
  updateTrayTooltip();
}

void StreamManager::createAudioStream() {
  qDebug() << "🎤 Creating audio stream...";

  AudioStreamDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted) {
    qDebug() << "❌ Audio stream creation cancelled";
    return;
  }

  AudioStreamConfig config = dialog.getConfig();

  // Determine stream name
  QString streamName;
  if (!config.customName.isEmpty()) {
    streamName = config.customName;
  } else {
    audioStreamCounter++;
    streamName = QString("qt-caster-audio-%1").arg(audioStreamCounter);
  }

  QProcess *worker = new QProcess(this);
  QString workerPath =
      QCoreApplication::applicationDirPath() + "/qt-caster-worker";

  QJsonObject configJson = config.toJson();
  configJson["streamName"] = streamName;
  configJson["type"] = "audio";
  QString configStr = QJsonDocument(configJson).toJson(QJsonDocument::Compact);

  qDebug() << "🚀 Launching worker:" << workerPath;
  qDebug() << "📄 Config:" << configStr;

  worker->start(workerPath, QStringList() << configStr);

  connect(worker, &QProcess::readyReadStandardOutput, this,
          &StreamManager::onWorkerOutput);
  connect(worker, &QProcess::readyReadStandardError, this,
          &StreamManager::onWorkerOutput);
  connect(worker, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &StreamManager::onWorkerFinished);

  StreamInfo info;
  info.type = "Audio";
  info.name = streamName;
  info.workerProcess = worker;
  info.audioConfig = config;
  info.pinned = false;
  info.virtualSinkModule = -1; // will be set by worker if created

  activeStreams.append(info);
  updateStreamTable();
  updateTrayTooltip();
}

void StreamManager::onWorkerOutput() {
  QProcess *worker = qobject_cast<QProcess *>(sender());
  if (!worker)
    return;

  QString output = worker->readAllStandardOutput();
  QString error = worker->readAllStandardError();

  // Parse both output and error for node ID
  QString combined = output + "\n" + error;
  QRegularExpression re("NODE_ID:\\s*(\\d+)");
  QRegularExpressionMatch match = re.match(combined);
  if (match.hasMatch()) {
    QString nodeId = match.captured(1);
    for (StreamInfo &stream : activeStreams) {
      if (stream.workerProcess == worker) {
        stream.nodeId = nodeId;
        qDebug() << "📡 Got node ID for stream" << stream.name << ":" << nodeId;
        break;
      }
    }
  }

  if (!output.isEmpty())
    qDebug().noquote() << "[Worker]" << output.trimmed();
  if (!error.isEmpty())
    qDebug().noquote() << "[Worker]" << error.trimmed();
}

void StreamManager::onWorkerFinished(int exitCode,
                                     QProcess::ExitStatus exitStatus) {
  QProcess *worker = qobject_cast<QProcess *>(sender());
  qDebug() << "🏁 Worker finished – code:" << exitCode
           << "status:" << exitStatus;

  for (int i = 0; i < activeStreams.size(); ++i) {
    if (activeStreams[i].workerProcess == worker) {
      // If it had a virtual sink, we should remove it, but worker should have
      // already removed it. We'll rely on worker cleanup.
      activeStreams[i].workerProcess = nullptr;
      break;
    }
  }
  updateStreamTable();
  updateTrayTooltip();
}

// ======================================================================
// Table Update with Pin Column
// ======================================================================
void StreamManager::updateStreamTable() {
  streamTable->setRowCount(activeStreams.size());
  streamTable->blockSignals(true);

  for (int i = 0; i < activeStreams.size(); ++i) {
    const StreamInfo &stream = activeStreams[i];
    bool isRunning = stream.workerProcess &&
                     stream.workerProcess->state() == QProcess::Running;

    // Column 0: Stream name
    QString nameIcon = (stream.type == "Video") ? "📹" : "🎵";
    QString displayName = QString("%1 %2").arg(nameIcon).arg(stream.name);
    QTableWidgetItem *nameItem = new QTableWidgetItem(displayName);
    nameItem->setData(Qt::UserRole, i);
    nameItem->setToolTip(stream.type);
    streamTable->setItem(i, 0, nameItem);

    // Column 1: Status
    QString statusText = isRunning ? "🟢 Running" : "🔴 Stopped";
    QTableWidgetItem *statusItem = new QTableWidgetItem(statusText);
    streamTable->setItem(i, 1, statusItem);

    // Column 2: Destination
    QString dest;
    if (stream.type == "Video") {
      dest = QString("%1:%2")
                 .arg(stream.videoConfig.host)
                 .arg(stream.videoConfig.port);
    } else {
      dest = QString("%1:%2")
                 .arg(stream.audioConfig.host)
                 .arg(stream.audioConfig.port);
    }
    QTableWidgetItem *destItem = new QTableWidgetItem(dest);
    streamTable->setItem(i, 2, destItem);

    // Column 3: Options
    QString opts;
    if (stream.type == "Video") {
      opts = QString("%1x%2 @ %3 kbps, %4")
                 .arg(stream.videoConfig.width)
                 .arg(stream.videoConfig.height)
                 .arg(stream.videoConfig.bitrate)
                 .arg(stream.videoConfig.encoder);
    } else {
      opts = QString("%1, %2 kbps, %3ch")
                 .arg(stream.audioConfig.codec.toUpper())
                 .arg(stream.audioConfig.bitrate)
                 .arg(stream.audioConfig.channels);
    }
    QTableWidgetItem *optsItem = new QTableWidgetItem(opts);
    streamTable->setItem(i, 3, optsItem);

    // Column 4: Activity
    QTableWidgetItem *activityItem = new QTableWidgetItem();
    activityItem->setText(isRunning ? "🟢" : "🔴");
    activityItem->setTextAlignment(Qt::AlignCenter);
    streamTable->setItem(i, 4, activityItem);

    // Column 5: Pin checkbox
    QTableWidgetItem *pinItem = new QTableWidgetItem();
    pinItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    pinItem->setCheckState(stream.pinned ? Qt::Checked : Qt::Unchecked);
    pinItem->setTextAlignment(Qt::AlignCenter);
    streamTable->setItem(i, 5, pinItem);
  }

  streamTable->blockSignals(false);
  streamTable->resizeColumnsToContents();
  streamTable->horizontalHeader()->setSectionResizeMode(0,
                                                        QHeaderView::Stretch);
  streamTable->horizontalHeader()->setSectionResizeMode(3,
                                                        QHeaderView::Stretch);
  streamTable->horizontalHeader()->setSectionResizeMode(
      4, QHeaderView::ResizeToContents);
  streamTable->horizontalHeader()->setSectionResizeMode(
      5, QHeaderView::ResizeToContents);
}

void StreamManager::onStreamDoubleClicked(int row, int column) {
  Q_UNUSED(column);
  if (row < 0 || row >= activeStreams.size())
    return;

  stopAndRemoveStream(row);
}

// ----------------------------------------------------------------------
// Helper to stop and remove a stream, removing saved config if pinned
// ----------------------------------------------------------------------
void StreamManager::stopAndRemoveStream(int row) {
  if (row < 0 || row >= activeStreams.size())
    return;

  StreamInfo &stream = activeStreams[row];

  if (stream.workerProcess) {
    // Disconnect all signals to avoid stale callbacks
    disconnect(stream.workerProcess, nullptr, this, nullptr);

    if (stream.workerProcess->state() == QProcess::Running) {
      qDebug() << "🛑 Stopping stream:" << stream.name;
      stream.workerProcess->terminate();
      if (!stream.workerProcess->waitForFinished(2000)) {
        stream.workerProcess->kill();
        stream.workerProcess->waitForFinished();
      }
    }
    stream.workerProcess->deleteLater();
    stream.workerProcess = nullptr;
  }

  // If stream was pinned, remove from saved config
  if (stream.pinned) {
    removeSavedStream(stream.name);
  }

  activeStreams.removeAt(row);
  updateStreamTable();
  updateTrayTooltip();
}

// ======================================================================
// Pin / Save Configuration
// ======================================================================
void StreamManager::onPinToggled(int row, int column) {
  if (column != 5)
    return;

  if (row < 0 || row >= activeStreams.size())
    return;

  StreamInfo &stream = activeStreams[row];
  QTableWidgetItem *pinItem = streamTable->item(row, 5);
  if (!pinItem)
    return;

  bool checked = (pinItem->checkState() == Qt::Checked);
  if (stream.pinned == checked)
    return;

  stream.pinned = checked;

  if (checked) {
    saveStreamConfig(stream);
    qDebug() << "📌 Pinned stream:" << stream.name;
  } else {
    removeSavedStream(stream.name);
    qDebug() << "❌ Unpinned stream:" << stream.name;
  }
}

QString StreamManager::getConfigFilePath() const {
  QString configDir =
      QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  if (configDir.isEmpty())
    configDir = QDir::home().filePath(".config/qt-caster");
  QDir dir(configDir);
  if (!dir.exists())
    dir.mkpath(".");
  return configDir + "/saved-streams.json";
}

void StreamManager::saveStreamConfig(const StreamInfo &stream) {
  QString path = getConfigFilePath();
  QJsonObject root;

  QFile file(path);
  if (file.exists() && file.open(QIODevice::ReadOnly)) {
    QByteArray data = file.readAll();
    file.close();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject())
      root = doc.object();
  }

  QJsonObject streamObj;
  streamObj["type"] = stream.type;
  streamObj["name"] = stream.name;

  if (stream.type == "Video") {
    streamObj["config"] = stream.videoConfig.toJson();
  } else {
    streamObj["config"] = stream.audioConfig.toJson();
  }

  root[stream.name] = streamObj;

  if (file.open(QIODevice::WriteOnly)) {
    QJsonDocument doc(root);
    file.write(doc.toJson());
    file.close();
  }
}

void StreamManager::removeSavedStream(const QString &name) {
  QString path = getConfigFilePath();
  QFile file(path);
  if (!file.exists())
    return;

  if (!file.open(QIODevice::ReadOnly))
    return;

  QByteArray data = file.readAll();
  file.close();
  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject())
    return;

  QJsonObject root = doc.object();
  if (root.contains(name)) {
    root.remove(name);
    if (file.open(QIODevice::WriteOnly)) {
      QJsonDocument newDoc(root);
      file.write(newDoc.toJson());
      file.close();
    }
  }
}

void StreamManager::loadSavedStreams() {
  QString path = getConfigFilePath();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return;

  QByteArray data = file.readAll();
  file.close();
  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject())
    return;

  QJsonObject root = doc.object();
  for (auto it = root.begin(); it != root.end(); ++it) {
    QJsonObject streamObj = it.value().toObject();
    QString type = streamObj["type"].toString();
    QString name = streamObj["name"].toString();
    QJsonObject configObj = streamObj["config"].toObject();

    if (type == "Video") {
      VideoStreamConfig cfg = VideoStreamConfig::fromJson(configObj);
      // Re-create the stream
      QProcess *worker = new QProcess(this);
      QString workerPath =
          QCoreApplication::applicationDirPath() + "/qt-caster-worker";

      QJsonObject configJson = cfg.toJson();
      configJson["streamName"] = name;
      configJson["type"] = "video";
      QString configStr =
          QJsonDocument(configJson).toJson(QJsonDocument::Compact);

      worker->start(workerPath, QStringList() << configStr);
      connect(worker, &QProcess::readyReadStandardOutput, this,
              &StreamManager::onWorkerOutput);
      connect(worker, &QProcess::readyReadStandardError, this,
              &StreamManager::onWorkerOutput);
      connect(worker,
              QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
              this, &StreamManager::onWorkerFinished);

      StreamInfo info;
      info.type = "Video";
      info.name = name;
      info.workerProcess = worker;
      info.videoConfig = cfg;
      info.pinned = true;

      activeStreams.append(info);
    } else if (type == "Audio") {
      AudioStreamConfig cfg = AudioStreamConfig::fromJson(configObj);
      QProcess *worker = new QProcess(this);
      QString workerPath =
          QCoreApplication::applicationDirPath() + "/qt-caster-worker";

      QJsonObject configJson = cfg.toJson();
      configJson["streamName"] = name;
      configJson["type"] = "audio";
      QString configStr =
          QJsonDocument(configJson).toJson(QJsonDocument::Compact);

      worker->start(workerPath, QStringList() << configStr);
      connect(worker, &QProcess::readyReadStandardOutput, this,
              &StreamManager::onWorkerOutput);
      connect(worker, &QProcess::readyReadStandardError, this,
              &StreamManager::onWorkerOutput);
      connect(worker,
              QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
              this, &StreamManager::onWorkerFinished);

      StreamInfo info;
      info.type = "Audio";
      info.name = name;
      info.workerProcess = worker;
      info.audioConfig = cfg;
      info.pinned = true;

      activeStreams.append(info);
    }
  }

  updateStreamTable();
  updateTrayTooltip();
  qDebug() << "📂 Loaded" << activeStreams.size() << "saved streams.";
}

// ======================================================================
// Context Menu Implementation
// ======================================================================
void StreamManager::showContextMenu(const QPoint &pos) {
  int row = streamTable->indexAt(pos).row();
  if (row < 0 || row >= activeStreams.size())
    return;

  m_contextMenuRow = row;

  QMenu contextMenu(this);
  QAction *editAction = contextMenu.addAction("✏️ Edit Stream");
  connect(editAction, &QAction::triggered, this, &StreamManager::editStream);

  if (activeStreams[row].type == "Video") {
    QAction *visualizeAction = contextMenu.addAction("👁️ Visualize Stream");
    connect(visualizeAction, &QAction::triggered, this,
            &StreamManager::visualizeVideoStream);
  }

  contextMenu.exec(streamTable->viewport()->mapToGlobal(pos));
}

void StreamManager::editStream() {
  int row = m_contextMenuRow;
  if (row < 0 || row >= activeStreams.size())
    return;

  StreamInfo &oldInfo = activeStreams[row];
  if (oldInfo.type == "Video") {
    VideoStreamDialog dialog(this);
    dialog.setConfig(oldInfo.videoConfig);
    if (dialog.exec() != QDialog::Accepted)
      return;

    VideoStreamConfig newConfig = dialog.getConfig();

    // Stop old stream
    if (oldInfo.workerProcess) {
      disconnect(oldInfo.workerProcess, nullptr, this, nullptr);
      oldInfo.workerProcess->terminate();
      oldInfo.workerProcess->waitForFinished(2000);
      oldInfo.workerProcess->deleteLater();
      oldInfo.workerProcess = nullptr;
    }

    QString streamName =
        newConfig.customName.isEmpty() ? oldInfo.name : newConfig.customName;
    bool pinned = oldInfo.pinned;
    if (pinned && streamName != oldInfo.name)
      removeSavedStream(oldInfo.name);

    // Launch new worker
    QProcess *worker = new QProcess(this);
    QString workerPath =
        QCoreApplication::applicationDirPath() + "/qt-caster-worker";
    QJsonObject configJson = newConfig.toJson();
    configJson["streamName"] = streamName;
    configJson["type"] = "video";
    QString configStr =
        QJsonDocument(configJson).toJson(QJsonDocument::Compact);
    worker->start(workerPath, QStringList() << configStr);

    connect(worker, &QProcess::readyReadStandardOutput, this,
            &StreamManager::onWorkerOutput);
    connect(worker, &QProcess::readyReadStandardError, this,
            &StreamManager::onWorkerOutput);
    connect(worker,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &StreamManager::onWorkerFinished);

    oldInfo.workerProcess = worker;
    oldInfo.videoConfig = newConfig;
    oldInfo.name = streamName;
    if (pinned)
      saveStreamConfig(oldInfo);

    updateStreamTable();
    updateTrayTooltip();
  } else if (oldInfo.type == "Audio") {
    AudioStreamDialog dialog(this);
    dialog.setConfig(oldInfo.audioConfig);
    if (dialog.exec() != QDialog::Accepted)
      return;

    AudioStreamConfig newConfig = dialog.getConfig();

    if (oldInfo.workerProcess) {
      disconnect(oldInfo.workerProcess, nullptr, this, nullptr);
      oldInfo.workerProcess->terminate();
      oldInfo.workerProcess->waitForFinished(2000);
      oldInfo.workerProcess->deleteLater();
      oldInfo.workerProcess = nullptr;
    }

    QString streamName =
        newConfig.customName.isEmpty() ? oldInfo.name : newConfig.customName;
    bool pinned = oldInfo.pinned;
    if (pinned && streamName != oldInfo.name)
      removeSavedStream(oldInfo.name);

    QProcess *worker = new QProcess(this);
    QString workerPath =
        QCoreApplication::applicationDirPath() + "/qt-caster-worker";
    QJsonObject configJson = newConfig.toJson();
    configJson["streamName"] = streamName;
    configJson["type"] = "audio";
    QString configStr =
        QJsonDocument(configJson).toJson(QJsonDocument::Compact);
    worker->start(workerPath, QStringList() << configStr);

    connect(worker, &QProcess::readyReadStandardOutput, this,
            &StreamManager::onWorkerOutput);
    connect(worker, &QProcess::readyReadStandardError, this,
            &StreamManager::onWorkerOutput);
    connect(worker,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &StreamManager::onWorkerFinished);

    oldInfo.workerProcess = worker;
    oldInfo.audioConfig = newConfig;
    oldInfo.name = streamName;
    if (pinned)
      saveStreamConfig(oldInfo);

    updateStreamTable();
    updateTrayTooltip();
  }
}

void StreamManager::visualizeVideoStream() {
  int row = m_contextMenuRow;
  if (row < 0 || row >= activeStreams.size())
    return;

  StreamInfo &info = activeStreams[row];
  if (info.type != "Video" || info.nodeId.isEmpty()) {
    QMessageBox::warning(this, "Preview Error",
                         "No valid node ID found for this video stream.");
    return;
  }

  // Launch a separate gst-launch-1.0 pipeline that captures the same PipeWire
  // source
  QStringList args;
  args << "pipewiresrc" << QString("path=%1").arg(info.nodeId) << "!"
       << "videoconvert" << "!" << "autovideosink";

  QProcess *previewProc = new QProcess(this);
  previewProc->setProcessChannelMode(QProcess::MergedChannels);
  connect(previewProc, &QProcess::readyRead, [previewProc]() {
    QByteArray data = previewProc->readAll();
    if (!data.isEmpty())
      qDebug().noquote() << "[Preview]" << data.trimmed();
  });
  connect(previewProc,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          [previewProc](int code, QProcess::ExitStatus status) {
            qDebug() << "Preview finished:" << code << status;
            previewProc->deleteLater();
          });
  previewProc->start("gst-launch-1.0", args);
}

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  app.setApplicationName("Qt-Caster");
  app.setApplicationDisplayName("🎬 Qt-Caster Stream Manager");
  app.setOrganizationName("Qt-Caster");

  StreamManager manager;
  manager.show();

  return app.exec();
}

#include "main.moc"