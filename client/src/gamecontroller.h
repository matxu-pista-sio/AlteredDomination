#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>
#include <vector>

#include <QColor>
#include <QElapsedTimer>
#include <QHash>

#include "ad/core/ai/strategic.hpp"
#include "ad/core/campaign.hpp"
#include "ad/core/catalog.hpp"
#include "ad/core/world.hpp"
#include "battlecontroller.h"
#include "catalogmodel.h"
#include "cityunitsmodel.h"
#include "countrymodel.h"
#include "linkmodel.h"
#include "savestore.h"
#include "worldmodel.h"

namespace ad::client {

/// QML singleton that owns the campaign (docs/ARCHITECTURE.md). Every
/// action QML can take goes through an invokable here and into
/// Campaign::apply; the models are updated from the results, never
/// rebuilt per action. The AI round is stepped in time slices from the
/// main thread's event loop (a QTimer at interval 0, at most ~8 ms of
/// stepping per tick), so the UI keeps animating and no second thread
/// ever touches the campaign.
class GameController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(bool worldLoaded READ worldLoaded CONSTANT)
  Q_PROPERTY(QString loadError READ loadError CONSTANT)
  Q_PROPERTY(ad::client::WorldModel* cities READ cities CONSTANT)
  Q_PROPERTY(ad::client::LinkModel* links READ links CONSTANT)
  Q_PROPERTY(ad::client::CountryModel* countries READ countries CONSTANT)
  Q_PROPERTY(ad::client::CityUnitsModel* cityUnits READ cityUnits CONSTANT)
  Q_PROPERTY(ad::client::CatalogModel* catalog READ catalog CONSTANT)
  Q_PROPERTY(ad::client::SaveStore* saves READ saves CONSTANT)
  Q_PROPERTY(ad::client::BattleController* battle READ battle CONSTANT)
  Q_PROPERTY(double mapWidth READ mapWidth CONSTANT)
  Q_PROPERTY(double mapHeight READ mapHeight CONSTANT)
  /// Every country outline as one SVG path (the Borders layer).
  Q_PROPERTY(QString bordersPath READ bordersPath CONSTANT)
  Q_PROPERTY(int dominationPercent READ dominationPercent CONSTANT)

  Q_PROPERTY(bool active READ active NOTIFY campaignChanged)
  Q_PROPERTY(int round READ round NOTIFY stateChanged)
  Q_PROPERTY(QString humanKey READ humanKey NOTIFY campaignChanged)
  Q_PROPERTY(QString humanName READ humanName NOTIFY campaignChanged)
  Q_PROPERTY(QString humanFlag READ humanFlag NOTIFY campaignChanged)
  Q_PROPERTY(QColor humanColor READ humanColor NOTIFY campaignChanged)
  Q_PROPERTY(QString currentPlayerKey READ currentPlayerKey NOTIFY stateChanged)
  Q_PROPERTY(QString currentPlayerName READ currentPlayerName NOTIFY stateChanged)
  Q_PROPERTY(bool humanTurn READ humanTurn NOTIFY stateChanged)
  Q_PROPERTY(double funds READ funds NOTIFY stateChanged)
  Q_PROPERTY(double income READ income NOTIFY stateChanged)
  Q_PROPERTY(double sharePercent READ sharePercent NOTIFY stateChanged)
  Q_PROPERTY(int cityCount READ cityCount NOTIFY stateChanged)
  Q_PROPERTY(int rank READ rank NOTIFY stateChanged)
  Q_PROPERTY(bool gameOver READ gameOver NOTIFY stateChanged)
  Q_PROPERTY(bool victory READ victory NOTIFY stateChanged)
  Q_PROPERTY(QString winnerName READ winnerName NOTIFY stateChanged)
  Q_PROPERTY(bool aiThinking READ aiThinking NOTIFY aiThinkingChanged)
  Q_PROPERTY(QString aiStatus READ aiStatus NOTIFY aiStatusChanged)
  Q_PROPERTY(double aiProgress READ aiProgress NOTIFY aiStatusChanged)
  Q_PROPERTY(int selectedCity READ selectedCity WRITE setSelectedCity NOTIFY selectedCityChanged)
  Q_PROPERTY(int interaction READ interaction WRITE setInteraction NOTIFY interactionChanged)
  Q_PROPERTY(QString modeKey READ modeKey NOTIFY campaignChanged)
  Q_PROPERTY(QString difficultyKey READ difficultyKey NOTIFY campaignChanged)
  Q_PROPERTY(QString seedText READ seedText NOTIFY campaignChanged)
  Q_PROPERTY(bool hotseat READ hotseat NOTIFY campaignChanged)
  Q_PROPERTY(QString currentSlot READ currentSlot NOTIFY campaignChanged)
  Q_PROPERTY(QString worldHash READ worldHash CONSTANT)
  Q_PROPERTY(bool battleOffered READ battleOffered NOTIFY battleOfferedChanged)
  Q_PROPERTY(QVariantMap battleOffer READ battleOffer NOTIFY battleOfferedChanged)

public:
  enum Interaction { Browse = 0, Moving = 1, Attacking = 2 };
  Q_ENUM(Interaction)

