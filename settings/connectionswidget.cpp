#include "connectionswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QNetworkInterface>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "collabmanager.h"
#include "constants.h"
#include "dialogs/confdialog.h"
#include "dialogs/messagedialog.h"
#include "lineeditnum.h"
#include "lineeditscreen.h"
#include "resholder.h"
#include "sharedrunsdialog.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
constexpr int DEFAULT_OPC_PORT{4840};
constexpr int DEFAULT_OPC_PERIOD_MS{500};

QString const COLOR_OK{"#16a34a"};
QString const COLOR_WAIT{"#d97706"};
QString const COLOR_OFF{"#94a3b8"};
QString const COLOR_ERR{"#dc2626"};

QString statusHtml(QString const &color, QString const &text) {
  return QStringLiteral("<span style=\"color:%1; font-size:large;\">●</span>&nbsp;%2").arg(color, text.toHtmlEscaped());
}

QPushButton *makeButton(QString const &text, QWidget *parent) {
  auto *button = new QPushButton(text, parent);
  button->setFixedHeight(deepvaccontrol::UI_BTN_HEIGHT * utils::ssf());
  button->setFocusPolicy(Qt::NoFocus);
  return button;
}

LineEditNum *makePortEdit(QWidget *parent) {
  auto *edit = new LineEditNum(1, 65535, 0, false, parent);
  edit->setFont(parent->font()); // the plain field font, not the large keypad digits
  edit->setFixedSize(110 * utils::ssf(), deepvaccontrol::UI_BTN_HEIGHT * utils::ssf());
  return edit;
}

// a card with its title, like the other settings tabs
QFrame *makeCard(QString const &title, QWidget *parent) {
  double const ssf{utils::ssf()};
  auto        *card = new QFrame(parent);
  card->setObjectName("pageCard");
  auto *lay = new QVBoxLayout(card);
  lay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
  lay->setSpacing(8 * ssf);
  auto *label = new QLabel(title, card);
  label->setObjectName("cardTitle");
  lay->addWidget(label);
  return card;
}

LineEditScreen *makeTextEdit(QWidget *parent) {
  auto *edit = new LineEditScreen(parent, VirtualKeyboard::EN);
  edit->setMinimumWidth(180 * utils::ssf());
  edit->setFixedHeight(deepvaccontrol::UI_BTN_HEIGHT * utils::ssf()); // as tall as the port fields and buttons
  return edit;
}

QLabel *makeInfoLabel(QWidget *parent) {
  auto *label = new QLabel(parent);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setProperty("muted", true);
  return label;
}

// IPv4 addresses other machines can use to reach this computer
QStringList hostAddresses() {
  QStringList result;
  for (QHostAddress const &address : QNetworkInterface::allAddresses()) {
    if ((address.protocol() == QAbstractSocket::IPv4Protocol) && !address.isLoopback()) {
      result.push_back(address.toString());
    }
  }
  return result.isEmpty() ? QStringList{"127.0.0.1"} : result;
}

QString chamberText(QVariantMap const &chamber) {
  return QStringLiteral("%1  —  %2:%3")
    .arg(chamber["name"].toString(), chamber["host"].toString())
    .arg(chamber["port"].toUInt());
}
}

