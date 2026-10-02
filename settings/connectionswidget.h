#ifndef CONNECTIONSWIDGET_H
#define CONNECTIONSWIDGET_H

#include <QWidget>

#include "statusmanager.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace deepvaccontrol {

class LineEditNum;
class LineEditScreen;
struct ResHolder;

/**
 * @brief Settings tab with the app's network links (app-only, not sent to the PLC):
 * - chamber (PLC gate) connection with a list of saved chambers;
 * - OPC UA server broadcasting the chamber's live values.
 */
class ConnectionsWidget : public QWidget {
  Q_OBJECT
public:
  explicit ConnectionsWidget(ResHolder *resHolder, QWidget *parent = nullptr);

private:
  QWidget *buildChamberBox();
  QWidget *buildOpcBox();
  QWidget *buildSharingBox();

  // chamber
  void         refreshChamberList();
  void         refreshChamberStatus();
  void         saveChamber(bool asNew);
  void         deleteChamber();
  void         useChamber();
  QVariantList chambers() const;

  // opc
  void        refreshOpcStatus();
  void        applyOpc(bool enabled);
  QVariantMap opcSettingsFromForm() const;

  // local network sharing
  void refreshSharing();

  void message(QString const &title, QString const &text) const;

  ResHolder *m_resHolder;

  QLabel         *m_chamberStatus{nullptr};
  QListWidget    *m_chamberList{nullptr};
  LineEditScreen *m_chamberName{nullptr};
  LineEditScreen *m_chamberHost{nullptr};
  LineEditNum    *m_chamberPort{nullptr};
  QString         m_connectedAddress; // address the running app uses (changes apply on restart)

  QLabel         *m_opcStatus{nullptr};
  LineEditNum    *m_opcPort{nullptr};
  LineEditScreen *m_opcRootName{nullptr};
  QComboBox      *m_opcPeriod{nullptr};
  QListWidget    *m_opcTags{nullptr};
  QPushButton    *m_opcStartBtn{nullptr};
  QLabel         *m_opcInfo{nullptr};

  QLabel         *m_hostStatus{nullptr};
  LineEditNum    *m_hostPort{nullptr};
  QPushButton    *m_hostBtn{nullptr};
  QPushButton    *m_joinCodeBtn{nullptr};
  QPushButton    *m_ownHostBtn{nullptr};
  QLabel         *m_joinCodeLabel{nullptr};
  QListWidget    *m_peers{nullptr};
  QLabel         *m_clientStatus{nullptr};
  LineEditScreen *m_hostAddress{nullptr};
  LineEditScreen *m_joinCode{nullptr};
  QLabel         *m_fingerprintLabel{nullptr};
  QPushButton    *m_pairBtn{nullptr};
  QPushButton    *m_syncBtn{nullptr};
  QPushButton    *m_disconnectBtn{nullptr};
  QCheckBox      *m_autoSync{nullptr};
  QString         m_pendingUrl;
  QByteArray      m_pendingCert;
};

} // namespace deepvaccontrol

#endif // CONNECTIONSWIDGET_H