  explicit GameController(QObject* parent = nullptr);
  ~GameController() override;

  [[nodiscard]] bool worldLoaded() const { return world_.has_value() && catalog_.has_value(); }
  [[nodiscard]] QString loadError() const { return loadError_; }
  [[nodiscard]] WorldModel* cities() const { return cities_; }
  [[nodiscard]] LinkModel* links() const { return links_; }
  [[nodiscard]] CountryModel* countries() const { return countries_; }
  [[nodiscard]] CityUnitsModel* cityUnits() const { return cityUnits_; }
  [[nodiscard]] CatalogModel* catalog() const { return catalogModel_; }
  [[nodiscard]] SaveStore* saves() const { return saves_; }
  [[nodiscard]] BattleController* battle() const { return battle_; }
  [[nodiscard]] double mapWidth() const;
  [[nodiscard]] double mapHeight() const;
  [[nodiscard]] QString bordersPath() const { return bordersPath_; }
  [[nodiscard]] QString worldHash() const;

  [[nodiscard]] bool active() const { return campaign_ != nullptr; }
  [[nodiscard]] int round() const;
  [[nodiscard]] QString humanKey() const;
  [[nodiscard]] QString humanName() const;
  [[nodiscard]] QString humanFlag() const;
  [[nodiscard]] QColor humanColor() const;
  [[nodiscard]] QString currentPlayerKey() const;
  [[nodiscard]] QString currentPlayerName() const;
  [[nodiscard]] bool humanTurn() const;
  [[nodiscard]] double funds() const;
  [[nodiscard]] double income() const;
  [[nodiscard]] double sharePercent() const;
  [[nodiscard]] int dominationPercent() const;
  [[nodiscard]] int cityCount() const;
  [[nodiscard]] int rank() const;
  [[nodiscard]] bool gameOver() const;
  [[nodiscard]] bool victory() const;
  [[nodiscard]] QString winnerName() const;
  [[nodiscard]] bool aiThinking() const { return round_ != nullptr; }
  [[nodiscard]] QString aiStatus() const { return aiStatus_; }
  [[nodiscard]] double aiProgress() const { return aiProgress_; }
  [[nodiscard]] int selectedCity() const { return selected_; }
  void setSelectedCity(int id);
  [[nodiscard]] int interaction() const { return interaction_; }
  void setInteraction(int mode);
  [[nodiscard]] QString modeKey() const;
  [[nodiscard]] QString difficultyKey() const;
  [[nodiscard]] QString seedText() const { return seedText_; }
  [[nodiscard]] bool hotseat() const;
  [[nodiscard]] QString currentSlot() const { return currentSlot_; }
  [[nodiscard]] bool battleOffered() const { return offer_.has_value(); }
  [[nodiscard]] QVariantMap battleOffer() const;

  // -- lifecycle ---------------------------------------------------------------
  Q_INVOKABLE bool newGame(const QString& countryKey, const QString& mode, const QString& difficulty,
                           const QString& seedText, const QStringList& extraHumans);
  Q_INVOKABLE bool loadGame(const QString& slot);
  Q_INVOKABLE bool saveGame(const QString& slot);
  Q_INVOKABLE void leaveGame();

