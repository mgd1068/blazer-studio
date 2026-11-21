#include "grbl.h"
#include "virtualgrblplant.h"
#include <QRandomGenerator>
#include <QSocketNotifier>
#include <QtTest>
#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

namespace {
class PlantPort : public QObject {
  public:
    VirtualGrblPlant plant;
    QString endpoint;
    QStringList received;
    int master = -1;
    QByteArray pending;
    QSocketNotifier *notifier = nullptr;
    bool reportEnabled = true, ackEnabled = true;
    explicit PlantPort(QRectF bounds = {0, 0, 200, 150}, QPointF actual = {80, 60},
                       QPointF workOffset = {}, QPointF home = {2, 2})
        : plant(bounds, actual, workOffset, home) {
        int slave;
        char name[256];
        if (openpty(&master, &slave, name, nullptr, nullptr))
            qFatal("openpty failed");
        endpoint = QString::fromLocal8Bit(name);
        termios attributes{};
        if (tcgetattr(slave, &attributes))
            qFatal("tcgetattr failed");
        cfmakeraw(&attributes);
        if (tcsetattr(slave, TCSANOW, &attributes))
            qFatal("tcsetattr failed");
        close(slave);
        fcntl(master, F_SETFL, O_NONBLOCK);
        notifier = new QSocketNotifier(master, QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, [this] {
            char bytes[4096];
            ssize_t count;
            while ((count = read(master, bytes, sizeof(bytes))) > 0) {
                for (ssize_t i = 0; i < count; ++i) {
                    const auto byte = static_cast<unsigned char>(bytes[i]);
                    if (byte == '?') {
                        if (reportEnabled)
                            report();
                        continue;
                    }
                    if (byte == 0x18) {
                        pending.clear();
                        plant.reset();
                        send("Grbl 1.1h ['$' for help]\r\n");
                        continue;
                    }
                    if (byte == '!') {
                        plant.hold = true;
                        continue;
                    }
                    if (byte == '~') {
                        plant.hold = false;
                        continue;
                    }
                    if (byte >= 0x80)
                        continue;
                    if (byte == '\r' || byte == '\n') {
                        if (!pending.isEmpty())
                            execute(pending);
                        pending.clear();
                    } else
                        pending.append(char(byte));
                }
            }
        });
    }
    ~PlantPort() override {
        notifier->setEnabled(false);
        close(master);
    }
    void send(const QByteArray &bytes) {
        if (write(master, bytes.constData(), bytes.size()) != bytes.size())
            qFatal("PTY write failed");
    }
    void report() {
        const auto c = plant.counter();
        const auto wco = plant.offset();
        send(QString("<%1|MPos:%2,%3,0|WCO:%4,%5,0|FS:0,%6>\r\n")
                 .arg(plant.alarm  ? "Alarm"
                      : plant.hold ? "Hold:0"
                                   : "Idle")
                 .arg(c.x(), 0, 'f', 3)
                 .arg(c.y(), 0, 'f', 3)
                 .arg(wco.x(), 0, 'f', 3)
                 .arg(wco.y(), 0, 'f', 3)
                 .arg(plant.idleOutput(), 0, 'f', 0)
                 .toLatin1());
    }
    void modalReport() {
        send(QString("[GC:G%1 G%2 G17 G%3 G%4 G94 M%5 T0 F0 S%6]\r\n")
                 .arg(plant.modal.motion)
                 .arg(plant.modal.coordinate)
                 .arg(plant.modal.inches ? 20 : 21)
                 .arg(plant.modal.absolute ? 90 : 91)
                 .arg(plant.modal.spindle ? plant.modal.spindle : 5)
                 .arg(plant.modal.power, 0, 'f', 0)
                 .toLatin1());
    }
    void execute(const QByteArray &block) {
        received.append(QString::fromLatin1(block));
        if (block == "$I")
            send("[VER:1.1h.virtual-plant:]\r\n");
        else if (block == "$$")
            send("$13=0\r\n$22=1\r\n$30=1000\r\n$31=0\r\n$32=1\r\n$130=200\r\n$131=150\r\n");
        else if (block == "$G")
            modalReport();
        else {
            const auto result = plant.apply(block);
            if (result == VirtualGrblPlant::Result::Rejected) {
                send("error:20\r\n");
                return;
            }
            if (result == VirtualGrblPlant::Result::Limit) {
                send("ALARM:1\r\n");
                return;
            }
        }
        if (ackEnabled)
            send("ok\r\n");
    }
};

JobPlan rectangle(double width, double height) {
    JobPlan plan;
    // Explicit scene edges. Physical expectations below are separate literals,
    // not obtained from application geometry, GCode, or coordinate helpers.
    plan.moves = {{{0, 0}, {width, 0}, 1000, 20, 0, false},
                  {{width, 0}, {width, height}, 1000, 20, 0, false},
                  {{width, height}, {0, height}, 1000, 20, 0, false},
                  {{0, height}, {0, 0}, 1000, 20, 0, false}};
    return plan;
}
bool near(QPointF a, QPointF b) {
    return std::abs(a.x() - b.x()) < .00001 && std::abs(a.y() - b.y()) < .00001;
}
} // namespace