ConnectionsWidget::ConnectionsWidget(ResHolder *resHolder, Page page, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder} {
  m_connectedAddress = m_resHolder->tcpGate ? m_resHolder->tcpGate->serverAddress() : QString();

  auto *content = new QWidget(this);
  auto *columns = new QHBoxLayout(content);
  columns->setContentsMargins(0, 8 * utils::ssf(), 0, 0);
  columns->setSpacing(12 * utils::ssf());

  auto *left  = new QVBoxLayout();
  auto *right = new QVBoxLayout();
  if (page == Page::TCP) {
    left->addWidget(buildChamberBox());
    right->addWidget(buildSharingBox());
  } else {
    left->addWidget(buildOpcBox());
  }
  left->addStretch(1);
  right->addStretch(1);
  columns->addLayout(left, 1);
  if (page == Page::TCP) {
    columns->addLayout(right, 1);
  }

  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(content);
  scroll->viewport()->setAutoFillBackground(false);
  content->setAutoFillBackground(false);
  scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->addWidget(scroll);

  if (page == Page::TCP) {
    connect(m_resHolder->status, &StatusManager::appStateChanged, this, &ConnectionsWidget::refreshChamberStatus);
    if (m_resHolder->tcpGate) {
      // a switch made elsewhere too (the chamber configuration dialog's "Back")
      connect(m_resHolder->tcpGate, &GateTcp::serverChanged, this, [this] {
        m_connectedAddress = m_resHolder->tcpGate->serverAddress();
        refreshChamberList();
        refreshChamberStatus();
      });
    }
    refreshChamberList();
    refreshChamberStatus();
  } else {
    connect(m_resHolder->status, &StatusManager::appStateChanged, this, &ConnectionsWidget::refreshOpcStatus);
    connect(m_resHolder->status, &StatusManager::opcStateChanged, this, &ConnectionsWidget::refreshOpcStatus);
    refreshOpcStatus();
  }
}

// ---------------------------------------------------------------- chamber

QWidget *ConnectionsWidget::buildChamberBox() {
  QFrame *box = makeCard(tr("Chamber connection"), this);
  auto   *lay = static_cast<QVBoxLayout *>(box->layout());

  m_chamberStatus = new QLabel(box);
  m_chamberStatus->setWordWrap(true);
  lay->addWidget(m_chamberStatus);

  m_chamberList = new QListWidget(box);
  m_chamberList->setMinimumHeight(180 * utils::ssf());
  m_chamberList->setStyleSheet(QStringLiteral("QListWidget::item { height: %1px; }").arg(static_cast<int>(34 * utils::ssf())));
  connect(m_chamberList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
    QVariantMap const chamber = item->data(Qt::UserRole).toMap();
    m_chamberName->setText(chamber["name"].toString());
    m_chamberHost->setText(chamber["host"].toString());
    m_chamberPort->setValue(chamber["port"].toUInt());
  });
  lay->addWidget(m_chamberList);

  auto *form = new QGridLayout();
  m_chamberName = makeTextEdit(box);
  m_chamberHost = makeTextEdit(box);
  m_chamberPort = makePortEdit(box);
  m_chamberName->setPlaceholderText(tr("e.g. Chamber 2"));
  m_chamberHost->setPlaceholderText(tr("e.g. 172.0.30.10"));
  m_chamberPort->setValue(m_resHolder->settings->settingsMap()["tcp_server"].toMap()["server_port"].toUInt());
  form->addWidget(new QLabel(tr("Name"), box), 0, 0);
  form->addWidget(m_chamberName, 0, 1, 1, 3);
  form->addWidget(new QLabel(tr("Host"), box), 1, 0);
  form->addWidget(m_chamberHost, 1, 1);
  form->addWidget(new QLabel(tr("Port"), box), 1, 2);
  form->addWidget(m_chamberPort, 1, 3);
  form->setColumnStretch(1, 1);
  lay->addLayout(form);

  auto *buttons   = new QHBoxLayout();
  auto *addBtn    = makeButton(tr("Add"), box);
  auto *saveBtn   = makeButton(tr("Save changes"), box);
  auto *deleteBtn = makeButton(tr("Delete"), box);
  auto *useBtn    = makeButton(tr("Connect"), box);
  useBtn->setObjectName("primaryButton");
  useBtn->setToolTip(tr("Connect to the selected chamber now; it is also used the next time the application starts"));
  connect(addBtn, &QPushButton::clicked, this, [this] {
    saveChamber(true);
  });
  connect(saveBtn, &QPushButton::clicked, this, [this] {
    saveChamber(false);
  });
  connect(deleteBtn, &QPushButton::clicked, this, &ConnectionsWidget::deleteChamber);
  connect(useBtn, &QPushButton::clicked, this, &ConnectionsWidget::connectChamber);
  for (QPushButton *button : {addBtn, saveBtn, deleteBtn, useBtn}) {
    buttons->addWidget(button);
  }
  lay->addLayout(buttons);
  return box;
}

