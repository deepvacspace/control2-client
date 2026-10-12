
#include <QApplication>
#include <QLocale>
#include <QStyleFactory>
#include <QTranslator>

#include "app.h"
#include "uilanguage.h"
#include "theme.h"
#ifdef DEEPVACCONTROL_HAVE_OPENSSL
#include <openssl/crypto.h>
#endif
#include "utils.h"

static void setupLanguage(QApplication &app, deepvaccontrol::UiLanguage lang, QTranslator &translator) {
  switch (lang) {
    case deepvaccontrol::UiLanguage::Russian: {
      qDebug() << "Set UiLanguage::Russian";
      // the texts in the code are English; res/language_ru.ts holds their Russian
      if (translator.load(":/language_ru.qm")) {
        app.installTranslator(&translator);
      }
      // Set global locale for entire app
      QLocale::setDefault(QLocale(QLocale::Russian, QLocale::Russia));
      break;
    }
    case deepvaccontrol::UiLanguage::English:
    default: {
      qDebug() << "Set UiLanguage::English";
      QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates)); // the texts in the code
      break;
    }
  }
}

int main(int argc, char *argv[]) {
#ifdef DEEPVACCONTROL_HAVE_OPENSSL
  // OpenSSL's own atexit clean-up races with the unloading of Qt's OpenSSL TLS plugin and crashed the
  // process on exit now and then; the operating system frees everything at exit anyway
  OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, nullptr);
#endif
  QApplication a(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Deepvac Control"));
#ifdef DEEPVACCONTROL_VERSION
  QApplication::setApplicationVersion(QStringLiteral(DEEPVACCONTROL_VERSION));
#endif
  // Keep the display name empty so the platform plugin does not append
  // " - Deepvac Control" to the title that MainWindow already provides.
  QApplication::setApplicationDisplayName(QString());
  QApplication::setOrganizationName(QStringLiteral("Deepvac"));

  // Options parser
  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Deepvac Control"));
  parser.addHelpOption();

  QCommandLineOption windowModeOption("w", "Enable windowed mode");
  QCommandLineOption debugOption("debug", "Enable debug output");

  parser.addOption(windowModeOption);
  parser.addOption(debugOption);
  parser.process(a);

  bool       windowedMode{parser.isSet(windowModeOption)};
  bool const debugMode{parser.isSet(debugOption)};

#ifndef NDEBUG // debug
  // windowedMode = true;
#endif

  // Style and sizes
  a.setStyle(QStyleFactory::create("fusion"));
  deepvaccontrol::theme::apply(false); // light design by default (System -> OS switches to dark)

  if (!windowedMode) {
    auto        *screen = a.primaryScreen();
    double const ssf{qMax(1.0, static_cast<double>(screen->size().width()) / 1024.0)};

    deepvaccontrol::utils::setSsf(ssf);

    auto font     = a.font();
    auto fontSize = font.pointSize();
    fontSize *= ssf;
    font.setPointSize(fontSize);
    a.setFont(font);
  }

  // Control application
  deepvaccontrol::App m_app(windowedMode, debugMode);

  // Translator must live until the end of the application
  QTranslator translator;
  setupLanguage(a, m_app.uiLanguage(), translator);

  m_app.runtimeSetups();
  m_app.start();
  return a.exec();
}
