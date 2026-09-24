#include "devdrive.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QKeySequence>
#include <QMetaObject>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QSocketNotifier>
#include <QStringList>
#include <QTimer>
#include <QWheelEvent>

#include <iostream>
#include <string>

namespace ad::client {
namespace {

QQuickWindow* driveWindow(QQmlApplicationEngine* engine) {
  for (QObject* root : engine->rootObjects())
    if (auto* win = qobject_cast<QQuickWindow*>(root)) return win;
  return nullptr;
}

void reply(const QString& line) { std::cout << line.toStdString() << std::endl; }

void postClick(QQuickWindow* win, const QPointF& pos, Qt::MouseButton button) {
  const QPointF global = win->mapToGlobal(pos);
  QCoreApplication::postEvent(win, new QMouseEvent(QEvent::MouseMove, pos, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier));
  QCoreApplication::postEvent(win, new QMouseEvent(QEvent::MouseButtonPress, pos, global, button, button, Qt::NoModifier));
  QTimer::singleShot(60, win, [win, pos, global, button] {
    QCoreApplication::postEvent(win, new QMouseEvent(QEvent::MouseButtonRelease, pos, global, button, Qt::NoButton, Qt::NoModifier));
  });
}

void runCommand(QQmlApplicationEngine* engine, const QString& line) {
  const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (parts.isEmpty()) return;
  const QString& cmd = parts[0];
  if (cmd == QLatin1String("quit")) {
    reply(QStringLiteral("ok quit"));
    QCoreApplication::quit();
    return;
  }
  if (cmd == QLatin1String("wait") && parts.size() == 2) {
    QTimer::singleShot(parts[1].toInt(), [] { reply(QStringLiteral("ok wait")); });
    return;
  }
  QQuickWindow* win = driveWindow(engine);
  if (!win) {
    reply(QStringLiteral("err no window"));
    return;
  }
  if (cmd == QLatin1String("activate")) {
    // Without a window manager (Xvfb) nothing activates the window, and an
    // inactive window has no active focus item to give key events to.
    win->requestActivate();
    reply(win->isActive() ? QStringLiteral("ok activate") : QStringLiteral("ok activate requested"));
  } else if (cmd == QLatin1String("shot") && parts.size() == 2) {
    const QImage img = win->grabWindow();
    if (!img.isNull() && img.save(parts[1]))
      reply(QStringLiteral("ok shot %1 %2x%3").arg(parts[1]).arg(img.width()).arg(img.height()));
    else
      reply(QStringLiteral("err shot %1").arg(parts[1]));
  } else if ((cmd == QLatin1String("click") || cmd == QLatin1String("rclick")) && parts.size() == 3) {
    postClick(win, QPointF(parts[1].toDouble(), parts[2].toDouble()),
              cmd == QLatin1String("click") ? Qt::LeftButton : Qt::RightButton);
    reply(QStringLiteral("ok %1 %2 %3").arg(cmd, parts[1], parts[2]));
  } else if (cmd == QLatin1String("move") && parts.size() == 3) {
    const QPointF pos(parts[1].toDouble(), parts[2].toDouble());
    QCoreApplication::postEvent(win, new QMouseEvent(QEvent::MouseMove, pos, win->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier));
    reply(QStringLiteral("ok move"));
  } else if (cmd == QLatin1String("wheel") && parts.size() == 4) {
    const QPointF pos(parts[1].toDouble(), parts[2].toDouble());
    const int delta = parts[3].toInt();
    QCoreApplication::postEvent(win, new QWheelEvent(pos, win->mapToGlobal(pos), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false));
    reply(QStringLiteral("ok wheel"));
  } else if (cmd == QLatin1String("size") && parts.size() == 3) {
    win->resize(parts[1].toInt(), parts[2].toInt());
    reply(QStringLiteral("ok size"));
  } else if (cmd == QLatin1String("float") && parts.size() == 5) {
    win->setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::BypassWindowManagerHint);
    win->setGeometry(parts[3].toInt(), parts[4].toInt(), parts[1].toInt(), parts[2].toInt());
    win->show();
    reply(QStringLiteral("ok float"));
  } else if (cmd == QLatin1String("type") && parts.size() >= 2) {
    if (!win->isActive()) win->requestActivate();
    const QString text = line.mid(line.indexOf(QLatin1Char(' ')) + 1);
    QCoreApplication::postEvent(win, new QKeyEvent(QEvent::KeyPress, 0, Qt::NoModifier, text));
    QCoreApplication::postEvent(win, new QKeyEvent(QEvent::KeyRelease, 0, Qt::NoModifier, text));
    reply(QStringLiteral("ok type"));
  } else if (cmd == QLatin1String("key") && parts.size() == 2) {
    const QKeySequence seq(parts[1]);
    if (seq.isEmpty()) {
      reply(QStringLiteral("err key %1").arg(parts[1]));
      return;
    }
    if (!win->isActive()) win->requestActivate();
    const int key = seq[0].key();
    const QString text = parts[1].size() == 1 ? parts[1] : QString();
    QCoreApplication::postEvent(win, new QKeyEvent(QEvent::KeyPress, key, seq[0].keyboardModifiers(), text));
    QCoreApplication::postEvent(win, new QKeyEvent(QEvent::KeyRelease, key, seq[0].keyboardModifiers(), text));
    reply(QStringLiteral("ok key %1").arg(parts[1]));
  } else if (cmd == QLatin1String("page") && parts.size() == 2) {
    QVariant ret;
    const bool ok = QMetaObject::invokeMethod(win, "devPage", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, parts[1]));
    reply(ok ? QStringLiteral("ok page %1").arg(parts[1]) : QStringLiteral("err page %1").arg(parts[1]));
  } else if (cmd == QLatin1String("eval") && parts.size() >= 2) {
    const QString js = line.mid(line.indexOf(QLatin1Char(' ')) + 1);
    QQmlExpression expr(engine->rootContext(), win, js);
    const QVariant v = expr.evaluate();
    reply(expr.hasError() ? QStringLiteral("err eval %1").arg(expr.error().toString())
                          : QStringLiteral("ok eval %1").arg(v.toString()));
  } else {
    reply(QStringLiteral("err unknown %1").arg(line));
  }
}

} // namespace

void installDevDrive(QQmlApplicationEngine& engine) {
  auto* notifier = new QSocketNotifier(0, QSocketNotifier::Read, QCoreApplication::instance());
  QObject::connect(notifier, &QSocketNotifier::activated, [engine = &engine, notifier] {
    std::string line;
    if (!std::getline(std::cin, line)) {
      notifier->setEnabled(false);
      return;
    }
    runCommand(engine, QString::fromStdString(line));
  });
}

} // namespace ad::client