  // -- the human's actions ---------------------------------------------------
  Q_INVOKABLE QVariantMap cityInfo(int id) const;
  Q_INVOKABLE QVariantList unitsOf(int cityId) const;
  Q_INVOKABLE QVariantList moveTargets(int cityId) const;
  Q_INVOKABLE QVariantList attackTargets(int cityId) const;
  Q_INVOKABLE QString recruit(int cityId, const QString& typeKey, int count);
  Q_INVOKABLE QString moveUnits(int from, int to, const QVariantList& unitIds);
  Q_INVOKABLE QString attack(int from, int to, const QVariantList& unitIds);
  Q_INVOKABLE void endTurn();
  Q_INVOKABLE int capitalOf(const QString& countryKey) const;
  Q_INVOKABLE QVariantMap countryInfo(const QString& key) const;
  Q_INVOKABLE QVariantList allCountries() const;
  Q_INVOKABLE QString randomCountryKey() const;
  /// The cities the human owns that still have unacted units, ascending id.
  Q_INVOKABLE QVariantList citiesWithUnacted() const;
  Q_INVOKABLE QVariantList searchCities(const QString& text, int limit) const;
  Q_INVOKABLE QString formatNumber(double n) const;

  // -- battles ---------------------------------------------------------------
  /// Called by BattleController when a board battle ends.
  void battleFinished(const ad::core::BattleOutcome& outcome);
  /// The human answers an AI attack: fight it on the board or let the engine play it.
  Q_INVOKABLE void acceptBattle(bool autoResolve);
  /// The human's own attack that needs a battle: fight or auto-resolve.
  Q_INVOKABLE void fightPending(bool autoResolve);

  [[nodiscard]] ad::core::Campaign* campaign() const { return campaign_.get(); }
  [[nodiscard]] const ad::core::World* world() const { return world_ ? &*world_ : nullptr; }
  [[nodiscard]] const ad::core::Catalog* coreCatalog() const { return catalog_ ? &*catalog_ : nullptr; }

signals:
  void campaignChanged();
  void stateChanged();
  void aiThinkingChanged();
  void aiStatusChanged();
  void selectedCityChanged();
  void interactionChanged();
  void battleOfferedChanged();
  /// A board battle must be played on the battle page (human involved).
  void battleRequested();
  /// Notices for the toast strip.
  void notice(const QString& text, const QString& kind);
  void cityCaptured(int cityId, const QString& byKey);
  void roundEnded(int round);
  void gameEnded(bool victory);

private:
  [[nodiscard]] ad::core::PlayerId me() const;
  [[nodiscard]] QString nameOf(ad::core::PlayerId p) const;
  void installCampaign(std::unique_ptr<ad::core::Campaign> c, const QString& slot);
  void refreshAll();
  void refreshCities(std::initializer_list<int> cities);
  void refreshHighlights();
  void trackHuman();
  void startAiRound();
  void stepAiRound();
  void finishAiRound();
  void startBoardBattle(const ad::core::PendingBattle& pb);
  void applyOutcome(const ad::core::PendingBattle& pb, const ad::core::BattleOutcome& out);
  void handleCapture(int city, ad::core::PlayerId by, ad::core::PlayerId from);
  void autosave();
  [[nodiscard]] QVariantMap saveMeta() const;
  [[nodiscard]] ad::core::SearchBudget humanBudget() const;

  std::optional<ad::core::World> world_;
  std::optional<ad::core::Catalog> catalog_;
  QString loadError_;
  std::unique_ptr<ad::core::Campaign> campaign_;
  std::unique_ptr<ad::core::AiRound> round_;
  QTimer aiTimer_;
  QString aiStatus_;
  double aiProgress_{0.0};
  int aiTurns_{0};
  int selected_{-1};
  int interaction_{Browse};
  std::optional<ad::core::PendingBattle> offer_;  // the battle waiting for a Fight / Auto-resolve answer
  bool offerIsAttack_{false};                       // true: the human's own attack
  ad::core::PlayerId lastHuman_{-1};
  QString seedText_;
  QString currentSlot_;
  QString bordersPath_;
  QElapsedTimer playClock_;
  qint64 playSeconds_{0};
  int aiCount_{0};
  std::vector<ad::core::PlayerId> ownerCache_;  // owner per city as the models last saw it
  std::vector<int> baseIncome_;                 // GDP-mode starting income per country

  WorldModel* cities_{nullptr};
  LinkModel* links_{nullptr};
  CountryModel* countries_{nullptr};
  CityUnitsModel* cityUnits_{nullptr};
  CatalogModel* catalogModel_{nullptr};
  SaveStore* saves_{nullptr};
  BattleController* battle_{nullptr};
};

} // namespace ad::client
