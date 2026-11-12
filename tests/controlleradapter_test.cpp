#include "batchexport.h"
#include "controlleradapter.h"
#include "document.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
#include <type_traits>

static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<JobMoves &>()[0])>>);
static_assert(
    std::is_const_v<std::remove_reference_t<decltype(*std::declval<JobMoves &>().begin())>>);

class ControllerAdapterTest : public QObject {
    Q_OBJECT
    DeviceProfile unknownProfile() {
        auto json = DeviceProfile::m3Reference().toJson();
        json["driver"] =
            QJsonObject{{"id", "test-binary"}, {"transport", "usb"}, {"settings", QJsonObject{}}};
        auto capabilities = json["capabilities"].toObject();
        capabilities["dynamic-power"] = false;
        json["capabilities"] = capabilities;
        QString error;
        const auto profile = DeviceProfile::parse(QJsonDocument(json).toJson(), error);
        if (!profile)
            qFatal("Invalid fixture");
        return *profile;
    }
    JobPlan smallPlan() {
        JobPlan plan;
        plan.moves = {{{0, 0}, {10, 10}, 1000, 20, 0, false}};
        return plan;
    }
  private slots:
    void neutralGeometryDoesNotRequireAnOutputAdapter() {
        Document document;
        document.newProject();
        document.addShape("rect");
        auto input = document.jobInput();
        const auto reference = JobPlanner::build(input);
        input.deviceProfile = unknownProfile();
        const auto neutral = JobPlanner::build(input);
        QVERIFY(neutral.valid());
        QVERIFY(neutral.errors.isEmpty());
        QCOMPARE(neutral.motionFingerprint(), reference.motionFingerprint());
        QVERIFY(!ControllerAdapters::find(input.deviceProfile));
        QVERIFY(!neutral.summary()["outputSupported"].toBool());
        QVERIFY(!neutral.summary()["outputError"].toString().isEmpty());
        const auto program = neutral.encodedProgram();
        QVERIFY(!program.valid());
        QVERIFY(program.payload().isEmpty());
        QVERIFY(neutral.gcode().isEmpty());
        const auto matrix = JobPlanner::materialTest(1000, 2000, 10, 20, 3, input.deviceProfile);
        QVERIFY(matrix.valid());
        QVERIFY(!matrix.encodedProgram().valid());
        QString error;
        QVERIFY(!ControllerAdapters::projectProfile(input.deviceProfile, error));
        QVERIFY(!error.isEmpty());
    }
    void artifactsBindBothProfileAndPhysicalMovesAndCachesCannotGoStale() {
        auto plan = smallPlan();
        const auto original = plan.encodedProgram();
        QVERIFY(original.valid());
        QCOMPARE(original.mediaType(), QString("text/x-gcode"));
        QCOMPARE(original.driverId(), QString("grbl-serial"));
        QVERIFY(original.matches(plan.deviceProfile(), plan.motionFingerprint()));
        QCOMPARE(original.payload(), (plan.gcode().join('\n') + '\n').toUtf8());
        plan.cacheCommands();
        QVERIFY(!plan.cachedCommands().isEmpty());
        const auto &retained = plan.moves[0];
        plan.cacheCommands();
        auto updated = retained;
        updated.power = 80;
        plan.moves.replace(0, updated);
        QVERIFY(plan.cachedCommands().isEmpty());
        const auto changed = plan.encodedProgram();
        QVERIFY(changed.valid());
        QVERIFY(changed.payload().contains("M4 S800"));
        QVERIFY(!changed.payload().contains("M4 S200"));
        QVERIFY(changed.sourceFingerprint() != original.sourceFingerprint());
        QVERIFY(original.payload().contains("M4 S200"));
        plan.cacheCommands();
        QCOMPARE(plan.cachedCommands(), changed.gcodeLines());
        updated = plan.moves[0];
        updated.to.setX(11);
        plan.moves.replace(0, updated);
        QVERIFY(plan.cachedCommands().isEmpty());
        QVERIFY(plan.encodedProgram().sourceFingerprint() != changed.sourceFingerprint());
        QVERIFY(!original.matches(unknownProfile(), original.sourceFingerprint()));
        auto binary =
            ControllerProgram::success(unknownProfile(), original.sourceFingerprint(),
                                       "application/octet-stream", QByteArray::fromHex("00ff4d34"));
        QVERIFY(binary.valid());
        QVERIFY(binary.gcodeLines().isEmpty());
    }
    void invalidOutputIsAtomicAndCancelledCachesDoNotEscape() {
        for (int variant = 0; variant < 6; ++variant) {
            auto plan = smallPlan();
            plan.cacheCommands();
            auto move = plan.moves[0];
            if (variant == 0)
                move.power = std::numeric_limits<double>::quiet_NaN();
            if (variant == 1)
                move.speed = 0;
            if (variant == 2)
                move.to.setY(std::numeric_limits<double>::infinity());
            if (variant == 3)
                move.to.setX(201);
            if (variant == 4)
                move.power = 101;
            if (variant == 5)
                move.from.setX(-1);
            plan.moves.replace(0, move);
            const auto invalid = plan.encodedProgram();
            QVERIFY(!invalid.valid());
            QVERIFY(!invalid.errors().isEmpty());
            QVERIFY(invalid.payload().isEmpty());
            QVERIFY(plan.gcode().isEmpty());
        }
        auto plan = smallPlan();
        plan.cacheCommands();
        auto token = std::make_shared<std::atomic_bool>(true);
        const auto cancelled = plan.encodedProgram(token);
        QVERIFY(cancelled.cancelled());
        QVERIFY(cancelled.payload().isEmpty());
        QVERIFY(plan.gcode(token).isEmpty());
        plan.cancelled = true;
        QVERIFY(!plan.valid());
        QVERIFY(plan.cachedCommands().isEmpty());
        QVERIFY(!plan.encodedProgram().valid());
        plan = smallPlan();
        auto tagged = plan.moves[0];
        tagged.slice = std::numeric_limits<int>::min();
        plan.moves.replace(0, tagged);
        QVERIFY(plan.encodedProgram().valid());
        QVERIFY(plan.gcode().join('\n').contains("2147483648"));
        plan.moves.resize(JobPlan::maximumMoves + 1);
        QVERIFY(plan.motionFingerprint().isEmpty());
        QVERIFY(!plan.encodedProgram().valid());
    }
    void physicalPlanningRejectsInvalidNumericInputsWithoutPartialMoves() {
        Document document;
        document.newProject();
        document.addShape("rect");
        auto input = document.jobInput();
        auto settings = input.layers[1].toMap();
        settings["speed"] = std::numeric_limits<double>::quiet_NaN();
        input.layers[1] = settings;
        const auto invalid = JobPlanner::build(input);
        QVERIFY(!invalid.valid());
        QVERIFY(invalid.moves.isEmpty());
        QVERIFY(!invalid.errors.isEmpty());
    }
    void missingAdapterNeverPublishesANewlineOnlyBatchFile() {
        QTemporaryDir storage;
        Document document;
        document.newProject();
        document.addShape("rect");
        auto input = document.jobInput();
        input.deviceProfile = unknownProfile();
        QFile csv(storage.filePath("input.csv"));
        QVERIFY(csv.open(QIODevice::WriteOnly));
        csv.write("name\nrow\n");
        csv.close();
        QFile keep(storage.filePath("keep.txt"));
        QVERIFY(keep.open(QIODevice::WriteOnly));
        keep.write("preserve");
        keep.close();
        const auto result = BatchExport::run(input, QUrl::fromLocalFile(csv.fileName()),
                                             QUrl::fromLocalFile(storage.path()), "");
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(QDir(storage.path()).entryList({"*.gcode"}, QDir::Files).size(), 0);
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("preserve"));
    }
};
QTEST_MAIN(ControllerAdapterTest)
#include "controlleradapter_test.moc"
