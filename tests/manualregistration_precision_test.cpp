#include "manualregistration.h"
#include "document.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtTest>

class ManualRegistrationPrecisionTest:public QObject {
    Q_OBJECT
private slots:
    void documentCommitMustRejectOutwardWireQuantization(){
        auto data=DeviceProfile::m3Reference().toJson();auto workspace=data["workspace"].toObject();
        workspace["width"]=200.0006;data["workspace"]=workspace;QString error;
        const auto profile=DeviceProfile::parse(QJsonDocument(data).toJson(),error);QVERIFY2(profile,qPrintable(error));
        Document document;document.newProject();QVERIFY(document.setDeviceProfile(*profile));
        document.addShape("rect");document.setProperty("x",10.);document.setProperty("y",20.);
        document.setProperty("width",20.);document.setProperty("height",10.);document.setLayer(1);
        const auto input=document.jobInput();const auto canonical=JobPlanner::build(input);
        ManualRegistration::Intent intent{
            ManualRegistration::SourceSnapshot::capture(input,canonical,document.offlineRegistrationFrame()),
            {{10,20},{30,20},{180.0006,20},{200.0006,20},ManualRegistration::Scaling::Rigid,.5}};
        QVERIFY(intent.source.valid());
        QVERIFY(!document.applyManualRegistration(intent));
        QVERIFY(!document.manualRegistrationEnabled());
    }
    void registeredWireCoordinatesRespectDeclaredProfile_data(){
        QTest::addColumn<double>("right");QTest::addColumn<bool>("mustRemainUsable");QTest::addColumn<bool>("registered");
        QTest::newRow("registered-rounding-stays-inside")<<200.0004<<true<<true;
        QTest::newRow("registered-rounding-crosses-right-boundary")<<200.0006<<false<<true;
        QTest::newRow("plain-rounding-stays-inside")<<200.0004<<true<<false;
        QTest::newRow("plain-rounding-crosses-right-boundary")<<200.0006<<false<<false;
    }
    void registeredWireCoordinatesRespectDeclaredProfile(){
        QFETCH(double,right);QFETCH(bool,mustRemainUsable);QFETCH(bool,registered);
        auto data=DeviceProfile::m3Reference().toJson();
        auto workspace=data["workspace"].toObject();workspace["width"]=200.0006;data["workspace"]=workspace;
        QString error;const auto profile=DeviceProfile::parse(QJsonDocument(data).toJson(),error);
        QVERIFY2(profile,qPrintable(error));
        JobInput input;input.deviceProfile=*profile;
        JobPlan source(*profile);
        source.moves={{{0,0},{10,20},3000,0,1,true},{{10,20},{30,20},600,40,1,false}};
        const QByteArray frame(64,'a');
        ManualRegistration::Intent intent{
            ManualRegistration::SourceSnapshot::capture(input,source,frame),
            {{10,20},{30,20},{right-20,20},{right,20},ManualRegistration::Scaling::Rigid,.5}};
        QVERIFY(intent.source.valid());
        const auto transformed=ManualRegistration::apply(input,source,intent,frame);
        if(mustRemainUsable)QVERIFY(transformed.valid());
        if(registered&&!transformed.valid())return;
        JobPlan direct(*profile);
        direct.moves={{{0,0},{right-20,20},3000,0,1,true},{{right-20,20},{right,20},600,40,1,false}};
        const auto program=registered ? transformed.plan.encodedProgram() : direct.encodedProgram();
        if(mustRemainUsable)QVERIFY(program.valid());
        if(!program.valid())return;
        // Independent wire oracle: parse literal coordinate words from the final
        // text. Do not use encoder formatting or planner bounds as expected data.
        const QRegularExpression words("(?:^| )([XY])([-+]?[0-9]+(?:\\.[0-9]+)?)");
        int targets=0;
        for(const auto &line:program.gcodeLines()){
            auto it=words.globalMatch(line);
            while(it.hasNext()){
                const auto match=it.next();const double value=match.captured(2).toDouble();++targets;
                const double maximum=match.captured(1)=="X"?200.0006:150.;
                QVERIFY2(value>=0 && value<=maximum,qPrintable("Accepted wire target outside profile: "+line));
            }
        }
        QVERIFY(targets>=4);
    }
};
QTEST_GUILESS_MAIN(ManualRegistrationPrecisionTest)
#include "manualregistration_precision_test.moc"