// Same test-only friend name as the protocol test executable. This separate
// binary does not introduce a public commissioning API or production bypass.
class GrblTest : public QObject {
    Q_OBJECT
    void homeAndSupplySyntheticContract(GrblSession &session, QRectF bounds) {
        session.commissionedHomingContract = true;
        session.home();
        QTRY_VERIFY(session.ready() && session.status().positionReferenced);
        session.commissionedEnvelope = bounds;
        session.commissionedModalContract = true;
        QVERIFY(session.motionSafetyError().isEmpty());
    }
  private slots:
    void workZeroChangesOnlyG54AndPreservesPhysicalHead_data() {
        QTest::addColumn<QRectF>("envelope");
        QTest::addColumn<QPointF>("actual");
        QTest::addColumn<QPointF>("home");
        QTest::addColumn<QPointF>("initialOffset");
        QTest::addColumn<QPointF>("expectedHead");
        QTest::newRow("positive-shifted") << QRectF(0,0,200,150) << QPointF(80,60)
            << QPointF(20,10) << QPointF(20,-10) << QPointF(50,30);
        QTest::newRow("signed-negative") << QRectF(-200,-150,200,150) << QPointF(-80,-60)
            << QPointF(-180,-130) << QPointF(-200,-150) << QPointF(-150,-110);
    }
    void workZeroChangesOnlyG54AndPreservesPhysicalHead() {
        QFETCH(QRectF, envelope);
        QFETCH(QPointF, actual);
        QFETCH(QPointF, home);
        QFETCH(QPointF, initialOffset);
        QFETCH(QPointF, expectedHead);
        PlantPort port(envelope,actual,initialOffset,home);
        GrblSession session;
        QVERIFY(session.open(port.endpoint));
        QTRY_VERIFY(session.ready());
        homeAndSupplySyntheticContract(session, envelope);
        session.jog(30, 20);
        QTRY_VERIFY(session.ready());
        QCOMPARE(port.plant.physical, expectedHead);
        const auto travels = port.plant.travel.size();
        const auto cut = port.received.size();
        session.zero();
        QVERIFY(session.status().coordinateUpdatePending);
        QTRY_VERIFY(session.ready());
        QVERIFY(!session.status().coordinateUpdatePending);
        QVERIFY(session.status().verified && session.status().positionReferenced);
        QCOMPARE(port.plant.physical, expectedHead);
        QCOMPARE(port.plant.counter(), expectedHead);
        QCOMPARE(port.plant.g54, expectedHead);
        QCOMPARE(port.plant.work(), QPointF());
        QCOMPARE(port.plant.travel.size(), travels);
        QCOMPARE(port.plant.modal.spindle, 0);
        QCOMPARE(port.plant.modal.power, 0.);
        QCOMPARE(port.plant.limits, 0);
        QCOMPARE(port.plant.rejected, 0);
        QCOMPARE(port.received.mid(cut),
                 QStringList({"M5 S0", "G21", "G90", "G10 L20 P1 X0 Y0", "G54", "$G"}));
    }
    void workZeroPhysicalPlantMovementIsNotHiddenByAnOffsetUpdate() {
        PlantPort port;
        GrblSession session;
        QVERIFY(session.open(port.endpoint));
        QTRY_VERIFY(session.ready());
        homeAndSupplySyntheticContract(session, {0, 0, 200, 150});
        session.jog(30, 20);
        QTRY_VERIFY(session.ready());
        QCOMPARE(port.plant.physical, QPointF(32, 22));
        port.ackEnabled = false;
        session.zero();
        QVERIFY(session.status().coordinateUpdatePending);
        port.plant.physical = QPointF(32.001, 22); // Independent external displacement.
        port.plant.g54 = port.plant.counter();     // WPos zero cannot hide changed raw MPos.
        port.report();
        QTRY_VERIFY(!session.connected());
        QVERIFY(!session.status().coordinateUpdatePending);
        QVERIFY(!session.status().positionReferenced);
        QCOMPARE(port.plant.physical, QPointF(32.001, 22));
    }
    void diagnosticChronologyRetainsOnlyFirstApproachAndPoweredTargetWithProtocolAcks() {
        PlantPort port;
        GrblSession session;
        QVERIFY(session.open(port.endpoint));
        QTRY_VERIFY(session.ready());
        homeAndSupplySyntheticContract(session, {0, 0, 200, 150});
        session.jog(71, 47);
        QTRY_VERIFY(session.ready());
        QCOMPARE(port.plant.physical, QPointF(73, 49));
        session.start(rectangle(200, 150));
        QTRY_VERIFY(!session.busy());
        const auto snapshot = session.diagnosticSnapshot();
        const auto context = snapshot["streamStartContext"].toMap();
        QVERIFY(context["homingProtocolReferenced"].toBool());
        QVERIFY(context["homingContract"].toBool());
        QVERIFY(context["machineEnvelopeContract"].toBool());
        QVERIFY(context["modalContract"].toBool());
        QCOMPARE(context["readback"].toMap()["$30"].toMap()["value"].toInt(), 1000);
        QCOMPARE(context["modalReadback"].toMap()["workCoordinateCode"].toMap()["value"].toInt(),
                 54);
        QCOMPARE(context["modalReadback"].toMap()["unitsCode"].toMap()["value"].toInt(), 21);
        QCOMPARE(context["workXY"].toMap()["xMm"].toDouble(), 73.);
        QCOMPARE(context["machineXY"].toMap()["yMm"].toDouble(), 49.);
        QVERIFY(context["wcoXY"].toMap()["known"].toBool());
        QVERIFY(context["workXY"].toMap()["ageAtStartMs"].toLongLong() >= 0);
        const auto events = snapshot["events"].toList();
        QVariantMap approach, target;
        int coordinateEvents = 0;
        quint64 lastSequence = 0;
        qint64 lastTime = -1;
        for (const auto &item : events) {
            const auto event = item.toMap();
            QVERIFY(event["sequence"].toULongLong() > lastSequence);
            QVERIFY(event["elapsedMs"].toLongLong() >= lastTime);
            lastSequence = event["sequence"].toULongLong();
            lastTime = event["elapsedMs"].toLongLong();
            if (event.contains("xMm"))
                ++coordinateEvents;
            if (event["event"] != "commandQueued")
                continue;
            if (event["command"] == "firstApproachRequested")
                approach = event;
            if (event["command"] == "firstPoweredTargetRequested")
                target = event;
        }
        QCOMPARE(coordinateEvents, 2); // No remaining rectangle vertices/toolpath retained.
        QCOMPARE(approach["acknowledgement"].toString(), QString("acknowledged"));
        QCOMPARE(target["acknowledgement"].toString(), QString("acknowledged"));
        QCOMPARE(approach["xMm"].toDouble(), 0.);
        QCOMPARE(approach["yMm"].toDouble(), 0.);
        QCOMPARE(target["xMm"].toDouble(), 200.);
        QCOMPARE(target["yMm"].toDouble(), 0.);
        QCOMPARE(target["requestedS"].toDouble(), 200.);
        QVERIFY(approach["sequence"].toULongLong() < target["sequence"].toULongLong());
        for (const auto &dispatch : {approach, target}) {
            bool ackFound = false;
            for (const auto &item : events) {
                const auto ack = item.toMap();
                if (ack["event"] == "commandAcknowledged" &&
                    ack["dispatchSequence"] == dispatch["sequence"]) {
                    QVERIFY(ack["sequence"].toULongLong() > dispatch["sequence"].toULongLong());
                    ackFound = true;
                }
            }
            QVERIFY(ackFound);
        }
        const auto serialized =
            QJsonDocument(QJsonObject::fromVariantMap(snapshot)).toJson(QJsonDocument::Compact);
        QVERIFY(serialized.size() <= ControllerDiagnostics::maximumSnapshotBytes);
        QVERIFY(!serialized.contains(port.endpoint.toUtf8()));
        QVERIFY(!serialized.contains("G1 X"));
        QVERIFY(!serialized.contains("G0 X"));
        QCOMPARE(port.plant.limits, 0); // Separate plant evidence, never an audit claim.
    }
    void reportedZeroCanHideANonoriginPhysicalHeadAndCauseACollision() {
        VirtualGrblPlant plant({0, 0, 200, 150}, {80, 60}, {}, {2, 2});
        QCOMPARE(plant.counter(), QPointF(0, 0));
        QVERIFY(!plant.homed);
        for (const auto &block : {"M5 S0", "G21 G90 G54", "G0 X0 Y0", "M4 S200"})
            QCOMPARE(plant.apply(block), VirtualGrblPlant::Result::Accepted);
        QCOMPARE(plant.physical, QPointF(80, 60)); // 'G0 to zero' did not reach physical zero.
        QCOMPARE(plant.apply("G1 X200 Y0 F1000"), VirtualGrblPlant::Result::Limit);
        QCOMPARE(plant.travel.last().from, QPointF(80, 60));
        QCOMPARE(plant.travel.last().attempted, QPointF(280, 60));
        QCOMPARE(plant.physical, QPointF(200, 60)); // First hard-boundary intersection.
        QVERIFY(plant.travel.last().emitting);
        QVERIFY(plant.alarm);
        QCOMPARE(plant.limits, 1);
        QCOMPARE(plant.modal.spindle, 0);
    }
    void independentPlantStopsAtFirstDiagonalHardBoundary() {
        VirtualGrblPlant plant({0, 0, 200, 150}, {80, 60}, {}, {80, 60});
        plant.home();
        QCOMPARE(plant.apply("G1 X280 Y360 M4 S200 F1000"), VirtualGrblPlant::Result::Limit);
        QCOMPARE(plant.physical, QPointF(140, 150));
        QCOMPARE(plant.travel.last().attempted, QPointF(280, 360));
        QCOMPARE(plant.limits, 1);
    }
    void independentInterpreterModelsUnitsDistanceOffsetsAndPower() {
        VirtualGrblPlant plant({0, 0, 200, 150}, {70, 60}, {10, 20}, {2, 2});
        plant.home();
        QCOMPARE(plant.apply("G21 G90 G54 G0 X20 Y10"), VirtualGrblPlant::Result::Accepted);
        QCOMPARE(plant.physical, QPointF(30, 30));
        QCOMPARE(plant.apply("G20 G91 G1 X1 Y0.5 M4 S200 F10"), VirtualGrblPlant::Result::Accepted);
        QVERIFY(near(plant.physical, {55.4, 42.7}));
        QVERIFY(plant.travel.last().emitting);
        QCOMPARE(plant.apply("G90 G0 X1 Y1"), VirtualGrblPlant::Result::Accepted);
        QVERIFY(near(plant.physical, {35.4, 45.4})); // Inches plus G54 offset.
        QVERIFY(!plant.travel.last().emitting);      // G0 has laser output suppressed.
        QCOMPARE(plant.apply("G21 G10 L20 P1 X0 Y0"), VirtualGrblPlant::Result::Accepted);
        QVERIFY(near(plant.g54, {35.4, 45.4}));
        QCOMPARE(plant.apply("M5 S0"), VirtualGrblPlant::Result::Accepted);
        QCOMPARE(plant.apply("G1 X10 Y10 F1000"), VirtualGrblPlant::Result::Accepted);
        QVERIFY(near(plant.physical, {45.4, 55.4}));
        QVERIFY(!plant.travel.last().emitting);
        const auto beforeJog = plant.modal;
        QCOMPARE(plant.apply("$J=G91 G21 X1 Y-1 F600"), VirtualGrblPlant::Result::Accepted);
        QVERIFY(near(plant.physical, {46.4, 54.4}));
        QCOMPARE(plant.modal.absolute, beforeJog.absolute);
        QCOMPARE(plant.modal.inches, beforeJog.inches);
        QCOMPARE(plant.apply("M3 S100"), VirtualGrblPlant::Result::Accepted);
        QCOMPARE(plant.idleOutput(), 100.);
        QCOMPARE(plant.apply("M5"), VirtualGrblPlant::Result::Accepted);
        QCOMPARE(plant.idleOutput(), 0.);
        QCOMPARE(plant.apply("G1 X15 Y15"), VirtualGrblPlant::Result::Accepted);
        QVERIFY(!plant.travel.last().emitting); // M5 inhibits output even with retained S100.
        QCOMPARE(plant.limits, 0);
    }
    void malformedAndUnsupportedBlocksHaveNoPartialEffects_data() {
        QTest::addColumn<QByteArray>("block");
        for (const auto &block :
             {"G91 G1 Xnan M4 S200", "G91 G90 G1 X1", "G20 X1 X2", "G53 G0 X1", "G92 X0", "G28",
              "G56 X1", "G1 X1E6", "M4 S1001", "G9999999999999999999999 X1", "G1 X1 garbage"})
            QTest::newRow(block) << QByteArray(block);
    }
    void malformedAndUnsupportedBlocksHaveNoPartialEffects() {
        QFETCH(QByteArray, block);
        VirtualGrblPlant plant({0, 0, 200, 150}, {80, 60}, {}, {2, 2});
        plant.home();
        const auto before = plant.physical;
        QCOMPARE(plant.apply(block), VirtualGrblPlant::Result::Rejected);
        QCOMPARE(plant.physical, before);
        QVERIFY(plant.travel.isEmpty());
        QVERIFY(plant.modal.absolute);
        QVERIFY(!plant.modal.inches);
        QCOMPARE(plant.modal.spindle, 0);
        QCOMPARE(plant.modal.power, 0.);
    }
    void productionPathCannotMoveThePhysicalPlantWithoutCommissioning() {
        PlantPort port;
        GrblSession session;
        QVERIFY(session.open(port.endpoint));
        QTRY_VERIFY(session.ready());
        QCOMPARE(session.position, QPointF(0, 0));
        QCOMPARE(port.plant.physical, QPointF(80, 60));
        const auto plan = rectangle(200, 150);
        session.start(plan);
        session.jog(1, 0);
        session.frame({{"valid", true}, {"x", 0}, {"y", 0}, {"width", 200}, {"height", 150}});
        session.zero();
        session.focus(true);
        QTest::qWait(40);
        QCOMPARE(port.plant.physical, QPointF(80, 60));
        QVERIFY(port.plant.travel.isEmpty());
        QCOMPARE(port.plant.modal.spindle, 0);
        session.home();
        QTest::qWait(40);
        QCOMPARE(port.plant.physical, QPointF(80, 60));
        QCOMPARE(port.plant.homes, 0);
        QVERIFY(!session.homingSafetyError().isEmpty());
        session.commissionedHomingContract = true; // Independent, synthetic setup evidence.
        session.home();
        QTRY_VERIFY(session.status().positionReferenced && session.ready());
        QCOMPARE(port.plant.physical, QPointF(2, 2));
        QVERIFY(!session.motionSafetyError().isEmpty());
        session.start(plan);
        session.jog(1, 0);
        session.focus(true);
        QTest::qWait(40);
        QCOMPARE(port.plant.physical, QPointF(2, 2));
        QVERIFY(port.plant.travel.isEmpty());
        QCOMPARE(port.plant.limits, 0);
    }
    void commissionedRectangleFollowsExactPhysicalEdgesFromArbitraryStart_data() {
        QTest::addColumn<QRectF>("envelope");
        QTest::addColumn<QPointF>("offset");
        QTest::addColumn<QPointF>("home");
        QTest::addColumn<QPointF>("start");
        QTest::addColumn<QSizeF>("size");
        QTest::addColumn<QVector<QPointF>>("vertices");
        QTest::newRow("positive-full-bed")
            << QRectF(0, 0, 200, 150) << QPointF(0, 0) << QPointF(2, 2) << QPointF(73, 49)
            << QSizeF(200, 150) << QVector<QPointF>{{0, 0}, {200, 0}, {200, 150}, {0, 150}, {0, 0}};
        QTest::newRow("negative-full-bed")
            << QRectF(-200, -150, 200, 150) << QPointF(-200, -150) << QPointF(-2, -2)
            << QPointF(-73, -49) << QSizeF(200, 150)
            << QVector<QPointF>{{-200, -150}, {0, -150}, {0, 0}, {-200, 0}, {-200, -150}};
        QTest::newRow("positive-shifted-G54")
            << QRectF(0, 0, 200, 150) << QPointF(35, 20) << QPointF(2, 2) << QPointF(180, 130)
            << QSizeF(120, 80)
            << QVector<QPointF>{{35, 20}, {155, 20}, {155, 100}, {35, 100}, {35, 20}};
    }
    void commissionedRectangleFollowsExactPhysicalEdgesFromArbitraryStart() {
        QFETCH(QRectF, envelope);
        QFETCH(QPointF, offset);
        QFETCH(QPointF, home);
        QFETCH(QPointF, start);
        QFETCH(QSizeF, size);
        QFETCH(QVector<QPointF>, vertices);
        PlantPort port(envelope, start, offset, home);
        GrblSession session;
        QVERIFY(session.open(port.endpoint));
        QTRY_VERIFY(session.ready());
        homeAndSupplySyntheticContract(session, envelope);
        session.jog(start.x() - home.x(), start.y() - home.y());
        QTRY_VERIFY(session.ready());
        QVERIFY(near(port.plant.physical, start));
        port.plant.travel.clear();
        // Inherited inches/incremental mode must be normalized by the output
        // preamble before ANY coordinates are interpreted. Report units stay mm.
        QCOMPARE(port.plant.apply("G20 G91"), VirtualGrblPlant::Result::Accepted);
        session.start(rectangle(size.width(), size.height()));
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(), 3000);
        QCOMPARE(port.plant.limits, 0);
        QCOMPARE(port.plant.rejected, 0);
        QCOMPARE(port.plant.travel.size(), 5);
        const auto &approach = port.plant.travel[0];
        QVERIFY(near(approach.from, start));
        QVERIFY(near(approach.to, vertices[0]));
        QVERIFY(approach.rapid && !approach.emitting);
        for (int edge = 0; edge < 4; ++edge) {
            const auto &travel = port.plant.travel[edge + 1];
            QVERIFY2(near(travel.from, vertices[edge]), qPrintable(QString::number(edge)));
            QVERIFY(near(travel.to, vertices[edge + 1]));
            QVERIFY(!travel.rapid && travel.emitting);
        }
        QVERIFY(near(port.plant.physical, vertices.last()));
        QCOMPARE(port.plant.modal.spindle, 0);
        QCOMPARE(port.plant.modal.power, 0.);
        QVERIFY(port.plant.modal.absolute && !port.plant.modal.inches);
        QCOMPARE(port.plant.modal.coordinate, 54);
    }
    void unsafeG54TransformsAreRejectedBeforeThePhysicalPlantMoves_data() {
        QTest::addColumn<QRectF>("envelope");
        QTest::addColumn<QPointF>("offset");
        QTest::addColumn<QPointF>("home");
        QTest::newRow("positive-offset-outside")
            << QRectF(0, 0, 200, 150) << QPointF(100, 75) << QPointF(2, 2);
        QTest::newRow("negative-offset-outside")
            << QRectF(-200, -150, 200, 150) << QPointF(-150, -100) << QPointF(-2, -2);
    }
    void unsafeG54TransformsAreRejectedBeforeThePhysicalPlantMoves() {
        QFETCH(QRectF, envelope);
        QFETCH(QPointF, offset);
        QFETCH(QPointF, home);
        PlantPort port(envelope, home, offset, home);
        GrblSession session;
        QVERIFY(session.open(port.endpoint));
        QTRY_VERIFY(session.ready());
        homeAndSupplySyntheticContract(session, envelope);
        session.start(rectangle(200, 150));
        QTest::qWait(40);
        QVERIFY(port.plant.travel.isEmpty());
        QCOMPARE(port.plant.physical, home);
        QCOMPARE(port.plant.limits, 0);
        QCOMPARE(port.plant.modal.spindle, 0);
    }
    void modalSurpriseAndMalformedReportsCannotAuthorizeNewTravel() {
        PlantPort port;
        GrblSession session;
        QVERIFY(session.open(port.endpoint));
        QTRY_VERIFY(session.ready());
        homeAndSupplySyntheticContract(session, {0, 0, 200, 150});
        QCOMPARE(port.plant.apply("G55"), VirtualGrblPlant::Result::Accepted);
        port.modalReport(); // Honest feedback from a different active work system.
        QTRY_VERIFY(!session.configurationValid());
        const auto before = port.plant.physical;
        session.start(rectangle(200, 150));
        QTest::qWait(40);
        QVERIFY(port.plant.travel.isEmpty());
        QCOMPARE(port.plant.physical, before);
        QCOMPARE(port.plant.apply("G54"), VirtualGrblPlant::Result::Accepted);
        port.modalReport();
        QTRY_VERIFY(session.ready());
        QVERIFY(!session.motionSafetyError().isEmpty());
        session.start(rectangle(200, 150));
        QTest::qWait(40);
        QVERIFY(port.plant.travel.isEmpty());     // G54 alone did not revive stale proof.
        session.commissionedModalContract = true; // New explicit synthetic evidence.
        port.reportEnabled = false;
        port.send("<Idle|MPos:nan,0,0|WCO:0,0,0|FS:0,0>\r\n");
        QTRY_VERIFY(!session.ready());
        session.start(rectangle(200, 150));
        QTest::qWait(40);
        QVERIFY(port.plant.travel.isEmpty());
        QCOMPARE(port.plant.physical, before);
    }
    void modalSurpriseAbortsBufferedJobWithoutStreamingFurtherPhysicalMoves() {
        PlantPort port;
        GrblSession session;
        QVERIFY(session.open(port.endpoint));
        QTRY_VERIFY(session.ready());
        homeAndSupplySyntheticContract(session, {0, 0, 200, 150});
        port.ackEnabled = false; // Bound the controller's accepted prelude to one RX window.
        const auto before = port.received.size();
        session.start(rectangle(200, 150));
        QTRY_VERIFY(port.received.size() > before);
        QTest::qWait(20);
        const auto movements = port.plant.travel.size();
        for (const auto &event : port.plant.travel)
            QVERIFY(!event.emitting);
        QCOMPARE(port.plant.modal.spindle, 0);
        QCOMPARE(port.plant.apply("G55"), VirtualGrblPlant::Result::Accepted);
        port.modalReport();
        QTRY_VERIFY(!session.connected());
        QVERIFY(!session.status().positionReferenced);
        QTRY_COMPARE(port.plant.resets, 2);
        QCOMPARE(port.plant.travel.size(), movements);
        QCOMPARE(port.plant.modal.spindle, 0);
        QCOMPARE(port.plant.limits, 0);
    }
    void boundedRandomPathsPreservePhysicalGeometryAndPower_data() {
        QTest::addColumn<bool>("negative");
        QTest::newRow("positive") << false;
        QTest::newRow("negative") << true;
    }
    void boundedRandomPathsPreservePhysicalGeometryAndPower() {
        QFETCH(bool, negative);
        const auto offset = negative ? QPointF(-200, -150) : QPointF();
        const QRectF envelope(offset, QSizeF(200, 150));
        const auto home = negative ? QPointF(-2, -2) : QPointF(2, 2);
        PlantPort port(envelope, offset + QPointF(130, 90), offset, home);
        GrblSession session;
        QVERIFY(session.open(port.endpoint));
        QTRY_VERIFY(session.ready());
        homeAndSupplySyntheticContract(session, envelope);
        QRandomGenerator random(0x4d335846u);
        JobPlan plan;
        QVector<QPointF> expected;
        QVector<bool> emission;
        QPointF previous(10, 10);
        expected.append(offset + previous);
        for (int i = 0; i < 80; ++i) {
            const QPointF next(5 + random.bounded(190), 5 + random.bounded(140));
            const bool rapid = i % 7 == 3;
            const double power = i % 5 == 0 ? 0 : 20;
            plan.moves.append({previous, next, 1000, power, 0, rapid});
            expected.append(offset + next);
            emission.append(!rapid && power > 0 && next != previous);
            previous = next;
        }
        session.start(plan);
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(), 5000);
        QCOMPARE(port.plant.limits, 0);
        QCOMPARE(port.plant.rejected, 0);
        QCOMPARE(port.plant.travel.size(), 81);
        QVERIFY(!port.plant.travel[0].emitting);
        QVERIFY(near(port.plant.travel[0].to, expected[0]));
        for (int i = 0; i < 80; ++i) {
            const auto &event = port.plant.travel[i + 1];
            QVERIFY(near(event.from, expected[i]));
            QVERIFY(near(event.to, expected[i + 1]));
            QCOMPARE(event.emitting, emission[i]);
        }
        QVERIFY(near(port.plant.physical, expected.last()));
        QCOMPARE(port.plant.modal.spindle, 0);
    }
};
QTEST_GUILESS_MAIN(GrblTest)
#include "grblplant_test.moc"
