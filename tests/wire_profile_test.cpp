#include "job.h"
#include <QJsonDocument>
#include <QtTest>

class WireProfileTest : public QObject {
    Q_OBJECT
  private slots:
    void fractionalProfileChecksActualWords_data() {
        QTest::addColumn<bool>("yAxis");
        QTest::addColumn<bool>("rapid");
        QTest::addColumn<int>("unsafe");
        for (bool y: {false,true}) for (bool rapid: {false,true}) for (int unsafe: {0,1,2})
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(y?"y":"x")
                .arg(rapid?"rapid-preview":"initial-approach").arg(unsafe))) << y << rapid << unsafe;
    }
    void fractionalProfileChecksActualWords() {
        QFETCH(bool,yAxis); QFETCH(bool,rapid); QFETCH(int,unsafe);
        auto descriptor=DeviceProfile::m3Reference().toJson();
        auto workspace=descriptor["workspace"].toObject();
        workspace[yAxis?"height":"width"]=200.0006;
        descriptor["workspace"]=workspace;
        QString error;
        const auto profile=DeviceProfile::parse(QJsonDocument(descriptor).toJson(),error);
        QVERIFY2(profile,qPrintable(error));
        const auto point=[&](double value) { return yAxis?QPointF(20,value):QPointF(value,20); };
        JobPlan plan(*profile);
        if (rapid) {
            // This raw point is legal but rounds outside. It is deliberately
            // NOT emitted, so it must not by itself reject otherwise safe data.
            plan.moves.append({point(200.0006),point(20),600,0,0,true});
        }
        const auto from=point(unsafe==2?200.0006:20);
        if (rapid && unsafe==2) {
            // Establish a real rapid target at this edge, not a discontinuous
            // powered connector; that target itself must be rejected.
            plan.moves.append({point(20),from,600,0,0,true});
        }
        plan.moves.append({from,point(unsafe==1?200.0006:200.0004),600,20,0,false});
        const auto program=plan.encodedProgram();
        if (unsafe) {
            QVERIFY2(!program.valid(),qPrintable(program.gcodeLines().join("\n")));
            QVERIFY(program.payload().isEmpty());
            QVERIFY(!program.errors().isEmpty());
        } else {
            QVERIFY2(program.valid(),qPrintable(program.errors().join("; ")));
            const auto text=program.gcodeLines().join("\n");
            QVERIFY(text.contains(yAxis?"Y200.000":"X200.000"));
            QVERIFY(!text.contains(yAxis?"Y200.001":"X200.001"));
        }
    }
};
QTEST_APPLESS_MAIN(WireProfileTest)
#include "wire_profile_test.moc"