QVariantList ConnectionsWidget::chambers() const {
  QVariantList list = m_resHolder->settings->settingsMap()["chambers"].toList();
  if (list.isEmpty()) {
    // older settings files only know the one configured gate
    QVariantMap const tcpServer = m_resHolder->settings->settingsMap()["tcp_server"].toMap();
    if (!tcpServer["server_ip"].toString().isEmpty()) {
      list.push_back(QVariantMap{{"name", tr("Chamber")},
                                 {"host", tcpServer["server_ip"].toString()},
                                 {"port", tcpServer["server_port"].toUInt()}});
    }
  }
  return list;
}

void ConnectionsWidget::refreshChamberList() {
  QVariantMap const tcpServer = m_resHolder->settings->settingsMap()["tcp_server"].toMap();
  QString const     selected{QStringLiteral("%1:%2").arg(tcpServer["server_ip"].toString()).arg(tcpServer["server_port"].toUInt())};

  m_chamberList->clear();
  for (QVariant const &entry : chambers()) {
    QVariantMap const chamber = entry.toMap();
    QString const     address{QStringLiteral("%1:%2").arg(chamber["host"].toString()).arg(chamber["port"].toUInt())};
    QString           text = chamberText(chamber);
    if (address == m_connectedAddress) {
      text += tr("   (connected)");
    } else if (address == selected) {
      text += tr("   (used at start)");
    }
    auto *item = new QListWidgetItem(text, m_chamberList);
    item->setData(Qt::UserRole, chamber);
    if (address == m_connectedAddress) {
      QFont font = item->font();
      font.setBold(true);
      item->setFont(font);
    }
  }
}

void ConnectionsWidget::refreshChamberStatus() {
  auto const state = m_resHolder->status->getAppState();
  QString    color{COLOR_ERR};
  QString    text;
  switch (state) {
    case StatusManager::AppState::ONLINE:
      color = COLOR_OK;
      text  = tr("Online — %1").arg(m_connectedAddress);
      break;
    case StatusManager::AppState::CONNECTING:
      color = COLOR_WAIT;
      text  = tr("Connecting to %1…").arg(m_connectedAddress);
      break;
    case StatusManager::AppState::CONNECTED:
      color = COLOR_WAIT;
      text  = tr("%1 accepted the connection; waiting for the chamber to answer…").arg(m_connectedAddress);
      break;
    case StatusManager::AppState::OFFLINE:
      text = tr("Connected to %1 but not used: its configuration differs from this application's").arg(m_connectedAddress);
      break;
    case StatusManager::AppState::DUMMY:
    case StatusManager::AppState::WAIT_IN_DEBUG:
      color = COLOR_OFF;
      text  = tr("Working without a chamber (demo / debug)");
      break;
    default:
      text = tr("Not connected — %1").arg(m_connectedAddress);
      break;
  }
  m_chamberStatus->setText(statusHtml(color, text));
}

void ConnectionsWidget::saveChamber(bool asNew) {
  QString const name = m_chamberName->text().trimmed();
  QString const host = m_chamberHost->text().trimmed();
  uint const    port = m_chamberPort->text().toUInt();
  if (name.isEmpty() || QHostAddress(host).isNull() || (0U == port) || (port > 65535U)) {
    message(tr("Chamber"), tr("Enter a name, a valid IPv4 address and a port (1–65535)."));
    return;
  }

  QVariantList list = chambers();
  QVariantMap const chamber{{"name", name}, {"host", host}, {"port", port}};
  if (asNew) {
    list.push_back(chamber);
  } else {
    int const row{m_chamberList->currentRow()};
    if ((row < 0) || (row >= list.size())) {
      message(tr("Chamber"), tr("Select the chamber to change in the list first."));
      return;
    }
    // keep the chamber used at start pointing at the edited one
    QVariantMap const old       = list[row].toMap();
    QVariantMap const tcpServer = m_resHolder->settings->settingsMap()["tcp_server"].toMap();
    if ((old["host"] == tcpServer["server_ip"]) && (old["port"].toUInt() == tcpServer["server_port"].toUInt())) {
      m_resHolder->settings->setTcpServer(host, static_cast<quint16>(port));
    }
    list[row] = chamber;
  }
  m_resHolder->settings->setChambers(list);
  refreshChamberList();
}

