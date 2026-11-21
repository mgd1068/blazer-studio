import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml"

Item {
    id: window
    width: 1180
    height: 760
    visible: true
    QtObject {
        id: appServices
        property string lengthUnit: "mm"
    }
    QtObject {
        id: projectSpy
        property string message: "outside-bed rejected"
        property var material: ({
                name: "Plywood",
                thickness: 3
            })
        property real bedWidth: 200
        property real bedHeight: 150
        property var calls: []
        property bool accept: true
        function command(action, args) {
            calls = calls.concat([
                {
                    action: action,
                    args: args
                }
            ]);
            return accept;
        }
    }
    QtObject {
        id: deviceSpy
        property string bindingKey: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        property string parameterHash: "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        property string activeId: "one"
        property string changeBlockReason: ""
        property string message: ""
        property var instances: [
            {
                id: "one",
                name: "Review device",
                endpoint: "/dev/review-only",
                width: 200,
                height: 150,
                driver: "grbl"
            }
        ]
        readonly property var active: details(activeId)
        property var managementCalls: []
        function details(id) {
            return instances.find(row => row.id === id) || ({});
        }
        function update(id, name, endpoint) {
            managementCalls = managementCalls.concat([
                {
                    action: "update",
                    id: id,
                    name: name,
                    endpoint: endpoint
                }
            ]);
            instances = instances.map(row => row.id === id ? ({
                        id: id,
                        name: name,
                        endpoint: endpoint,
                        width: 200,
                        height: 150,
                        driver: "grbl"
                    }) : row);
            return true;
        }
        function activate(id) {
            managementCalls = managementCalls.concat([
                {
                    action: "activate",
                    id: id,
                    endpoint: details(id).endpoint
                }
            ]);
            activeId = id;
            return true;
        }
    }
    QtObject {
        id: ledgerSpy
        property bool writable: true
        property string message: ""
        property var records: []
        property var calls: []
        function applicable(binding, parameters) {
            return records;
        }
        function record(binding, parameters, observation) {
            calls = calls.concat([
                {
                    binding: binding,
                    parameters: parameters,
                    observation: observation
                }
            ]);
            return "record-id";
        }
    }
    DeviceManagerDialog {
        id: deviceDialog
        manager: deviceSpy
        parent: window
    }
    BarcodeDialog {
        id: barcode
        document: projectSpy
        parent: window
    }
    MaterialTestRecordDialog {
        id: materialRecord
        document: projectSpy
        manager: deviceSpy
        ledger: ledgerSpy
        parent: window
    }
    TestCase {
        name: "RootDialogInputReview"
        when: windowShown
        function field(dialog, name) {
            let f = findChild(dialog, name);
            verify(f !== null, name);
            return f;
        }
        function editor(dialog, name) {
            return field(field(dialog, name), "numericEditor");
        }
        function raw(dialog, name, text) {
            let f = editor(dialog, name);
            f.forceActiveFocus();
            keyClick(Qt.Key_A, Qt.ControlModifier);
            for (let i = 0; i < text.length; i++) {
                let key = text.charCodeAt(i);
                if (key >= 97 && key <= 122)
                    key -= 32;
                keyClick(key);
            }
            wait(0);
        }
        function clickButton(dialog, name) {
            let b = field(dialog, name);
            mouseClick(b, b.width / 2, b.height / 2);
            wait(20);
        }
        function init() {
            barcode.close();
            materialRecord.close();
            deviceDialog.close();
            wait(10);
            projectSpy.calls = [];
            projectSpy.accept = true;
            ledgerSpy.calls = [];
            ledgerSpy.records = [];
            deviceSpy.instances = [
                {
                    id: "one",
                    name: "Review device",
                    endpoint: "/dev/review-only",
                    width: 200,
                    height: 150,
                    driver: "grbl"
                }
            ];
            deviceSpy.activeId = "one";
            deviceSpy.managementCalls = [];
            deviceSpy.changeBlockReason = "";
            appServices.lengthUnit = "mm";
            deviceSpy.bindingKey = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
            deviceSpy.parameterHash = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
        }
        function openBarcode() {
            barcode.moduleWidth = .3;
            barcode.barHeight = 15;
            barcode.left = 10;
            barcode.bottom = 10;
            barcode.open();
            wait(30);
            field(barcode, "barcodeText").text = "ABC";
        }
        function test_rawNumberBeforeBlurReachesCommand() {
            openBarcode();
            raw(barcode, "barcodeModuleWidth", "0,25 mm");
            verify(barcode.valid);
            compare(field(barcode, "barcodeModuleWidth").numericValue(), .25);
            clickButton(barcode, "createBarcodeButton");
            compare(projectSpy.calls.length, 1);
            compare(projectSpy.calls[0].args.moduleWidth, .25);
        }
        function test_numericInvalidInputsKeepCreationDisabled() {
            openBarcode();
            for (const text of ["-1", "NaN", "Infinity", "1e999", "garbage", "0,05 mm", "2.01 mm", ""]) {
                raw(barcode, "barcodeModuleWidth", text);
                verify(!barcode.valid, "Rejected raw " + text);
                verify(!field(barcode, "createBarcodeButton").enabled, "Disabled for " + text);
            }
            compare(projectSpy.calls.length, 0);
        }
        function test_inchInputAndExplicitMillimetreSuffix() {
            appServices.lengthUnit = "in";
            openBarcode();
            raw(barcode, "barcodeModuleWidth", "0.02");
            compare(field(barcode, "barcodeModuleWidth").numericValue(), .508);
            clickButton(barcode, "createBarcodeButton");
            compare(projectSpy.calls[0].args.moduleWidth, .508);
            openBarcode();
            raw(barcode, "barcodeModuleWidth", "0.2 mm");
            clickButton(barcode, "createBarcodeButton");
            compare(projectSpy.calls[1].args.moduleWidth, .2);
        }
        function test_unitSwitchDoesNotReinterpretUncommittedDraft() {
            openBarcode();
            raw(barcode, "barcodeModuleWidth", "0.25");
            verify(field(barcode, "barcodeModuleWidth").draftEdited);
            appServices.lengthUnit = "in";
            wait(0);
            compare(field(barcode, "barcodeModuleWidth").shownUnit, "mm");
            compare(field(barcode, "barcodeModuleWidth").numericValue(), .25);
            clickButton(barcode, "createBarcodeButton");
            compare(projectSpy.calls[0].args.moduleWidth, .25);
        }
        function test_outOfBedBackendRejectionRemainsVisible() {
            openBarcode();
            projectSpy.accept = false;
            raw(barcode, "barcodeLeft", "199 mm");
            if (field(barcode, "createBarcodeButton").enabled) {
                clickButton(barcode, "createBarcodeButton");
                compare(projectSpy.calls.length, 1);
                compare(projectSpy.calls[0].args.x, 199);
                verify(barcode.visible);
                compare(barcode.error, projectSpy.message);
            } else
                compare(projectSpy.calls.length, 0);
        }
        function prepareRecord() {
            materialRecord.open();
            wait(30);
            field(materialRecord, "materialObservationNotes").text = "Observed clean test edge";
            raw(materialRecord, "materialObservationThickness", "2.7 mm");
            verify(field(materialRecord, "saveMaterialObservation").enabled);
        }
        function test_staleParametersDisableRecordAndBlockProgrammaticSignal() {
            prepareRecord();
            deviceSpy.parameterHash = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
            wait(0);
            verify(!materialRecord.sameContext);
            verify(!field(materialRecord, "saveMaterialObservation").enabled);
            field(materialRecord, "saveMaterialObservation").clicked();
            compare(ledgerSpy.calls.length, 0);
            verify(materialRecord.error.length > 0);
        }
        function test_staleDeviceDisableRecord() {
            prepareRecord();
            deviceSpy.bindingKey = "";
            wait(0);
            verify(!materialRecord.sameContext);
            verify(!field(materialRecord, "saveMaterialObservation").enabled);
            compare(ledgerSpy.calls.length, 0);
        }
        function test_oversizedBarcodeIsRejectedWithoutTruncation() {
            openBarcode();
            const text = "a".repeat(129);
            field(barcode, "barcodeText").text = text;
            compare(field(barcode, "barcodeText").text, text);
            verify(!field(barcode, "createBarcodeButton").enabled);
            compare(projectSpy.calls.length, 0);
        }
        function test_materialNotesLimitMatchesBackend() {
            prepareRecord();
            field(materialRecord, "materialObservationNotes").text = "a".repeat(4001);
            verify(!field(materialRecord, "saveMaterialObservation").enabled);
            compare(ledgerSpy.calls.length, 0);
        }
        function allTexts(item) {
            let texts = [];
            if (typeof item.text !== "undefined")
                texts.push(String(item.text));
            for (const child of item.children || [])
                texts = texts.concat(allTexts(child));
            return texts;
        }
        function findText(item, part) {
            if (typeof item.text !== "undefined" && typeof item.textFormat !== "undefined" && String(item.text).includes(part))
                return item;
            for (const child of item.children || []) {
                const found = findText(child, part);
                if (found)
                    return found;
            }
            return null;
        }
        function test_nonAsciiBarcodeInputIsRejected() {
            openBarcode();
            for (const text of ["Müller", "😀", "a\u007fb"]) {
                field(barcode, "barcodeText").text = text;
                compare(field(barcode, "barcodeText").text, text);
                verify(!field(barcode, "createBarcodeButton").enabled);
            }
            compare(projectSpy.calls.length, 0);
        }
        function test_deviceNamesAreRenderedAsPlainText() {
            deviceSpy.instances = [
                {
                    id: "one",
                    name: "<b>Review</b>",
                    endpoint: "/dev/review-only",
                    width: 200,
                    height: 150,
                    driver: "grbl"
                }
            ];
            deviceDialog.open();
            wait(30);
            let label = findText(deviceDialog.contentItem, "<b>Review</b>");
            verify(label !== null);
            compare(label.textFormat, Text.PlainText);
            deviceDialog.close();
            wait(10);
            materialRecord.open();
            wait(30);
            label = findText(materialRecord.contentItem, "<b>Review</b>");
            verify(label !== null);
            compare(label.textFormat, Text.PlainText);
        }
        function test_nestedHistoricalObservationIsDisplayedAsPlainText() {
            ledgerSpy.records = [
                {
                    id: "historical",
                    bindingKey: deviceSpy.bindingKey,
                    parameterHash: deviceSpy.parameterHash,
                    observation: {
                        date: "2026-11-20",
                        material: "<b>Material</b>",
                        thicknessMm: 3,
                        result: "usable",
                        notes: "Independent history observation"
                    }
                }
            ];
            materialRecord.open();
            wait(30);
            compare(materialRecord.matchingRecords.length, 1);
            const text = allTexts(materialRecord.contentItem).join("\n");
            verify(text.includes("2026-11-20 · <b>Material</b>"), text);
            verify(text.includes("Independent history observation"), text);
            const label = findText(materialRecord.contentItem, "Independent history observation");
            verify(label !== null);
            compare(label.textFormat, Text.PlainText);
        }
        function test_deviceEditsMustBeSavedBeforeActivation() {
            deviceDialog.open();
            wait(30);
            verify(field(deviceDialog, "activateDeviceInstance").enabled);
            field(deviceDialog, "deviceInstanceEndpoint").text = "/dev/new-review-only";
            verify(deviceDialog.dirty);
            verify(!field(deviceDialog, "activateDeviceInstance").enabled);
            compare(deviceSpy.managementCalls.length, 0);
            clickButton(deviceDialog, "saveDeviceInstance");
            verify(!deviceDialog.dirty);
            verify(field(deviceDialog, "activateDeviceInstance").enabled);
            clickButton(deviceDialog, "activateDeviceInstance");
            compare(deviceSpy.managementCalls.length, 2);
            compare(deviceSpy.managementCalls[0].action, "update");
            compare(deviceSpy.managementCalls[1].action, "activate");
            compare(deviceSpy.managementCalls[1].endpoint, "/dev/new-review-only");
        }
        function test_recordConsumesRawThickness() {
            prepareRecord();
            clickButton(materialRecord, "saveMaterialObservation");
            compare(ledgerSpy.calls.length, 1);
            compare(ledgerSpy.calls[0].observation.thicknessMm, 2.7);
            compare(ledgerSpy.calls[0].observation.result, "inconclusive");
        }
    }
}
