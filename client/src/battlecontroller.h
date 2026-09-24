#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

#include "ad/core/ai/tactical.hpp"
#include "ad/core/battle.hpp"
#include "ad/core/campaign.hpp"
#include "boardmodel.h"

namespace ad::client {

/// Owns the current board battle (docs/GAME_DESIGN.md §8) and is the only
/// path from QML into it. The tactical AI thinks on a QtConcurrent worker
/// over a private copy of the battle; its action is applied on the main
/// thread and animated before the next one is requested. Reached from QML
/// as GameController.battle.
class BattleController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by GameController")

  Q_PROPERTY(bool active READ active NOTIFY battleChanged)
  Q_PROPERTY(ad::client::BoardModel* units READ units CONSTANT)
  Q_PROPERTY(int boardLength READ boardLength NOTIFY battleChanged)
  Q_PROPERTY(int boardWidth READ boardWidth NOTIFY battleChanged)
  Q_PROPERTY(int deployColumns READ deployColumns CONSTANT)
  Q_PROPERTY(bool attackerHuman READ attackerHuman NOTIFY battleChanged)
  Q_PROPERTY(bool defenderHuman READ defenderHuman NOTIFY battleChanged)
  Q_PROPERTY(QVariantMap attacker READ attacker NOTIFY battleChanged)
  Q_PROPERTY(QVariantMap defender READ defender NOTIFY battleChanged)
  Q_PROPERTY(QString cityName READ cityName NOTIFY battleChanged)
  Q_PROPERTY(int phase READ phase NOTIFY stateChanged)
  Q_PROPERTY(QString phaseKey READ phaseKey NOTIFY stateChanged)
  Q_PROPERTY(int sideToAct READ sideToAct NOTIFY stateChanged)
  /// The side the screen is laid out for (the human's; in hotseat, the side to act).
  Q_PROPERTY(int viewSide READ viewSide NOTIFY stateChanged)
  Q_PROPERTY(bool myTurn READ myTurn NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(bool resolving READ resolving NOTIFY busyChanged)
  Q_PROPERTY(int actionsLeft READ actionsLeft NOTIFY stateChanged)
  Q_PROPERTY(int turn READ turn NOTIFY stateChanged)
  Q_PROPERTY(int quietTurns READ quietTurns NOTIFY stateChanged)
  Q_PROPERTY(int selectedX READ selectedX NOTIFY selectionChanged)
  Q_PROPERTY(int selectedY READ selectedY NOTIFY selectionChanged)
  Q_PROPERTY(QVariantList highlights READ highlights NOTIFY selectionChanged)
  Q_PROPERTY(bool drawOfferedByMe READ drawOfferedByMe NOTIFY stateChanged)
  Q_PROPERTY(bool drawOfferedByEnemy READ drawOfferedByEnemy NOTIFY stateChanged)
  Q_PROPERTY(bool over READ over NOTIFY stateChanged)
  Q_PROPERTY(QString resultKey READ resultKey NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap result READ result NOTIFY stateChanged)
  Q_PROPERTY(QString status READ status NOTIFY stateChanged)
  Q_PROPERTY(bool canReady READ canReady NOTIFY stateChanged)
  // live per-side counts, for the header
  Q_PROPERTY(int attackerUnits READ attackerUnits NOTIFY stateChanged)
  Q_PROPERTY(int defenderUnits READ defenderUnits NOTIFY stateChanged)
  Q_PROPERTY(int attackerGenerals READ attackerGenerals NOTIFY stateChanged)
  Q_PROPERTY(int defenderGenerals READ defenderGenerals NOTIFY stateChanged)
  Q_PROPERTY(int attackerPower READ attackerPower NOTIFY stateChanged)
  Q_PROPERTY(int defenderPower READ defenderPower NOTIFY stateChanged)

public:
  enum Highlight { NoHighlight = 0, MoveTarget = 1, StrikeTarget = 2, Selected = 3 };
  Q_ENUM(Highlight)

  explicit BattleController(QObject* parent = nullptr);
  ~BattleController() override;

  /// Set up the battle the campaign asked for and start the AI sides.
  void begin(const ad::core::Campaign& campaign, const ad::core::PendingBattle& pb, bool attackerHuman,
             bool defenderHuman, ad::core::SearchBudget aiBudget);
  /// The online form (docs/PROTOCOL.md §5): `human[s]` is played here,
  /// `remote[s]` is driven by relayed commands, the rest by the local AI
  /// (whose commands are then relayed out). `viewSide` forces the screen's
  /// side (-1: as usual).
  void begin(const ad::core::Campaign& campaign, const ad::core::PendingBattle& pb, const bool human[2],
             const bool remote[2], int viewSide, ad::core::SearchBudget aiBudget);
  /// Drop the battle (after finished()).
  void end();
  [[nodiscard]] std::optional<ad::core::BattleOutcome> outcome() const;

  // -- the relay (GameController) --------------------------------------------
  /// Apply one command that arrived from the other client; animated like
  /// a local one, never re-emitted through commandApplied().
  void applyRemote(const ad::core::BattleCommand& cmd);
  /// The other client quit before Play: `side` concedes, everyone survives.
  void remoteRetreat(int side);
  /// While a log is replayed no AI runs and nothing animates; turning it
  /// off kicks the AI for whatever side is due.
  void setReplaying(bool on);
  [[nodiscard]] bool isRemote(int side) const { return side >= 0 && side < 2 && remote_[side]; }
  /// quit() before Play: which side withdrew (-1 when the battle was played out).
  [[nodiscard]] int retreatedSide() const { return retreated_ ? static_cast<int>(retreatSide_) : -1; }

  [[nodiscard]] bool active() const { return battle_.has_value(); }
  [[nodiscard]] BoardModel* units() const { return units_; }
  [[nodiscard]] int boardLength() const;
  [[nodiscard]] int boardWidth() const;
  [[nodiscard]] int deployColumns() const;
  [[nodiscard]] bool attackerHuman() const { return human_[0]; }
  [[nodiscard]] bool defenderHuman() const { return human_[1]; }
  [[nodiscard]] QVariantMap attacker() const { return parties_[0]; }
  [[nodiscard]] QVariantMap defender() const { return parties_[1]; }
  [[nodiscard]] QString cityName() const { return cityName_; }
  [[nodiscard]] int phase() const;
  [[nodiscard]] QString phaseKey() const;
  [[nodiscard]] int sideToAct() const;
  [[nodiscard]] int viewSide() const;
  [[nodiscard]] bool myTurn() const;
  [[nodiscard]] bool busy() const { return thinking_ || resolving_; }
  [[nodiscard]] bool resolving() const { return resolving_; }
  [[nodiscard]] int actionsLeft() const;
  [[nodiscard]] int turn() const;
  [[nodiscard]] int quietTurns() const;
  [[nodiscard]] int selectedX() const { return selected_ ? selected_->x : -1; }
  [[nodiscard]] int selectedY() const { return selected_ ? selected_->y : -1; }
  [[nodiscard]] QVariantList highlights() const { return highlights_; }
  [[nodiscard]] bool drawOfferedByMe() const;
  [[nodiscard]] bool drawOfferedByEnemy() const;
  [[nodiscard]] bool over() const;
  [[nodiscard]] QString resultKey() const;
  [[nodiscard]] QVariantMap result() const { return result_; }
  [[nodiscard]] QString status() const;

  [[nodiscard]] int attackerUnits() const { return livingUnits(0); }
  [[nodiscard]] int defenderUnits() const { return livingUnits(1); }
  [[nodiscard]] int attackerGenerals() const { return livingGenerals(0); }
  [[nodiscard]] int defenderGenerals() const { return livingGenerals(1); }
  [[nodiscard]] int attackerPower() const { return power(0); }
  [[nodiscard]] int defenderPower() const { return power(1); }

  Q_INVOKABLE int generalsRequired(int side) const;
  Q_INVOKABLE int generalsPromoted(int side) const;
  Q_INVOKABLE int livingGenerals(int side) const;
  Q_INVOKABLE int livingUnits(int side) const;
  Q_INVOKABLE int power(int side) const;
  Q_INVOKABLE bool isReady(int side) const;
  Q_INVOKABLE bool isHuman(int side) const;
  /// True when the Ready button applies to the view side right now.
  [[nodiscard]] bool canReady() const;
  Q_INVOKABLE QVariantMap unitAt(int x, int y) const;
  Q_INVOKABLE bool inZone(int side, int x, int y) const;

  Q_INVOKABLE void cellClicked(int x, int y);
  Q_INVOKABLE void clearSelection();
  Q_INVOKABLE void ready();
  Q_INVOKABLE void endTurn();
  Q_INVOKABLE void offerDraw();
  Q_INVOKABLE void surrender();
  /// Concede the battle as it stands (the quit confirmation), then leave.
  Q_INVOKABLE void quit();
  /// Let the engine play the rest for both sides.
  Q_INVOKABLE void autoResolve();
  /// The result overlay's Continue: hand the outcome to the campaign.
  Q_INVOKABLE void leave();

signals:
  void battleChanged();
  void stateChanged();
  void selectionChanged();
  void busyChanged();
  /// One applied action, for the animations: {kind, side, fromX, fromY,
  /// toX, toY, typeKey, victimTypeKey, victimGeneral, victimIndex}.
  void actionPerformed(const QVariantMap& action);
  void notice(const QString& text);
  /// The battle is over and the player left the page: outcome() is set.
  void finished();
  /// Every command applied on this client - the human's and the local
  /// AI's, never a relayed one - for the online relay.
  void commandApplied(const ad::core::BattleCommand& cmd);

private:
  [[nodiscard]] ad::core::Side sideOf(int s) const { return s == 0 ? ad::core::Side::Attacker : ad::core::Side::Defender; }
  [[nodiscard]] bool humanSide(ad::core::Side s) const { return human_[static_cast<int>(s)]; }
  [[nodiscard]] bool aiSide(ad::core::Side s) const { return !human_[static_cast<int>(s)] && !remote_[static_cast<int>(s)]; }
  [[nodiscard]] ad::core::Side humanFor(ad::core::Cell c) const;
  /// The one door into Battle::apply: a success is announced through
  /// commandApplied() unless it came from the relay.
  bool applyCmd(const ad::core::BattleCommand& cmd, bool fromRemote = false);
  void applyHuman(const ad::core::BattleCommand& cmd, const QVariantMap& action = {});
  void retreat(ad::core::Side side);
  void afterChange();
  void updateHighlights();
  void kickAi();
  void aiAct();
  void applyAiAction(const ad::core::BattleAction& a);
  QVariantMap actionMap(const char* kind, ad::core::Side side, ad::core::Cell from, ad::core::Cell to) const;
  void finishBattle();

  std::optional<ad::core::Battle> battle_;
  BoardModel* units_{nullptr};
  bool human_[2]{false, false};
  bool remote_[2]{false, false};
  int viewSide_{-1};
  bool replaying_{false};
  bool retreated_{false};
  ad::core::Side retreatSide_{ad::core::Side::Attacker};
  QVariantMap parties_[2];
  QString cityName_;
  ad::core::SearchBudget budget_{ad::core::SearchBudget::Normal};
  std::optional<ad::core::Cell> selected_;
  QVariantList highlights_;
  QVariantMap result_;
  std::optional<ad::core::BattleOutcome> outcome_;
  bool thinking_{false};
  bool resolving_{false};
  bool aiOfferedDraw_[2]{false, false};
  int generation_{0};
  QTimer aiTimer_;
  QFutureWatcher<ad::core::SearchResult>* searchWatcher_{nullptr};
  QFutureWatcher<ad::core::Battle>* resolveWatcher_{nullptr};
  std::vector<ad::core::UnitTypeId> initialTypes_;  // for the casualty summary
};

} // namespace ad::client