void ConnectionsWidget::deleteChamber() {
  int const    row{m_chamberList->currentRow()};
  QVariantList list = chambers();
  if ((row < 0) || (row >= list.size())) {
    return;
  }
  QVariantMap const chamber   = list[row].toMap();
  QVariantMap const tcpServer = m_resHolder->settings->settingsMap()["tcp_server"].toMap();
  if ((chamber["host"] == tcpServer["server_ip"]) && (chamber["port"].toUInt() == tcpServer["server_port"].toUInt())) {
    message(tr("Chamber"), tr("This chamber is used at start. Select another one first."));
    return;
  }
  list.removeAt(row);
  m_resHolder->settings->setChambers(list);
  refreshChamberList();
}

void ConnectionsWidget::connectChamber() {
  QListWidgetItem *item = m_chamberList->currentItem();
  if (!item) {
    message(tr("Chamber"), tr("Select a chamber in the list first."));
    return;
  }
  QVariantMap const chamber = item->data(Qt::UserRole).toMap();
  QString const     host    = chamber["host"].toString();
  auto const        port    = static_cast<quint16>(chamber["port"].toUInt());
  QString const     address{QStringLiteral("%1:%2").arg(host).arg(port)};
  if (address == m_connectedAddress) {
    message(tr("Chamber"), tr("\"%1\" is the chamber of this session already.").arg(chamber["name"].toString()));
    return;
  }
  if (m_resHolder->hard_status && (m_resHolder->hard_status->getHardwareStatus() == HardwareStatus::WORK)) {
    message(tr("Warning"), tr("The current chamber is running a job. Stop it or wait until it finishes before switching."));
    return;
  }

  auto *confirm = new ConfDialog(tr("Connect to \"%1\"?").arg(chamber["name"].toString()),
                                 tr("The connection to %1 is closed and %2 is connected now. "
                                    "It is also the chamber used the next time the application starts.")
                                   .arg(m_connectedAddress, address),
                                 tr("Connect"), tr("Cancel"), this);
  connect(confirm, &QDialog::accepted, this, [this, host, port, address] {
    m_resHolder->settings->setChambers(chambers()); // persist the synthesized default too
    m_resHolder->settings->setTcpServer(host, port);
    m_connectedAddress = address;
    if (m_resHolder->tcpGate) {
      m_resHolder->tcpGate->setServer(QHostAddress(host), port);
    }
    window()->setWindowTitle(QStringLiteral("Deepvac Control (%1)").arg(host));
    refreshChamberList();
    refreshChamberStatus();
  });
  confirm->open();
}

// ---------------------------------------------------------------- opc

QWidget *ConnectionsWidget::buildOpcBox() {
  QVariantMap const opc = m_resHolder->settings->settingsMap()["opc_settings"].toMap();

  QFrame *box = makeCard(tr("OPC UA server"), this);
  auto   *lay = static_cast<QVBoxLayout *>(box->layout());

  m_opcStatus = new QLabel(box);
  m_opcStatus->setWordWrap(true);
  lay->addWidget(m_opcStatus);

  auto *form = new QGridLayout();
  m_opcPort  = makePortEdit(box);
  m_opcPort->setValue(opc.value("port", DEFAULT_OPC_PORT).toUInt());
  m_opcRootName = makeTextEdit(box);
  m_opcRootName->setText(opc.value("root_name", "Testa chamber").toString());
  m_opcRootName->setToolTip(tr("Name of the root object clients browse to"));

  m_opcPeriod = new QComboBox(box);
  m_opcPeriod->setFixedHeight(deepvaccontrol::UI_BTN_HEIGHT * utils::ssf());
  int const period{opc.value("period_ms", DEFAULT_OPC_PERIOD_MS).toInt()};
  QList<int> periods{100, 200, 250, 500, 1000, 2000};
  if (!periods.contains(period)) {
    periods.push_back(period);
    std::sort(periods.begin(), periods.end());
  }
  for (int const ms : periods) {
    m_opcPeriod->addItem(ms < 1000 ? tr("%1 ms").arg(ms) : tr("%1 s").arg(ms / 1000.0), ms);
  }
  m_opcPeriod->setCurrentIndex(m_opcPeriod->findData(period));
  m_opcPeriod->setToolTip(tr("How often the latest chamber values are pushed to the OPC UA variables"));

  form->addWidget(new QLabel(tr("Port"), box), 0, 0);
  form->addWidget(m_opcPort, 0, 1);
  form->addWidget(new QLabel(tr("Update rate"), box), 0, 2);
  form->addWidget(m_opcPeriod, 0, 3);
  form->addWidget(new QLabel(tr("Root name"), box), 1, 0);
  form->addWidget(m_opcRootName, 1, 1, 1, 3);
  form->setColumnStretch(3, 1);
  lay->addLayout(form);


  lay->addWidget(new QLabel(tr("Published variables"), box));
  m_opcTags = new QListWidget(box);
  m_opcTags->setMinimumHeight(160 * utils::ssf());
  QStringList publishedStates;
  for (QVariant const &tag : opc["tags"].toList()) {
    publishedStates.push_back(tag.toMap()["state"].toString());
  }
  // the PLC's job bookkeeping, status words and I/O bit masks are not measurements: not offered
  static QStringList const internal{"temp_pid_idx", "status_condition", "work_type",       "interval",   "intervals_count",
                                    "cycle_cur",    "cycle_total",      "t_total",         "t_interval", "inputs",
                                    "outputs",      "alarm_in_status",  "alarm_out_status"};
  for (QVariant const &state : m_resHolder->settings->settingsMap()["states"].toList()) {
    QVariantMap const map  = state.toMap();
    QString const     name = map["name"].toString();
    if (internal.contains(name)) {
      continue;
    }
    auto *item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(map["label"].toString(), name), m_opcTags);
    item->setData(Qt::UserRole, name);
    item->setData(Qt::UserRole + 1, map["label"].toString());
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(publishedStates.contains(name) ? Qt::Checked : Qt::Unchecked);
  }
  lay->addWidget(m_opcTags);

  auto *buttons  = new QHBoxLayout();
  m_opcStartBtn  = makeButton(QString{}, box);
  auto *applyBtn = makeButton(tr("Apply changes"), box);
  connect(m_opcStartBtn, &QPushButton::clicked, this, [this] {
    bool const enabled = m_resHolder->settings->settingsMap()["app_settings"].toMap()["opc_server"].toBool();
    applyOpc(!enabled);
  });
  connect(applyBtn, &QPushButton::clicked, this, [this] {
    applyOpc(m_resHolder->settings->settingsMap()["app_settings"].toMap()["opc_server"].toBool());
  });
  buttons->addWidget(m_opcStartBtn);
  buttons->addWidget(applyBtn);
  lay->addLayout(buttons);

  m_opcInfo = makeInfoLabel(box);
  lay->addWidget(m_opcInfo);
  return box;
}

QVariantMap ConnectionsWidget::opcSettingsFromForm() const {
  QVariantMap opc = m_resHolder->settings->settingsMap()["opc_settings"].toMap();

  // keep descriptions already written for a tag
  QHash<QString, QVariantMap> previous;
  for (QVariant const &tag : opc["tags"].toList()) {
    previous.insert(tag.toMap()["state"].toString(), tag.toMap());
  }
  QVariantList tags;
  for (int i{0}; i < m_opcTags->count(); ++i) {
    QListWidgetItem const *item = m_opcTags->item(i);
    if (item->checkState() != Qt::Checked) {
      continue;
    }
    QString const state = item->data(Qt::UserRole).toString();
    QVariantMap   tag   = previous.value(state);
    tag["state"]        = state;
    if (tag["tag"].toString().isEmpty()) {
      tag["tag"] = state;
    }
    if (tag["description"].toString().isEmpty()) {
      tag["description"] = item->data(Qt::UserRole + 1).toString();
    }
    tags.push_back(tag);
  }

  opc["port"]      = m_opcPort->text().toUInt();
  opc["root_name"] = m_opcRootName->text().trimmed();
  opc["period_ms"] = m_opcPeriod->currentData().toInt();
  opc["tags"]      = tags;
  return opc;
}

void ConnectionsWidget::applyOpc(bool enabled) {
  QVariantMap const opc = opcSettingsFromForm();
  uint const        port{opc["port"].toUInt()};
  if ((0U == port) || (port > 65535U) || opc["root_name"].toString().isEmpty()) {
    message(tr("OPC UA server"), tr("Enter a port (1–65535) and a root name."));
    return;
  }
  if (enabled && opc["tags"].toList().isEmpty()) {
    message(tr("OPC UA server"), tr("Select at least one variable to publish."));
    return;
  }
  // App restarts the server from these settings (see App::restartOpc)
  m_resHolder->settings->setOpcSettings(enabled, opc);
  refreshOpcStatus();
}

void ConnectionsWidget::refreshOpcStatus() {
  bool const enabled = m_resHolder->settings->settingsMap()["app_settings"].toMap()["opc_server"].toBool();
  bool const online  = StatusManager::AppState::ONLINE == m_resHolder->status->getAppState();
  int const  state{m_resHolder->status->getOpcStatus()};
  uint const port{m_resHolder->settings->settingsMap()["opc_settings"].toMap()["port"].toUInt()};

  m_opcStartBtn->setText(enabled ? tr("Stop server") : tr("Start server"));

  if (!enabled) {
    m_opcStatus->setText(statusHtml(COLOR_OFF, tr("Server stopped")));
  } else if (!online) {
    m_opcStatus->setText(statusHtml(COLOR_WAIT, tr("Starts when the chamber is online")));
  } else if (0 == state) {
    m_opcStatus->setText(statusHtml(COLOR_OK, tr("Broadcasting on port %1").arg(port)));
  } else if (-2 == state) {
    m_opcStatus->setText(statusHtml(COLOR_WAIT, tr("Starting…")));
  } else {
    m_opcStatus->setText(statusHtml(COLOR_ERR, tr("Server error (code %1) — is port %2 free?").arg(state).arg(port)));
  }

  QStringList   endpoints;
  for (QString const &host : hostAddresses()) {
    endpoints.push_back(QStringLiteral("opc.tcp://%1:%2").arg(host).arg(port));
  }
  m_opcInfo->setText(tr("Endpoint: %1").arg(endpoints.join(", ")));
}

// ---------------------------------------------------------------- local network sharing

QWidget *ConnectionsWidget::buildSharingBox() {
  auto *cards = new QWidget(this);
  auto *cardsLay = new QVBoxLayout(cards);
  cardsLay->setContentsMargins(0, 0, 0, 0);
  cardsLay->setSpacing(12 * utils::ssf());
  QFrame *box = makeCard(tr("Host data transfer"), cards);
  auto   *lay = static_cast<QVBoxLayout *>(box->layout());
  cardsLay->addWidget(box);

  CollabManager *collab = m_resHolder->collab;
  if (!collab || !collab->available()) {
    auto *info = makeInfoLabel(box);
    info->setText(tr("Not available in this build (OpenSSL is missing)."));
    lay->addWidget(info);
    return cards;
  }

  // host
  m_hostStatus = new QLabel(box);
  m_hostStatus->setWordWrap(true);
  m_hostStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
  lay->addWidget(m_hostStatus);

  auto *hostRow = new QHBoxLayout();
  m_hostPort    = makePortEdit(box);
  m_hostPort->setValue(m_resHolder->settings->settingsMap()["collab"].toMap().value("host_port", 8765).toUInt());
  m_hostBtn     = makeButton(QString{}, box);
  m_joinCodeBtn = makeButton(tr("New join code"), box);
  hostRow->addWidget(new QLabel(tr("Port"), box));
  hostRow->addWidget(m_hostPort);
  hostRow->addWidget(m_hostBtn);
  hostRow->addWidget(m_joinCodeBtn);
  hostRow->addStretch();
  lay->addLayout(hostRow);

  m_joinCodeLabel = new QLabel(box);
  m_joinCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  lay->addWidget(m_joinCodeLabel);

  auto *peersRow = new QHBoxLayout();
  m_peers        = new QListWidget(box);
  m_peers->setMaximumHeight(80 * utils::ssf());
  auto *removePeerBtn = makeButton(tr("Remove device"), box);
  peersRow->addWidget(m_peers, 1);
  peersRow->addWidget(removePeerBtn, 0, Qt::AlignTop);
  lay->addLayout(peersRow);

  connect(m_hostBtn, &QPushButton::clicked, this, [this, collab] {
    if (collab->isHosting()) {
      collab->stopHost();
      return;
    }
    QString error;
    if (!collab->startHost(static_cast<quint16>(m_hostPort->text().toUInt()), &error)) {
      message(tr("Host data transfer"), tr("Could not start hosting: %1").arg(error));
      return;
    }
    collab->connectToOwnHost(); // the host's own data takes part too
    refreshSharing();
  });
  connect(m_joinCodeBtn, &QPushButton::clicked, this, [this, collab] {
    QString const code = collab->newJoinCode();
    m_joinCodeLabel->setText(tr("Join code: <span style=\"font-size:x-large; font-weight:bold;\">%1</span> "
                                "(valid 10 minutes, one device)")
                               .arg(code));
  });
  connect(removePeerBtn, &QPushButton::clicked, this, [this, collab] {
    if (QListWidgetItem *item = m_peers->currentItem()) {
      collab->removePeer(item->data(Qt::UserRole).toString());
    }
  });

  // client
  box = makeCard(tr("Connect for data transfer"), cards);
  lay = static_cast<QVBoxLayout *>(box->layout());
  cardsLay->addWidget(box);
  m_clientStatus = new QLabel(box);
  m_clientStatus->setWordWrap(true);
  lay->addWidget(m_clientStatus);

  auto *joinRow = new QGridLayout();
  m_hostAddress = makeTextEdit(box);
  m_hostAddress->setPlaceholderText(tr("host address, e.g. 192.168.1.20:8765"));
  m_joinCode = makeTextEdit(box);
  m_joinCode->setPlaceholderText(tr("join code"));
  auto *checkBtn = makeButton(tr("Check host"), box);
  m_pairBtn      = makeButton(tr("Pair"), box);
  m_pairBtn->setEnabled(false);
  joinRow->addWidget(m_hostAddress, 0, 0);
  joinRow->addWidget(checkBtn, 0, 1);
  joinRow->addWidget(m_joinCode, 1, 0);
  joinRow->addWidget(m_pairBtn, 1, 1);
  joinRow->setColumnStretch(0, 1);
  lay->addLayout(joinRow);
  m_fingerprintLabel = makeInfoLabel(box);
  lay->addWidget(m_fingerprintLabel);

  auto *syncRow   = new QHBoxLayout();
  m_syncBtn       = makeButton(tr("Sync now"), box);
  auto *sharedBtn = makeButton(tr("Shared experiments…"), box);
  m_disconnectBtn = makeButton(tr("Disconnect"), box);
  m_autoSync      = new QCheckBox(tr("Every 5 min"), box);
  m_autoSync->setChecked(collab->autoSync());
  syncRow->addWidget(m_syncBtn);
  syncRow->addWidget(m_autoSync);
  syncRow->addWidget(sharedBtn);
  syncRow->addWidget(m_disconnectBtn);
  lay->addLayout(syncRow);

  connect(checkBtn, &QPushButton::clicked, this, [this, collab] {
    m_fingerprintLabel->setText(tr("Contacting the host…"));
    collab->fetchCertificate(m_hostAddress->text());
  });
  connect(collab, &CollabManager::certificateFetched, this,
          [this](QString const &url, QByteArray const &pem, QString const &fingerprint, QString const &error) {
            m_pendingUrl  = url;
            m_pendingCert = pem;
            m_pairBtn->setEnabled(!pem.isEmpty());
            Q_UNUSED(fingerprint);
            m_fingerprintLabel->setText(pem.isEmpty() ? error : tr("Host found: enter its join code and pair."));
          });
  connect(m_pairBtn, &QPushButton::clicked, this, [this, collab] {
    collab->pair(m_pendingUrl, m_joinCode->text(), m_pendingCert);
  });
  connect(collab, &CollabManager::paired, this, [this](bool ok, QString const &text) {
    message(tr("Connect for data transfer"), text);
    if (ok) {
      m_joinCode->clear();
      m_fingerprintLabel->clear();
      m_pairBtn->setEnabled(false);
    }
  });
  connect(m_syncBtn, &QPushButton::clicked, collab, &CollabManager::syncNow);
  connect(m_autoSync, &QCheckBox::toggled, collab, &CollabManager::setAutoSync);
  connect(m_disconnectBtn, &QPushButton::clicked, collab, &CollabManager::disconnectFromHost);
  connect(sharedBtn, &QPushButton::clicked, this, [this, collab] {
    auto *dialog = new SharedRunsDialog(collab, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
  });
  connect(collab, &CollabManager::stateChanged, this, &ConnectionsWidget::refreshSharing);
  connect(collab, &CollabManager::syncFinished, this, &ConnectionsWidget::refreshSharing);

  refreshSharing();
  return cards;
}

void ConnectionsWidget::refreshSharing() {
  CollabManager *collab = m_resHolder->collab;
  if (!collab || !m_hostStatus) {
    return;
  }

  bool const hosting{collab->isHosting()};
  m_hostBtn->setText(hosting ? tr("Stop hosting") : tr("Start hosting"));
  m_joinCodeBtn->setEnabled(hosting);
  m_hostPort->setEnabled(!hosting);
  if (hosting) {
    QStringList urls;
    for (QString const &host : hostAddresses()) {
      urls.push_back(QStringLiteral("%1:%2").arg(host).arg(collab->hostPort()));
    }
    m_hostStatus->setText(statusHtml(COLOR_OK, tr("Hosting at %1").arg(urls.join(", "))));
  } else {
    m_hostStatus->setText(statusHtml(COLOR_OFF, tr("Not hosting")));
    m_joinCodeLabel->clear();
  }

  m_peers->clear();
  for (collab::Peer const &peer : collab->peers()) {
    auto *item = new QListWidgetItem(tr("%1  (paired %2)").arg(peer.displayName, peer.pairedAt.left(10)), m_peers);
    item->setData(Qt::UserRole, peer.keyHash);
  }

  bool const connected{collab->isConnected()};
  QString    text = connected ? tr("Connected to %1 (%2)").arg(collab->connection().hostName, collab->connection().url)
                              : tr("Not connected.");
  if (collab->isBusy()) {
    text += tr(" — working…");
  } else if (!collab->lastSyncSummary().isEmpty()) {
    text += "\n" + tr("Last sync %1").arg(collab->lastSyncSummary());
  }
  m_clientStatus->setText(text);
  m_syncBtn->setEnabled(connected && !collab->isBusy());
  m_disconnectBtn->setEnabled(connected);
  m_autoSync->setEnabled(connected);
}

void ConnectionsWidget::message(QString const &title, QString const &text) const {
  (new MessageDialog(title, text))->open();
}

}
